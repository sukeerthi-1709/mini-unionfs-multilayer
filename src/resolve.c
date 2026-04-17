#include "../include/unionfs.h"

/* ── whiteout_path ───────────────────────────────────────────────────
 * Build the whiteout sentinel path for `path` inside upper_dir.
 *
 * For path "/dir/file.txt"  → "<upper>/dir/.wh.file.txt"
 * For path "/file.txt"      → "<upper>/.wh.file.txt"   (avoid double slash)
 */
int whiteout_path(const char *path, char *wh, size_t wh_size)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    /* We need mutable copies because dirname/basename may modify in-place */
    char p1[512], p2[512];
    strncpy(p1, path, sizeof(p1) - 1); p1[sizeof(p1)-1] = '\0';
    strncpy(p2, path, sizeof(p2) - 1); p2[sizeof(p2)-1] = '\0';

    char *dir  = dirname(p1);
    char *base = basename(p2);

    int n;
    if (strcmp(dir, "/") == 0)
        n = snprintf(wh, wh_size, "%s/.wh.%s", fs->upper_dir, base);
    else
        n = snprintf(wh, wh_size, "%s%s/.wh.%s", fs->upper_dir, dir, base);

    if (n < 0 || (size_t)n >= wh_size)
        return -ENAMETOOLONG;
    return 0;
}

/* ── find_in_lowers ──────────────────────────────────────────────────
 * Scan lower layers from index 0 (top/highest-priority) downward.
 * First match wins (top-most lower layer semantics).
 * Returns layer index on success, -1 if not found.
 */
int find_in_lowers(const struct mini_unionfs_state *fs,
                   const char *path, char *found)
{
    for (int i = 0; i < fs->n_lowers; i++) {
        char candidate[512];
        snprintf(candidate, sizeof(candidate), "%s%s", fs->lower_dirs[i], path);
        if (access(candidate, F_OK) == 0) {
            strncpy(found, candidate, 512);
            found[511] = '\0';
            return i;
        }
    }
    return -1;
}

/* ── resolve_path ────────────────────────────────────────────────────
 * Determine the real on-disk path for a virtual `path`.
 *
 * Priority order:
 *   1. Whiteout in upper → ENOENT  (hides ALL lower layers)
 *   2. upper/<path>      → use upper copy
 *   3. lower[0]/<path>   → top-most lower
 *   ...
 *   N. lower[n-1]/<path> → bottom-most lower
 *   fallthrough          → ENOENT
 */
int resolve_path(const char *path, char *resolved)
{
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    /* 1. Whiteout check — upper/.wh.<base> hides the file everywhere */
    char wh[512];
    if (whiteout_path(path, wh, sizeof(wh)) == 0 &&
        access(wh, F_OK) == 0)
        return -ENOENT;

    /* 2. Upper takes unconditional precedence */
    char upper[512];
    snprintf(upper, sizeof(upper), "%s%s", fs->upper_dir, path);
    if (access(upper, F_OK) == 0) {
        strncpy(resolved, upper, 512);
        resolved[511] = '\0';
        return 0;
    }

    /* 3. Walk lower layers top-to-bottom; first match wins */
    char found[512];
    if (find_in_lowers(fs, path, found) >= 0) {
        strncpy(resolved, found, 512);
        resolved[511] = '\0';
        return 0;
    }

    return -ENOENT;
}
