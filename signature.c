// Copyright © 2025 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#include "signature.h"

#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "runtime_dispatch.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// Simple xxHash64 implementation for first 4KB
// Based on xxHash by Yann Collet
static const uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
static const uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
static const uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
static const uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
static const uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;

static inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

uint64_t signature_fast_hash_bytes(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = PRIME64_1 + PRIME64_2;
        uint64_t v2 = PRIME64_2;
        uint64_t v3 = 0;
        uint64_t v4 = -(int64_t)PRIME64_1;

        do {
            v1 += *(uint64_t*)p * PRIME64_2; v1 = rotl64(v1, 31); v1 *= PRIME64_1; p += 8;
            v2 += *(uint64_t*)p * PRIME64_2; v2 = rotl64(v2, 31); v2 *= PRIME64_1; p += 8;
            v3 += *(uint64_t*)p * PRIME64_2; v3 = rotl64(v3, 31); v3 *= PRIME64_1; p += 8;
            v4 += *(uint64_t*)p * PRIME64_2; v4 = rotl64(v4, 31); v4 *= PRIME64_1; p += 8;
        } while (p <= limit);

        h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);

        v1 *= PRIME64_2; v1 = rotl64(v1, 31); v1 *= PRIME64_1; h64 ^= v1; h64 = h64 * PRIME64_1 + PRIME64_4;
        v2 *= PRIME64_2; v2 = rotl64(v2, 31); v2 *= PRIME64_1; h64 ^= v2; h64 = h64 * PRIME64_1 + PRIME64_4;
        v3 *= PRIME64_2; v3 = rotl64(v3, 31); v3 *= PRIME64_1; h64 ^= v3; h64 = h64 * PRIME64_1 + PRIME64_4;
        v4 *= PRIME64_2; v4 = rotl64(v4, 31); v4 *= PRIME64_1; h64 ^= v4; h64 = h64 * PRIME64_1 + PRIME64_4;
    } else {
        h64 = PRIME64_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = *(uint64_t*)p;
        k1 *= PRIME64_2; k1 = rotl64(k1, 31); k1 *= PRIME64_1;
        h64 ^= k1; h64 = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
    }

    if (p + 4 <= end) {
        h64 ^= (uint64_t)(*(uint32_t*)p) * PRIME64_1;
        h64 = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p++) * PRIME64_5;
        h64 = rotl64(h64, 11) * PRIME64_1;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}

static bool read_sample_into(int fd, off_t position, uint64_t size, int32_t* out) {
    unsigned char raw[sizeof(int32_t)] = {0};
    size_t remaining = 0;

    if (position < 0 || (uint64_t)position >= size) {
        return false;
    }

    remaining = (size_t)(size - (uint64_t)position);
    size_t to_read = remaining < sizeof(raw) ? remaining : sizeof(raw);
    ssize_t n = pread(fd, raw, to_read, position);
    if (n < 0 || (size_t)n != to_read) {
        return false;
    }

    memcpy(out, raw, sizeof(raw));
    return true;
}

FileSignature* compute_signature(const char* path, dev_t device, uint64_t size) {
    FileSignature* sig = calloc(1, sizeof(FileSignature));
    if (!sig) {
        return NULL;
    }

    sig->device = device;
    sig->size = size;

    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        free(sig);
        return NULL;
    }

    // Clear O_NONBLOCK for actual I/O operations
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    if (size == 0) {
        sig->quick_hash = signature_fast_hash_bytes("", 0);
        close(fd);
        return sig;
    }

    // Sample at 4 strategic positions
    off_t positions[4] = {
        0,                               // Start
        (off_t)(size / 3),               // 1/3 point
        (off_t)((size * 2) / 3),         // 2/3 point
        (off_t)(size > 4 ? size - 4 : 0) // End (or start if file < 4 bytes)
    };

#ifdef __ARM_NEON
    if (size >= 16) {
        char buf[16] __attribute__((aligned(16)));
        for (int i = 0; i < 4; i++) {
            if (pread(fd, &buf[i * 4], 4, positions[i]) != 4) {
                close(fd);
                free(sig);
                return NULL;
            }
        }
        int32x4_t samples_vec = vld1q_s32((int32_t*)buf);
        vst1q_s32(sig->samples, samples_vec);
    } else
#endif
    {
        for (int i = 0; i < 4; i++) {
            if (!read_sample_into(fd, positions[i], size, &sig->samples[i])) {
                close(fd);
                free(sig);
                return NULL;
            }
        }
    }

    size_t hash_size = size < 4096 ? (size_t)size : 4096U;
    char* buf = malloc(hash_size);
    if (!buf) {
        close(fd);
        free(sig);
        return NULL;
    }

    ssize_t n = pread(fd, buf, hash_size, 0);
    if (n < 0 || (size_t)n != hash_size) {
        free(buf);
        close(fd);
        free(sig);
        return NULL;
    }

    dedup_fast_hash_fn hash_fn = signature_fast_hash_bytes;
    const DedupRuntimeDispatch* dispatch = dedup_runtime_dispatch_get();
    if (dispatch && dispatch->fast_hash) {
        hash_fn = dispatch->fast_hash;
    }
    sig->quick_hash = hash_fn(buf, (size_t)n);

    free(buf);
    close(fd);

    return sig;
}

void free_signature(FileSignature* sig) {
    free(sig);
}

bool signatures_match(const FileSignature* a, const FileSignature* b) {
    if (!a || !b) {
        return false;
    }

    if (a->device != b->device) return false;
    if (a->size != b->size) return false;
    if (a->quick_hash != b->quick_hash) return false;

#ifdef __ARM_NEON
    int32x4_t a_vec = vld1q_s32(a->samples);
    int32x4_t b_vec = vld1q_s32(b->samples);
    uint32x4_t cmp = vceqq_s32(a_vec, b_vec);
    uint32x2_t tmp = vand_u32(vget_low_u32(cmp), vget_high_u32(cmp));
    return vget_lane_u32(vpmin_u32(tmp, tmp), 0) == 0xFFFFFFFF;
#else
    return memcmp(a->samples, b->samples, sizeof(a->samples)) == 0;
#endif
}

static bool files_match_exact_impl(const char* a_path, const char* b_path, size_t chunk_size, bool use_xor_or) {
    if (!a_path || !b_path || chunk_size == 0) {
        return false;
    }

    int a_fd = open(a_path, O_RDONLY);
    if (a_fd < 0) {
        return false;
    }

    int b_fd = open(b_path, O_RDONLY);
    if (b_fd < 0) {
        close(a_fd);
        return false;
    }

    struct stat a_st = {0};
    struct stat b_st = {0};
    bool equal = false;

    if (fstat(a_fd, &a_st) != 0 || fstat(b_fd, &b_st) != 0) {
        goto cleanup;
    }

    if (a_st.st_size != b_st.st_size) {
        goto cleanup;
    }

#ifdef POSIX_FADV_SEQUENTIAL
    (void)posix_fadvise(a_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    (void)posix_fadvise(b_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    unsigned char* a_buf = malloc(chunk_size);
    unsigned char* b_buf = malloc(chunk_size);
    if (!a_buf || !b_buf) {
        free(a_buf);
        free(b_buf);
        goto cleanup;
    }

    off_t offset = 0;
    equal = true;
    while (offset < a_st.st_size) {
        size_t remaining = (size_t)(a_st.st_size - offset);
        size_t to_read = remaining < chunk_size ? remaining : chunk_size;
        ssize_t a_read = pread(a_fd, a_buf, to_read, offset);
        ssize_t b_read = pread(b_fd, b_buf, to_read, offset);
        if (a_read < 0 || b_read < 0 || a_read != b_read || (size_t)a_read != to_read) {
            equal = false;
            break;
        }

        if (use_xor_or) {
#ifdef __ARM_NEON
            uint8x16_t diff_acc = vdupq_n_u8(0);
            size_t i = 0;
            for (; i + 16 <= to_read; i += 16) {
                uint8x16_t va = vld1q_u8(a_buf + i);
                uint8x16_t vb = vld1q_u8(b_buf + i);
                diff_acc = vorrq_u8(diff_acc, veorq_u8(va, vb));
            }
            if (vmaxvq_u8(diff_acc) != 0) {
                equal = false;
                break;
            }
            for (; i < to_read; i++) {
                if ((unsigned char)(a_buf[i] ^ b_buf[i]) != 0) {
                    equal = false;
                    break;
                }
            }
            if (!equal) {
                break;
            }
#else
            unsigned char diff = 0;
            for (size_t i = 0; i < to_read; i++) {
                diff |= (unsigned char)(a_buf[i] ^ b_buf[i]);
            }
            if (diff != 0) {
                equal = false;
                break;
            }
#endif
        } else if (memcmp(a_buf, b_buf, to_read) != 0) {
            equal = false;
            break;
        }

        offset += a_read;
    }

    free(a_buf);
    free(b_buf);

cleanup:
    close(a_fd);
    close(b_fd);
    return equal;
}

bool files_match_exact(const char* a_path, const char* b_path) {
    return files_match_exact_memcmp(a_path, b_path);
}

bool files_match_exact_memcmp(const char* a_path, const char* b_path) {
    return files_match_exact_impl(a_path, b_path, 64U * 1024U, false);
}

bool files_match_exact_xor_or(const char* a_path, const char* b_path) {
    return files_match_exact_impl(a_path, b_path, 64U * 1024U, true);
}

bool files_match_exact_cpu_tiles(const char* a_path, const char* b_path) {
    return files_match_exact_impl(a_path, b_path, 1024U * 1024U, true);
}

uint64_t hash_signature(const FileSignature* sig) {
    uint64_t h = sig->size;
    h ^= (uint64_t)sig->device + 0x9e3779b97f4a7c15ULL;
    h ^= sig->quick_hash + 0x9e3779b97f4a7c15ULL;

#ifdef __ARM_NEON
    int32x4_t samples_vec = vld1q_s32(sig->samples);
    uint64x2_t hash_vec = vreinterpretq_u64_s32(samples_vec);
    h ^= vgetq_lane_u64(hash_vec, 0);
    h ^= vgetq_lane_u64(hash_vec, 1);
#else
    for (int i = 0; i < 4; i++) {
        h ^= (uint64_t)sig->samples[i] * PRIME64_1;
        h = rotl64(h, 27);
    }
#endif

    return h;
}
