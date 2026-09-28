#include "include/path.h"

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *p, uint64_t value) {
    write_le32(p, (uint32_t)value);
    write_le32(p + 4, (uint32_t)(value >> 32));
}

int posix_path_metadata_validate(const posix_path_metadata *metadata) {
    if (!metadata || metadata->kind > POSIX_NODE_SYMLINK || metadata->size < 0)
        return -1;
    if (metadata->kind == POSIX_NODE_NONE)
        return -1;
    if (metadata->mtime_nsec < 0 || metadata->mtime_nsec >= 1000000000)
        return -1;
    return 0;
}

int posix_path_metadata_encode(uint8_t *bytes,
                               const posix_path_metadata *metadata) {
    if (!bytes || !metadata || posix_path_metadata_validate(metadata) < 0)
        return -1;
    write_le32(bytes, metadata->kind);
    write_le32(bytes + 4, metadata->mode);
    write_le32(bytes + 8, metadata->uid);
    write_le32(bytes + 12, metadata->gid);
    write_le64(bytes + 16, (uint64_t)metadata->size);
    write_le64(bytes + 24, metadata->inode);
    write_le64(bytes + 32, (uint64_t)metadata->mtime_sec);
    write_le64(bytes + 40, (uint64_t)metadata->mtime_nsec);
    return 0;
}

int posix_path_metadata_decode(posix_path_metadata *metadata,
                               const uint8_t *bytes) {
    if (!metadata || !bytes) return -1;
    metadata->kind = read_le32(bytes);
    metadata->mode = read_le32(bytes + 4);
    metadata->uid = read_le32(bytes + 8);
    metadata->gid = read_le32(bytes + 12);
    metadata->size = (int64_t)read_le64(bytes + 16);
    metadata->inode = read_le64(bytes + 24);
    metadata->mtime_sec = (int64_t)read_le64(bytes + 32);
    metadata->mtime_nsec = (int64_t)read_le64(bytes + 40);
    return posix_path_metadata_validate(metadata);
}

int posix_guest_stat_encode(uint8_t *bytes, const posix_guest_stat *stat) {
    if (!bytes || !stat) return -1;
    write_le64(bytes, stat->st_dev);
    write_le64(bytes + 8, stat->st_ino);
    write_le32(bytes + 16, stat->st_mode);
    write_le32(bytes + 20, stat->st_nlink);
    write_le32(bytes + 24, stat->st_uid);
    write_le32(bytes + 28, stat->st_gid);
    write_le64(bytes + 32, stat->st_rdev);
    write_le64(bytes + 40, (uint64_t)stat->st_size);
    write_le64(bytes + 48, (uint64_t)stat->st_blksize);
    write_le64(bytes + 56, (uint64_t)stat->st_blocks);
    write_le64(bytes + 64, (uint64_t)stat->st_atime_sec);
    write_le64(bytes + 72, (uint64_t)stat->st_atime_nsec);
    write_le64(bytes + 80, (uint64_t)stat->st_mtime_sec);
    write_le64(bytes + 88, (uint64_t)stat->st_mtime_nsec);
    write_le64(bytes + 96, (uint64_t)stat->st_ctime_sec);
    write_le64(bytes + 104, (uint64_t)stat->st_ctime_nsec);
    for (size_t i = 0; i < sizeof(stat->reserved); i++) bytes[112 + i] = stat->reserved[i];
    return 0;
}

int posix_guest_stat_decode(posix_guest_stat *stat, const uint8_t *bytes) {
    if (!stat || !bytes) return -1;
    stat->st_dev = read_le64(bytes);
    stat->st_ino = read_le64(bytes + 8);
    stat->st_mode = read_le32(bytes + 16);
    stat->st_nlink = read_le32(bytes + 20);
    stat->st_uid = read_le32(bytes + 24);
    stat->st_gid = read_le32(bytes + 28);
    stat->st_rdev = read_le64(bytes + 32);
    stat->st_size = (int64_t)read_le64(bytes + 40);
    stat->st_blksize = (int64_t)read_le64(bytes + 48);
    stat->st_blocks = (int64_t)read_le64(bytes + 56);
    stat->st_atime_sec = (int64_t)read_le64(bytes + 64);
    stat->st_atime_nsec = (int64_t)read_le64(bytes + 72);
    stat->st_mtime_sec = (int64_t)read_le64(bytes + 80);
    stat->st_mtime_nsec = (int64_t)read_le64(bytes + 88);
    stat->st_ctime_sec = (int64_t)read_le64(bytes + 96);
    stat->st_ctime_nsec = (int64_t)read_le64(bytes + 104);
    for (size_t i = 0; i < sizeof(stat->reserved); i++) stat->reserved[i] = bytes[112 + i];
    return 0;
}
