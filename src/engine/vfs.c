#include "vfs.h"
#include "lib/json.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int canonical(const char *path) {
    if (*path++ != '/') return 0;
    if (!*path) return 1;
    const char *start = path;
    for (;;) {
        unsigned char ch = (unsigned char)*path;
        if (!ch || ch == '/') {
            size_t n = (size_t)(path - start);
            if (!n || (n == 1 && *start == '.') ||
                (n == 2 && start[0] == '.' && start[1] == '.')) return 0;
            if (!ch) return 1;
            start = path + 1;
        } else if (ch < 32 || ch == '\\') return 0;
        path++;
    }
}

static int number(json_view object, const char *key, uint64_t limit,
                   int signed_value, uint64_t *out) {
    json_view v;
    if (!member(object, key, &v) || v.type == '"') return 0;
    const char *p = v.start;
    int negative = p < v.end && *p == '-';
    if (negative) { if (!signed_value) return 0; p++; limit++; }
    if (p == v.end) return 0;
    uint64_t n = 0;
    for (; p < v.end; p++) {
        if (*p < '0' || *p > '9') return 0;
        unsigned digit = (unsigned)(*p - '0');
        if (digit > limit || n > (limit - digit) / 10u) return 0;
        n = n * 10u + digit;
    }
    *out = negative ? UINT64_C(0) - n : n;
    return 1;
}

void waste_vfs_free(waste_vfs *vfs) {
    if (!vfs) return;
    for (uint32_t i = 0; i < vfs->count; i++) free(vfs->entries[i].data);
    free(vfs->entries);
    memset(vfs, 0, sizeof(*vfs));
}

int waste_vfs_inventory(const char *json, size_t length, waste_vfs *vfs) {
    if (!json || !vfs || !length || length > WASTE_VFS_INVENTORY_MAX) return -1;
    json_reader reader = {json, json + length};
    json_view root, entries;
    uint64_t version;
    if (!value(&reader, &root, 0)) return -1;
    whitespace(&reader);
    if (reader.at != reader.end || !number(root, "version", 1, 0, &version) ||
        version != 1 || !member(root, "entries", &entries) || entries.type != '[') return -1;
    waste_vfs decoded = {0};
    decoded.entries = calloc(WASTE_VFS_MAX_ENTRIES, sizeof(*decoded.entries));
    if (!decoded.entries) return -1;
    reader = (json_reader){entries.start + 1, entries.end - 1};
    whitespace(&reader);
    while (reader.at < reader.end) {
        json_view object;
        uint64_t n;
        char role[64];
        if (decoded.count == WASTE_VFS_MAX_ENTRIES || !value(&reader, &object, 0)) goto invalid;
        waste_vfs_entry *e = &decoded.entries[decoded.count];
        if (!field(object, "path", e->path, sizeof(e->path)) || !canonical(e->path) ||
            !field(object, "role", role, sizeof(role))) goto invalid;
#define FIELD(key, maximum, target) \
        do { if (!number(object, key, maximum, 0, &n)) goto invalid; target = n; } while (0)
        FIELD("kind", POSIX_NODE_DIRECTORY, e->metadata.kind);
        FIELD("mode", 0777u, e->metadata.mode);
        FIELD("uid", UINT32_MAX, e->metadata.uid);
        FIELD("gid", UINT32_MAX, e->metadata.gid);
        FIELD("size", WASTE_VFS_MAX_BYTES, e->metadata.size);
        FIELD("inode", UINT64_MAX, e->metadata.inode);
        FIELD("mtime_nsec", 999999999u, e->metadata.mtime_nsec);
#undef FIELD
        if (!number(object, "mtime_sec", INT64_MAX, 1, &n)) goto invalid;
        e->metadata.mtime_sec = (int64_t)n;
        if (!e->metadata.kind || !e->metadata.inode ||
            (e->metadata.kind == POSIX_NODE_DIRECTORY) != !strcmp(role, "directory")) goto invalid;
        e->interpreter = !strcmp(role, "interpreter");
        e->ready = e->interpreter || e->metadata.kind == POSIX_NODE_DIRECTORY;
        if (e->ready) {
            if (e->metadata.size) goto invalid;
        } else {
            if (!field(object, "sha256", e->sha256, sizeof(e->sha256)) || strlen(e->sha256) != 64)
                goto invalid;
            for (unsigned j = 0; j < 64; j++)
                if (!((e->sha256[j] >= '0' && e->sha256[j] <= '9') ||
                      (e->sha256[j] >= 'a' && e->sha256[j] <= 'f'))) goto invalid;
        }
        if (!decoded.count) {
            if (strcmp(e->path, "/") || e->metadata.kind != POSIX_NODE_DIRECTORY) goto invalid;
        } else {
            const char *slash = e->path;
            for (const char *p = e->path + 1; *p; p++) if (*p == '/') slash = p;
            size_t parent_length = slash == e->path ? 1u : (size_t)(slash - e->path);
            int parent = 0;
            for (uint32_t j = 0; j < decoded.count; j++) {
                waste_vfs_entry *previous = &decoded.entries[j];
                if (!strcmp(e->path, previous->path) || e->metadata.inode == previous->metadata.inode)
                    goto invalid;
                if (previous->metadata.kind == POSIX_NODE_DIRECTORY &&
                    strlen(previous->path) == parent_length &&
                    !memcmp(previous->path, e->path, parent_length)) parent = 1;
            }
            if (!parent) goto invalid;
        }
        if ((size_t)e->metadata.size > WASTE_VFS_MAX_BYTES - decoded.bytes) goto invalid;
        decoded.bytes += (size_t)e->metadata.size;
        decoded.count++;
        whitespace(&reader);
        if (reader.at < reader.end && *reader.at++ != ',') goto invalid;
        whitespace(&reader);
    }
    if (!decoded.count) goto invalid;
    waste_vfs_free(vfs);
    *vfs = decoded;
    return 0;
invalid:
    waste_vfs_free(&decoded);
    return -1;
}

/* SHA-256 enforces the inventory content contract when native files are read
 * directly and when extracted browser files enter the engine. */
static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void sha256(const uint8_t *bytes, size_t length, char output[65]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t padded = (length + 9u + 63u) & ~(size_t)63u;
    for (size_t at = 0; at < padded; at += 64) {
        uint32_t w[64];
        for (unsigned i = 0; i < 16; i++) {
            uint32_t word = 0;
            for (unsigned j = 0; j < 4; j++) {
                size_t p = at + i*4u + j;
                uint8_t b = p < length ? bytes[p] : p == length ? 0x80 :
                    p >= padded - 8u ? (uint8_t)((uint64_t)length*8u >> ((padded-1u-p)*8u)) : 0;
                word = (word << 8) | b;
            }
            w[i] = word;
        }
        for (unsigned i = 16; i < 64; i++) {
            uint32_t a = w[i-15], b = w[i-2];
            w[i] = w[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) + w[i-7] +
                   (rotate(b,17)^rotate(b,19)^(b>>10));
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for (unsigned i = 0; i < 64; i++) {
            uint32_t t = z + (rotate(e,6)^rotate(e,11)^rotate(e,25)) +
                         ((e&f)^(~e&g)) + k[i] + w[i];
            uint32_t u = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            z=g; g=f; f=e; e=d+t; d=c; c=b; b=a; a=t+u;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=z;
    }
    static const char hex_digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 64; i++)
        output[i] = hex_digits[(h[i/8] >> (28u-(i%8u)*4u)) & 15u];
    output[64] = 0;
}

static waste_vfs_entry *validated_file(waste_vfs *vfs, uint32_t index,
                                       const uint8_t *bytes, size_t length) {
    if (!vfs || index >= vfs->count || (!bytes && length)) return NULL;
    waste_vfs_entry *e = &vfs->entries[index];
    if (e->ready || length != (size_t)e->metadata.size) return NULL;
    char hash[65];
    sha256(bytes, length, hash);
    return strcmp(hash, e->sha256) ? NULL : e;
}

int waste_vfs_take_file(waste_vfs *vfs, uint32_t index, uint8_t *bytes, size_t length) {
    waste_vfs_entry *e = validated_file(vfs, index, bytes, length);
    if (!e) return -1;
    e->data = bytes;
    e->ready = 1;
    return 0;
}

int waste_vfs_set_file(waste_vfs *vfs, uint32_t index, const uint8_t *bytes, size_t length) {
    waste_vfs_entry *e = validated_file(vfs, index, bytes, length);
    if (!e) return -1;
    e->data = malloc(length ? length : 1u);
    if (!e->data) return -1;
    if (length) memcpy(e->data, bytes, length);
    e->ready = 1;
    return 0;
}

int waste_vfs_ready(const waste_vfs *vfs) {
    if (!vfs || !vfs->count) return 0;
    for (uint32_t i = 0; i < vfs->count; i++) if (!vfs->entries[i].ready) return 0;
    return 1;
}

int waste_vfs_mount(struct posix_kernel *kernel, const waste_vfs *vfs) {
    if (!kernel || !waste_vfs_ready(vfs)) return -1;
    for (uint32_t i = 0; i < vfs->count; i++) {
        const waste_vfs_entry *e = &vfs->entries[i];
        int rc = posix_kernel_path_add_data(kernel, e->path, &e->metadata,
                                            e->data, (size_t)e->metadata.size);
        if (rc) return rc;
    }
    return 0;
}
