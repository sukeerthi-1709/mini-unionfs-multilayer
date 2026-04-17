#include "../include/unionfs.h"

/* ── unionfs_open (CoW trigger) ──────────────────────────────────────
 *
 * Cases handled:
 *
 *  O_RDONLY  → no CoW, return immediately.
 *
 *  Write intent (O_WRONLY / O_RDWR / O_APPEND / O_TRUNC):
 *    a) File already in upper        → no copy needed, write in place.
 *    b) Whiteout exists in upper     → file is logically absent;
 *                                      upper_dir will host the new file,
 *                                      whiteout will be removed on create.
 *                                      No copy needed here.
 *    c) File found in a lower layer  → CoW: copy from top-most lower to upper,
 *                                      preserving mode bits.
 *    d) File nowhere                 → nothing to CoW; create will handle it.
 */
int unionfs_open(const char *path, struct fuse_file_info *fi)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    /* Pure read → never trigger CoW */
    int accmode     = fi->flags & O_ACCMODE;
    int write_intent = (accmode == O_WRONLY) ||
                       (accmode == O_RDWR)   ||
                       (fi->flags & O_APPEND)||
                       (fi->flags & O_TRUNC);
    if (!write_intent)
        return 0;

    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);

    /* Already in upper — no copy needed */
    if (access(upper, F_OK) == 0)
        return 0;

    /* Whiteout present — file is logically absent; let write/create proceed */
    char wh[512];
    if (whiteout_path(path, wh, sizeof(wh)) == 0 &&
        access(wh, F_OK) == 0)
        return 0;

    /* Find the top-most lower layer that has this file */
    char lower_src[512];
    if (find_in_lowers(fs, path, lower_src) < 0)
        return 0;   /* not in any lower → new file, create handles it */

    /* CoW: copy lower_src → upper, preserving permissions */
    struct stat st;
    if (stat(lower_src, &st) != 0)
        return -errno;

    FILE *src = fopen(lower_src, "rb");
    if (!src) return -errno;

    FILE *dst = fopen(upper, "wb");
    if (!dst) { fclose(src); return -errno; }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src); fclose(dst);
            unlink(upper);   /* clean up partial copy */
            return -EIO;
        }
    }

    fclose(src);
    fclose(dst);
    chmod(upper, st.st_mode);

    return 0;
}
