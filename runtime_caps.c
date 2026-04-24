// Copyright © 2026 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#include "runtime_caps.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

static DedupRuntimeCaps g_runtime_caps;
static bool g_runtime_caps_initialized = false;
static volatile int g_memcmp_bench_sink = 0;
static volatile int g_exact_bench_sink = 0;

#if defined(__APPLE__)
static bool have_sysctl_u32(const char* name) {
    uint32_t value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, NULL, 0) != 0 || size != sizeof(value)) {
        return false;
    }
    return value != 0;
}
#endif

static bool env_is_enabled(const char* name) {
    const char* value = getenv(name);
    return value && strcmp(value, "1") == 0;
}

static bool detect_metal_available(void) {
#if defined(__APPLE__)
    return access("/System/Library/Frameworks/Metal.framework/Metal", F_OK) == 0;
#else
    return false;
#endif
}

static double monotonic_seconds(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static double benchmark_memcmp_bucket(size_t size) {
    if (size == 0) {
        return 0.0;
    }

    unsigned char* a = malloc(size);
    unsigned char* b = malloc(size);
    if (!a || !b) {
        free(a);
        free(b);
        return 0.0;
    }

    memset(a, 0x5A, size);
    memset(b, 0x5A, size);

    size_t iterations = (32U * 1024U * 1024U) / size;
    if (iterations < 8) {
        iterations = 8;
    }
    if (iterations > 8192) {
        iterations = 8192;
    }

    for (size_t i = 0; i < 4; i++) {
        g_memcmp_bench_sink |= memcmp(a, b, size);
    }

    double start = monotonic_seconds();
    for (size_t i = 0; i < iterations; i++) {
        g_memcmp_bench_sink |= memcmp(a, b, size);
    }
    double end = monotonic_seconds();

    free(a);
    free(b);

    double elapsed = end - start;
    if (elapsed <= 0.0) {
        return 0.0;
    }

    double total_bytes = (double)size * (double)iterations;
    return total_bytes / elapsed / (1024.0 * 1024.0 * 1024.0);
}

static bool compare_exact_tile_buffers(const unsigned char* a, const unsigned char* b, size_t size,
                                       size_t chunk_size, bool use_xor_or) {
    if (!a || !b || size == 0 || chunk_size == 0) {
        return false;
    }

    for (size_t offset = 0; offset < size; offset += chunk_size) {
        size_t to_read = size - offset;
        if (to_read > chunk_size) {
            to_read = chunk_size;
        }

        if (use_xor_or) {
            unsigned char diff = 0;
            for (size_t i = 0; i < to_read; i++) {
                diff |= (unsigned char)(a[offset + i] ^ b[offset + i]);
            }
            if (diff != 0) {
                return false;
            }
        } else if (memcmp(a + offset, b + offset, to_read) != 0) {
            return false;
        }
    }

    return true;
}

static double benchmark_exact_tile_bucket(size_t size, size_t chunk_size) {
    if (size == 0 || chunk_size == 0) {
        return 0.0;
    }

    unsigned char* a = malloc(size);
    unsigned char* b = malloc(size);
    if (!a || !b) {
        free(a);
        free(b);
        return 0.0;
    }

    memset(a, 0xA5, size);
    memset(b, 0xA5, size);

    size_t iterations = (32U * 1024U * 1024U) / size;
    if (iterations < 8) {
        iterations = 8;
    }
    if (iterations > 8192) {
        iterations = 8192;
    }

    for (size_t i = 0; i < 4; i++) {
        g_exact_bench_sink |= compare_exact_tile_buffers(a, b, size, chunk_size, true);
    }

    double start = monotonic_seconds();
    for (size_t i = 0; i < iterations; i++) {
        g_exact_bench_sink |= compare_exact_tile_buffers(a, b, size, chunk_size, true);
    }
    double end = monotonic_seconds();

    free(a);
    free(b);

    double elapsed = end - start;
    if (elapsed <= 0.0) {
        return 0.0;
    }

    double total_bytes = (double)size * (double)iterations;
    return total_bytes / elapsed / (1024.0 * 1024.0 * 1024.0);
}

static void populate_capabilities(DedupRuntimeCaps* caps) {
    memset(caps, 0, sizeof(*caps));

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    caps->apple_arm64 = true;
    caps->neon = true;
    caps->unified_memory = true;
    caps->dotprod = have_sysctl_u32("hw.optional.arm.FEAT_DotProd");
    caps->i8mm = have_sysctl_u32("hw.optional.arm.FEAT_I8MM");
    caps->crc32 = have_sysctl_u32("hw.optional.armv8_crc32");
    caps->pmull = have_sysctl_u32("hw.optional.arm.FEAT_PMULL");
    caps->sha3 = have_sysctl_u32("hw.optional.armv8_2_sha3");
#endif

    caps->metal_available = detect_metal_available();

    if (env_is_enabled("DEDUP_DISABLE_BENCH")) {
        return;
    }

    caps->memcmp_gib_s_4k = benchmark_memcmp_bucket(4U * 1024U);
    caps->memcmp_gib_s_64k = benchmark_memcmp_bucket(64U * 1024U);
    caps->memcmp_gib_s_1m = benchmark_memcmp_bucket(1024U * 1024U);
    caps->memcmp_gib_s_8m = benchmark_memcmp_bucket(8U * 1024U * 1024U);
    caps->exact_cpu_tiles_gib_s_1m = benchmark_exact_tile_bucket(1024U * 1024U, 1024U * 1024U);
}

const DedupRuntimeCaps* dedup_runtime_caps_get(void) {
    if (!g_runtime_caps_initialized) {
        populate_capabilities(&g_runtime_caps);
        g_runtime_caps_initialized = true;
    }

    return &g_runtime_caps;
}

void dedup_runtime_caps_reset_for_tests(void) {
    memset(&g_runtime_caps, 0, sizeof(g_runtime_caps));
    g_runtime_caps_initialized = false;
}
