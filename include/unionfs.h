#ifndef UNIONFS_H
#define UNIONFS_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <libgen.h>

/*
 * Multi-lower-layer state.
 *
 * Layer ordering (highest priority first):
 *   upper_dir              — read-write, always wins
 *   lower_dirs[0]          — top-most read-only lower (highest priority)
 *   lower_dirs[1]          — next lower
 *   ...
 *   lower_dirs[n_lowers-1] — bottom-most lower (lowest priority)
 *
 * Command-line usage:
 *   mini_unionfs <upper> <lower0> [lower1 ...] <mountpoint>
 */
#define MAX_LOWER_LAYERS 16

struct mini_unionfs_state {
    char *upper_dir;
    char *lower_dirs[MAX_LOWER_LAYERS];
    int   n_lowers;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ── Core helpers ─────────────────────────────────────────────────── */

/*
 * resolve_path – find the real on-disk path for a virtual path.
 *
 * Algorithm:
 *   1. Build whiteout sentinel path: upper/.wh.<base>
 *      If that exists → return -ENOENT (file logically deleted)
 *   2. If upper/<path> exists → resolved = upper path, return 0
 *   3. For each lower layer, index 0 (top) … n_lowers-1 (bottom):
 *      If lower[i]/<path> exists → resolved = that path, return 0
 *   4. return -ENOENT
 */
int resolve_path(const char *path, char *resolved);

/*
 * whiteout_path – fill `wh` with the whiteout sentinel path.
 * Returns 0 on success, -ENAMETOOLONG on overflow.
 */
int whiteout_path(const char *path, char *wh, size_t wh_size);

/*
 * find_in_lowers – scan lower layers top-to-bottom for `path`.
 * On success fills `found` (must be ≥512 bytes) and returns layer index.
 * Returns -1 if not found anywhere.
 */
int find_in_lowers(const struct mini_unionfs_state *fs,
                   const char *path, char *found);

/* ── POSIX operations ─────────────────────────────────────────────── */
int unionfs_getattr(const char *, struct stat *, struct fuse_file_info *);
int unionfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
                    struct fuse_file_info *, enum fuse_readdir_flags);
int unionfs_read   (const char *, char *, size_t, off_t, struct fuse_file_info *);
int unionfs_write  (const char *, const char *, size_t, off_t, struct fuse_file_info *);
int unionfs_create (const char *, mode_t, struct fuse_file_info *);
int unionfs_mkdir  (const char *, mode_t);
int unionfs_rmdir  (const char *);

/* ── CoW trigger ──────────────────────────────────────────────────── */
int unionfs_open(const char *, struct fuse_file_info *);

/* ── Whiteout / delete ────────────────────────────────────────────── */
int unionfs_unlink(const char *);

#endif /* UNIONFS_H */
