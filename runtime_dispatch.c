// Copyright © 2026 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#include "runtime_dispatch.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "runtime_caps.h"
#include "signature.h"

static DedupRuntimeDispatch g_runtime_dispatch;
static bool g_runtime_dispatch_initialized = false;

static uint64_t fast_hash_xxhash_backend(const void* data, size_t len) {
    return signature_fast_hash_bytes(data, len);
}

static bool witness_none_backend(const char* a_path, const char* b_path, uint64_t size) {
    (void)a_path;
    (void)b_path;
    (void)size;
    return true;
}

static bool read_exact(int fd, void* buf, size_t len, off_t offset) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, (unsigned char*)buf + done, len - done, offset + (off_t)done);
        if (n <= 0) {
            return false;
        }
        done += (size_t)n;
    }
    return true;
}

static bool compare_window_hashes(int a_fd, int b_fd, off_t offset, size_t window_size) {
    unsigned char a_buf[4096];
    unsigned char b_buf[4096];

    if (window_size > sizeof(a_buf)) {
        return false;
    }
    if (!read_exact(a_fd, a_buf, window_size, offset) || !read_exact(b_fd, b_buf, window_size, offset)) {
        return false;
    }

    return signature_fast_hash_bytes(a_buf, window_size) ==
           signature_fast_hash_bytes(b_buf, window_size);
}

static bool compare_sample32(int a_fd, int b_fd, off_t offset, uint64_t size) {
    unsigned char a_raw[sizeof(int32_t)] = {0};
    unsigned char b_raw[sizeof(int32_t)] = {0};
    size_t remaining = (size_t)(size - (uint64_t)offset);
    size_t to_read = remaining < sizeof(a_raw) ? remaining : sizeof(a_raw);

    if (!read_exact(a_fd, a_raw, to_read, offset) || !read_exact(b_fd, b_raw, to_read, offset)) {
        return false;
    }

    return memcmp(a_raw, b_raw, sizeof(a_raw)) == 0;
}

static bool witness_cpu_backend(const char* a_path, const char* b_path, uint64_t size) {
    if (!a_path || !b_path) {
        return false;
    }
    if (size == 0) {
        return true;
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

    bool matches = true;
    size_t window_size = size < 4096U ? (size_t)size : 4096U;
    off_t offsets[3] = {
        0,
        (off_t)((size > window_size) ? ((size / 2U) - (window_size / 2U)) : 0),
        (off_t)((size > window_size) ? (size - window_size) : 0),
    };

    for (size_t i = 0; i < 3; i++) {
        if (!compare_window_hashes(a_fd, b_fd, offsets[i], window_size)) {
            matches = false;
            goto cleanup;
        }
    }

    off_t sample_positions[4] = {
        0,
        (off_t)(size / 3U),
        (off_t)((size * 2U) / 3U),
        (off_t)(size > 4U ? size - 4U : 0),
    };
    for (size_t i = 0; i < 4; i++) {
        if (!compare_sample32(a_fd, b_fd, sample_positions[i], size)) {
            matches = false;
            goto cleanup;
        }
    }

cleanup:
    close(a_fd);
    close(b_fd);
    return matches;
}

static bool exact_compare_memcmp_backend(const char* a_path, const char* b_path) {
    return files_match_exact_memcmp(a_path, b_path);
}

static bool exact_compare_cpu_xor_or_backend(const char* a_path, const char* b_path) {
    return files_match_exact_xor_or(a_path, b_path);
}

static bool exact_compare_cpu_tiles_backend(const char* a_path, const char* b_path) {
    return files_match_exact_cpu_tiles(a_path, b_path);
}

static bool exact_compare_gpu_exact_stream_backend(const char* a_path, const char* b_path) {
    return files_match_exact_memcmp(a_path, b_path);
}

static const char* pick_name_or_default(const char* value, const char* fallback,
                                        const char* const* allowed, size_t allowed_count) {
    if (!value || value[0] == '\0') {
        return fallback;
    }

    for (size_t i = 0; i < allowed_count; i++) {
        if (strcmp(value, allowed[i]) == 0) {
            return allowed[i];
        }
    }

    if (strcmp(value, "gpu_stream") == 0) {
        return "gpu_exact_stream";
    }

    return fallback;
}

static bool env_is_enabled(const char* env_name) {
    const char* raw = getenv(env_name);
    return raw && strcmp(raw, "1") == 0;
}

static size_t parse_size_override(const char* env_name, size_t fallback) {
    const char* raw = getenv(env_name);
    if (!raw || raw[0] == '\0') {
        return fallback;
    }

    char* end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0' || parsed == 0) {
        return fallback;
    }

    return (size_t)parsed;
}

static dedup_fast_hash_fn fast_hash_backend_for_name(const char* name) {
    if (strcmp(name, "xxhash") == 0) {
        return fast_hash_xxhash_backend;
    }
    return fast_hash_xxhash_backend;
}

static dedup_pair_witness_fn witness_backend_for_name(const char* name) {
    if (strcmp(name, "cpu_witness") == 0) {
        return witness_cpu_backend;
    }
    return witness_none_backend;
}

static dedup_exact_compare_fn exact_backend_for_name(const char* name) {
    if (strcmp(name, "cpu_xor_or") == 0) {
        return exact_compare_cpu_xor_or_backend;
    }
    if (strcmp(name, "cpu_tiles") == 0) {
        return exact_compare_cpu_tiles_backend;
    }
    if (strcmp(name, "gpu_exact_stream") == 0) {
        return exact_compare_gpu_exact_stream_backend;
    }
    return exact_compare_memcmp_backend;
}

const DedupRuntimeDispatch* dedup_runtime_dispatch_get(void) {
    if (!g_runtime_dispatch_initialized) {
        static const char* const fast_hash_names[] = { "xxhash", "rapidhash", "komihash", "blake3" };
        static const char* const strong_hash_names[] = { "none", "blake3", "sha3", "pmull_poly" };
        static const char* const witness_names[] = { "none", "cpu_witness", "gpu_witness_stream" };
        static const char* const exact_names[] = { "memcmp", "cpu_xor_or", "cpu_tiles", "gpu_exact_stream" };
        const DedupRuntimeCaps* caps = dedup_runtime_caps_get();

        memset(&g_runtime_dispatch, 0, sizeof(g_runtime_dispatch));

        const char* fast_hash_name = pick_name_or_default(getenv("DEDUP_FORCE_FAST_HASH"),
                                                          "xxhash",
                                                          fast_hash_names,
                                                          sizeof(fast_hash_names) / sizeof(fast_hash_names[0]));
        if (strcmp(fast_hash_name, "xxhash") != 0) {
            fast_hash_name = "xxhash";
        }
        g_runtime_dispatch.fast_hash_name = fast_hash_name;

        const char* strong_hash_name = pick_name_or_default(getenv("DEDUP_FORCE_STRONG_HASH"),
                                                            "none",
                                                            strong_hash_names,
                                                            sizeof(strong_hash_names) / sizeof(strong_hash_names[0]));
        if (strcmp(strong_hash_name, "none") != 0) {
            strong_hash_name = "none";
        }
        g_runtime_dispatch.strong_hash_name = strong_hash_name;

        const char* witness_name = pick_name_or_default(getenv("DEDUP_FORCE_WITNESS"),
                                                        "none",
                                                        witness_names,
                                                        sizeof(witness_names) / sizeof(witness_names[0]));
        if (strcmp(witness_name, "gpu_witness_stream") == 0 && !caps->metal_available) {
            witness_name = "none";
        }
        if (strcmp(witness_name, "none") != 0 && strcmp(witness_name, "cpu_witness") != 0) {
            witness_name = "none";
        }
        g_runtime_dispatch.witness_name = witness_name;

        const bool cpu_tiles_wins = caps->apple_arm64 && caps->exact_cpu_tiles_gib_s_1m > 0.0 &&
                                    caps->exact_cpu_tiles_gib_s_1m >= caps->memcmp_gib_s_1m;
        const char* exact_small_name = "memcmp";
        const char* exact_large_name = cpu_tiles_wins ? "cpu_tiles" : "memcmp";
        const char* forced_exact_name = pick_name_or_default(getenv("DEDUP_FORCE_EXACT_COMPARE"),
                                                             NULL,
                                                             exact_names,
                                                             sizeof(exact_names) / sizeof(exact_names[0]));
        if (forced_exact_name) {
            exact_small_name = forced_exact_name;
            exact_large_name = forced_exact_name;
        }
        if (strcmp(exact_large_name, "gpu_exact_stream") == 0 && !caps->metal_available) {
            exact_small_name = "memcmp";
            exact_large_name = "memcmp";
        }
        g_runtime_dispatch.exact_small_name = exact_small_name;
        g_runtime_dispatch.exact_large_name = exact_large_name;

        g_runtime_dispatch.fast_hash = fast_hash_backend_for_name(g_runtime_dispatch.fast_hash_name);
        g_runtime_dispatch.strong_hash = NULL;
        g_runtime_dispatch.witness = witness_backend_for_name(g_runtime_dispatch.witness_name);
        g_runtime_dispatch.exact_small = exact_backend_for_name(g_runtime_dispatch.exact_small_name);
        g_runtime_dispatch.exact_large = exact_backend_for_name(g_runtime_dispatch.exact_large_name);

        g_runtime_dispatch.witness_threshold = parse_size_override("DEDUP_WITNESS_THRESHOLD_BYTES", 256U * 1024U);
        g_runtime_dispatch.exact_large_threshold = parse_size_override("DEDUP_EXACT_LARGE_THRESHOLD_BYTES",
                                                                       cpu_tiles_wins ? (1024U * 1024U) : (64U * 1024U));
        g_runtime_dispatch.gpu_batch_threshold = parse_size_override("DEDUP_GPU_BATCH_THRESHOLD", 16U);

        if (env_is_enabled("DEDUP_FORCE_GPU") && caps->metal_available &&
            strcmp(g_runtime_dispatch.witness_name, "none") == 0) {
            g_runtime_dispatch.witness_name = "gpu_witness_stream";
        }

        g_runtime_dispatch_initialized = true;
    }

    return &g_runtime_dispatch;
}

bool dedup_runtime_witness_compare(const char* a_path, const char* b_path, uint64_t size) {
    const DedupRuntimeDispatch* dispatch = dedup_runtime_dispatch_get();

    if (!dispatch || !a_path || !b_path) {
        return false;
    }

    if (strcmp(dispatch->witness_name, "none") == 0) {
        return true;
    }

    if (size < dispatch->witness_threshold) {
        return true;
    }

    return dispatch->witness(a_path, b_path, size);
}

bool dedup_runtime_exact_compare(const char* a_path, const char* b_path, uint64_t size) {
    const DedupRuntimeDispatch* dispatch = dedup_runtime_dispatch_get();
    if (!dispatch || !a_path || !b_path) {
        return false;
    }

    if (size >= dispatch->exact_large_threshold) {
        return dispatch->exact_large(a_path, b_path);
    }
    return dispatch->exact_small(a_path, b_path);
}

void dedup_runtime_dispatch_reset_for_tests(void) {
    memset(&g_runtime_dispatch, 0, sizeof(g_runtime_dispatch));
    g_runtime_dispatch_initialized = false;
}
