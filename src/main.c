#include "../include/unionfs.h"

static struct fuse_operations ops = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .create  = unionfs_create,
    .mkdir   = unionfs_mkdir,
    .rmdir   = unionfs_rmdir,
    .open    = unionfs_open,
    .unlink  = unionfs_unlink,
};

/*
 * Usage: mini_unionfs <upper> <lower0> [lower1 ...] <mountpoint>
 *
 * Argument layout:
 *   argv[0]          binary name
 *   argv[1]          upper_dir  (read-write)
 *   argv[2..argc-2]  lower dirs (read-only, descending priority)
 *   argv[argc-1]     mountpoint
 *
 * Minimum: upper + one lower + mountpoint = 4 arguments total.
 */
int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <upper> <lower0> [lower1 ...] <mountpoint>\n"
            "  upper      — read-write layer (container layer)\n"
            "  lower0...  — read-only layers, highest priority first\n"
            "  mountpoint — where to mount the unified view\n",
            argv[0]);
        return 1;
    }

    int n_lowers = argc - 3;   /* everything between upper and mountpoint */
    if (n_lowers > MAX_LOWER_LAYERS) {
        fprintf(stderr, "Error: too many lower layers (max %d)\n",
                MAX_LOWER_LAYERS);
        return 1;
    }

    struct mini_unionfs_state *fs = calloc(1, sizeof(*fs));
    if (!fs) { perror("calloc"); return 1; }

    /* argv[1] = upper */
    fs->upper_dir = realpath(argv[1], NULL);
    if (!fs->upper_dir) {
        fprintf(stderr, "Error: bad upper path '%s': %s\n",
                argv[1], strerror(errno));
        return 1;
    }

    /* argv[2 .. argc-2] = lowers */
    fs->n_lowers = n_lowers;
    for (int i = 0; i < n_lowers; i++) {
        fs->lower_dirs[i] = realpath(argv[2 + i], NULL);
        if (!fs->lower_dirs[i]) {
            fprintf(stderr, "Error: bad lower path '%s': %s\n",
                    argv[2 + i], strerror(errno));
            return 1;
        }
        printf("[mini-unionfs] lower[%d] = %s\n", i, fs->lower_dirs[i]);
    }
    printf("[mini-unionfs] upper    = %s\n", fs->upper_dir);
    printf("[mini-unionfs] mount    = %s\n", argv[argc - 1]);

    /* Hand only binary name + mountpoint to fuse_main */
    char *fuse_argv[] = { argv[0], argv[argc - 1] };
    return fuse_main(2, fuse_argv, &ops, fs);
}
