#include "../include/unionfs.h"

/* ── unionfs_unlink ──────────────────────────────────────────────────
 *
 * Cases handled:
 *
 *  1. File in upper only (brand-new, never in any lower):
 *       DELETE upper/<path>
 *       No whiteout needed — nothing to hide in lowers.
 *
 *  2. File in one or more lowers (may also be in upper due to CoW):
 *       DELETE upper/<path>  if present
 *       CREATE upper/.wh.<base>
 *       One whiteout hides the file across ALL lower layers simultaneously.
 *
 *  3. Already whiteout-ed (upper/.wh.<base> exists):
 *       File is already logically absent — return success, do nothing.
 *
 *  4. File not found anywhere:
 *       Return -ENOENT.
 */
int unionfs_unlink(const char *path)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);

    /* Build whiteout path */
    char wh[512];
    if (whiteout_path(path, wh, sizeof(wh)) != 0)
        return -ENAMETOOLONG;

    /* Case 3: already whiteout-ed → nothing to do */
    if (access(wh, F_OK) == 0)
        return 0;

    int in_upper = (access(upper, F_OK) == 0);

    /* Check if the file exists in any lower layer */
    char lower_found[512];
    int  in_lower = (find_in_lowers(fs, path, lower_found) >= 0);

    if (!in_upper && !in_lower)
        return -ENOENT;

    /* Remove from upper if present (CoW copy or upper-only new file) */
    if (in_upper) {
        if (unlink(upper) != 0)
            return -errno;
    }

    /* If the file lived in any lower, create one whiteout that masks all of them */
    if (in_lower) {
        int fd = open(wh, O_CREAT | O_WRONLY, 0644);
        if (fd == -1) return -errno;
        close(fd);
    }

    return 0;
}
