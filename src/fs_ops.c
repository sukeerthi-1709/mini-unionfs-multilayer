#include "../include/unionfs.h"

/* ── getattr ─────────────────────────────────────────────────────────
 * resolve_path already encodes the full priority chain (whiteout →
 * upper → lower[0] → … → lower[n-1]), so getattr is unchanged.
 */
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi)
{
    (void)fi;
    char resolved[512];
    if (resolve_path(path, resolved) != 0)
        return -ENOENT;

    if (lstat(resolved, stbuf) == -1)
        return -errno;

    return 0;
}

/* ── read ────────────────────────────────────────────────────────────
 * resolve_path returns the highest-priority real path; pread from it.
 */
int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi)
{
    (void)fi;
    char resolved[512];
    if (resolve_path(path, resolved) != 0)
        return -ENOENT;

    int fd = open(resolved, O_RDONLY);
    if (fd == -1) return -errno;

    ssize_t res = pread(fd, buf, size, offset);
    close(fd);
    return (int)res;
}

/* ── write ───────────────────────────────────────────────────────────
 * Always writes to upper (CoW in unionfs_open guarantees the file is
 * already there before write is called).
 */
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);

    int flags = O_WRONLY | O_CREAT;
    if (fi->flags & O_APPEND) flags |= O_APPEND;

    int fd = open(upper, flags, 0644);
    if (fd == -1) return -errno;

    ssize_t res;
    if (fi->flags & O_APPEND)
        res = write(fd, buf, size);
    else
        res = pwrite(fd, buf, size, offset);

    if (res == -1) res = -errno;
    close(fd);
    return (int)res;
}

/* ── create ──────────────────────────────────────────────────────────
 * New files always land in upper.
 * If a whiteout exists for this name, remove it so the file reappears.
 */
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi)
{
    (void)fi;
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);

    /* Remove stale whiteout if present */
    char wh[512];
    if (whiteout_path(path, wh, sizeof(wh)) == 0)
        unlink(wh);   /* ignore error — may not exist */

    int fd = open(upper, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1) return -errno;

    close(fd);
    return 0;
}

/* ── mkdir ───────────────────────────────────────────────────────────
 * Directories are always created in upper.
 */
int unionfs_mkdir(const char *path, mode_t mode)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char full[512];
    snprintf(full, sizeof(full), "%s%s", fs->upper_dir, path);

    if (mkdir(full, mode) == -1)
        return -errno;
    return 0;
}

/* ── rmdir ───────────────────────────────────────────────────────────
 * Only removes from upper (lower dirs are read-only).
 */
int unionfs_rmdir(const char *path)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char full[512];
    snprintf(full, sizeof(full), "%s%s", fs->upper_dir, path);

    if (rmdir(full) == -1)
        return -errno;
    return 0;
}

/* ── readdir ─────────────────────────────────────────────────────────
 * Merged directory listing: upper + all lowers, with deduplication and
 * whiteout filtering.
 *
 * Algorithm:
 *  1. Open upper dir; add all entries EXCEPT .wh.* sentinels.
 *     Record names seen and whiteouts found in this pass.
 *  2. For each lower layer (index 0 = top … n-1 = bottom):
 *     Open that layer's version of `path` (may not exist — skip).
 *     For each entry:
 *       - Skip if already seen (dedup).
 *       - Skip if a whiteout exists in upper for this name.
 *       - Otherwise add and mark seen.
 *
 * The seen[] table uses a flat array with linear search; fine for the
 * directory sizes typical in container image layers.
 */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);

    /*
     * Seen-name table + whiteout set.
     * We allocate on the heap so large directories don't blow the FUSE stack.
     */
#define MAX_ENTRIES 4096
#define NAME_LEN     256

    char (*seen)[NAME_LEN]     = calloc(MAX_ENTRIES, NAME_LEN);
    char (*whiteouts)[NAME_LEN] = calloc(MAX_ENTRIES, NAME_LEN);
    if (!seen || !whiteouts) {
        free(seen); free(whiteouts);
        return -ENOMEM;
    }
    int n_seen = 0, n_wh = 0;

    (void)offset; (void)fi; (void)flags;
    DIR *dp;
    struct dirent *de;

    /* ── Pass 1: upper directory ─────────────────────────────────── */
    dp = opendir(upper);
    if (dp) {
        while ((de = readdir(dp))) {
            /* Collect whiteout sentinels but never expose them */
            if (strncmp(de->d_name, ".wh.", 4) == 0) {
                if (n_wh < MAX_ENTRIES)
                    strncpy(whiteouts[n_wh++], de->d_name + 4, NAME_LEN - 1);
                continue;
            }

            filler(buf, de->d_name, NULL, 0, 0);
            if (n_seen < MAX_ENTRIES)
                strncpy(seen[n_seen++], de->d_name, NAME_LEN - 1);
        }
        closedir(dp);
    }

    /* ── Pass 2: each lower layer, top-to-bottom ─────────────────── */
    for (int li = 0; li < fs->n_lowers; li++) {
        char lower_path[512];
        snprintf(lower_path, sizeof(lower_path), "%s%s",
                 fs->lower_dirs[li], path);

        dp = opendir(lower_path);
        if (!dp) continue;   /* this layer has no such directory */

        while ((de = readdir(dp))) {
            /* Never expose whiteout sentinels from any layer */
            if (strncmp(de->d_name, ".wh.", 4) == 0)
                continue;

            /* Dedup: already added from upper or a higher lower layer */
            int skip = 0;
            for (int i = 0; i < n_seen && !skip; i++)
                if (strcmp(seen[i], de->d_name) == 0)
                    skip = 1;

            /* Whiteout: upper has .wh.<name> → logically deleted */
            for (int i = 0; i < n_wh && !skip; i++)
                if (strcmp(whiteouts[i], de->d_name) == 0)
                    skip = 1;

            if (!skip) {
                filler(buf, de->d_name, NULL, 0, 0);
                if (n_seen < MAX_ENTRIES)
                    strncpy(seen[n_seen++], de->d_name, NAME_LEN - 1);
            }
        }
        closedir(dp);
    }

    free(seen);
    free(whiteouts);
    return 0;

#undef MAX_ENTRIES
#undef NAME_LEN
}
