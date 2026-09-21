#ifndef WASTE_POSIX_PATH_H
#define WASTE_POSIX_PATH_H

#include <stddef.h>
#include <stdint.h>

/* POSIX values used by the engine-owned pathname boundary.  They are kept
 * here, rather than taking host errno values, so native and Wasm runtimes
 * expose the same contract. */
#define POSIX_EFAULT  14
#define POSIX_ENOENT   2
#define POSIX_EACCES  13
#define POSIX_ENOTDIR 20
#define POSIX_EISDIR  21
#define POSIX_ERANGE  34
#define POSIX_ELOOP   40
#define POSIX_ENOTEMPTY 39
#define POSIX_ESPIPE   29
#define POSIX_ENOSYS  38
#define POSIX_ENOSPC  28

#define POSIX_PATH_MAX 4096
#define POSIX_PATH_NODE_MAX 32
#define POSIX_PATH_NODE_NAME_MAX 256

#define POSIX_F_OK 0
#define POSIX_X_OK 1
#define POSIX_W_OK 2
#define POSIX_R_OK 4

/* Versioned imports reserved for the libc/kernel boundary.  Stage 4 wires
 * these names to the per-instance kernel; keeping the names here freezes the
 * ABI before any implementation-specific lookup code is added. */
#define POSIX_PATH_ABI_VERSION 1
#define POSIX_KERNEL_PATH_ACCESS_V1 "path_access_v1"
#define POSIX_KERNEL_PATH_STAT_V1   "path_stat_v1"

typedef enum {
    POSIX_NODE_NONE = 0,
    POSIX_NODE_REGULAR = 1,
    POSIX_NODE_DIRECTORY = 2,
    POSIX_NODE_SYMLINK = 3,
} posix_node_kind;

/* Pointer-free metadata used between the kernel and libc.  The record is
 * deliberately smaller than public struct stat and has a version-stable
 * little-endian wire representation. */
typedef struct {
    uint32_t kind;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int64_t size;
    uint64_t inode;
} posix_path_metadata;

#define POSIX_PATH_METADATA_BYTES 32

_Static_assert(sizeof(posix_path_metadata) == POSIX_PATH_METADATA_BYTES,
               "path metadata ABI must remain 32 bytes");
_Static_assert(_Alignof(posix_path_metadata) == 8, "metadata alignment");
_Static_assert(offsetof(posix_path_metadata, kind) == 0, "kind offset");
_Static_assert(offsetof(posix_path_metadata, mode) == 4, "mode offset");
_Static_assert(offsetof(posix_path_metadata, uid) == 8, "uid offset");
_Static_assert(offsetof(posix_path_metadata, gid) == 12, "gid offset");
_Static_assert(offsetof(posix_path_metadata, size) == 16, "size offset");
_Static_assert(offsetof(posix_path_metadata, inode) == 24, "inode offset");

/* WASTE guest struct stat ABI.  This is a Wasm32 contract, not the build
 * host's struct stat.  The final 16 bytes are reserved for future fields. */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime_sec;
    int64_t st_atime_nsec;
    int64_t st_mtime_sec;
    int64_t st_mtime_nsec;
    int64_t st_ctime_sec;
    int64_t st_ctime_nsec;
    uint8_t reserved[16];
} posix_guest_stat;

#define POSIX_GUEST_STAT_BYTES 128

_Static_assert(sizeof(posix_guest_stat) == POSIX_GUEST_STAT_BYTES,
               "guest stat ABI must remain 128 bytes");
_Static_assert(_Alignof(posix_guest_stat) == 8, "guest stat alignment");
_Static_assert(offsetof(posix_guest_stat, st_mode) == 16, "stat mode offset");
_Static_assert(offsetof(posix_guest_stat, st_size) == 40, "stat size offset");
_Static_assert(offsetof(posix_guest_stat, st_atime_sec) == 64,
               "stat atime offset");
_Static_assert(offsetof(posix_guest_stat, st_ctime_nsec) == 104,
               "stat ctime offset");

int posix_path_metadata_validate(const posix_path_metadata *metadata);
int posix_path_metadata_encode(uint8_t *bytes,
                               const posix_path_metadata *metadata);
int posix_path_metadata_decode(posix_path_metadata *metadata,
                               const uint8_t *bytes);
int posix_guest_stat_encode(uint8_t *bytes, const posix_guest_stat *stat);
int posix_guest_stat_decode(posix_guest_stat *stat, const uint8_t *bytes);

/* Kernel-owned, pointer-free pathname operations. The input is an explicit
 * byte span and is never retained. Results are zero or a negative POSIX
 * errno. */
struct posix_kernel;
int posix_kernel_path_add(struct posix_kernel *kernel, const char *path,
                          const posix_path_metadata *metadata);
int posix_kernel_path_add_data(struct posix_kernel *kernel, const char *path,
                               const posix_path_metadata *metadata,
                               const uint8_t *data, size_t length);
int posix_kernel_path_add_symlink(struct posix_kernel *kernel, const char *path,
                                  const posix_path_metadata *metadata,
                                  const char *target);
int posix_kernel_getcwd(const struct posix_kernel *kernel, char *buffer,
                        size_t capacity);
int posix_kernel_path_mkdir(struct posix_kernel *kernel, const uint8_t *path,
                            size_t length, int mode);
int posix_kernel_path_unlink(struct posix_kernel *kernel, const uint8_t *path,
                             size_t length, int directory);
int posix_kernel_path_rename(struct posix_kernel *kernel,
                             const uint8_t *source, size_t source_length,
                             const uint8_t *destination, size_t destination_length);
int posix_kernel_path_readlink(struct posix_kernel *kernel, const uint8_t *path,
                               size_t length, char *buffer, size_t capacity);
int posix_kernel_path_set_cwd(struct posix_kernel *kernel, const char *path);
int posix_kernel_path_stat(struct posix_kernel *kernel, const uint8_t *path,
                           size_t length, int follow,
                           posix_path_metadata *metadata);
int posix_kernel_path_access(struct posix_kernel *kernel, const uint8_t *path,
                             size_t length, int mode, int flags);

#endif /* WASTE_POSIX_PATH_H */
