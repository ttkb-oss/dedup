// Copyright © 2026 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <check.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../runtime_caps.h"
#include "../runtime_dispatch.h"
#include "runtime_dispatch_suite.h"

bool dedup_runtime_witness_compare(const char* a_path, const char* b_path, uint64_t size);
bool dedup_runtime_exact_compare(const char* a_path, const char* b_path, uint64_t size);

static void clear_runtime_env(void) {
    unsetenv("DEDUP_FORCE_FAST_HASH");
    unsetenv("DEDUP_FORCE_STRONG_HASH");
    unsetenv("DEDUP_FORCE_WITNESS");
    unsetenv("DEDUP_FORCE_EXACT_COMPARE");
    unsetenv("DEDUP_WITNESS_THRESHOLD_BYTES");
    unsetenv("DEDUP_GPU_BATCH_THRESHOLD");
    unsetenv("DEDUP_EXACT_LARGE_THRESHOLD_BYTES");
}

static void write_bytes(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    ck_assert_ptr_nonnull(f);
    ck_assert_uint_eq(size, fwrite(data, 1, size, f));
    ck_assert_int_eq(0, fclose(f));
}

static char* make_temp_dir(const char* suffix) {
    size_t len = strlen("/tmp/dedup-runtime-dispatch--XXXXXX") + strlen(suffix) + 1;
    char* dir = calloc(len, 1);
    ck_assert_ptr_nonnull(dir);
    snprintf(dir, len, "/tmp/dedup-runtime-dispatch-%s-XXXXXX", suffix);
    ck_assert_ptr_nonnull(mkdtemp(dir));
    return dir;
}

START_TEST(runtime_caps_are_cached_and_resettable) {
    clear_runtime_env();
    dedup_runtime_caps_reset_for_tests();

    const DedupRuntimeCaps* caps_a = dedup_runtime_caps_get();
    const DedupRuntimeCaps* caps_b = dedup_runtime_caps_get();

    ck_assert_ptr_nonnull(caps_a);
    ck_assert_ptr_eq(caps_a, caps_b);

    dedup_runtime_caps_reset_for_tests();
    const DedupRuntimeCaps* caps_c = dedup_runtime_caps_get();
    ck_assert_ptr_nonnull(caps_c);
} END_TEST

START_TEST(runtime_dispatch_has_expected_defaults) {
    clear_runtime_env();
    dedup_runtime_dispatch_reset_for_tests();

    const DedupRuntimeCaps* caps = dedup_runtime_caps_get();
    const DedupRuntimeDispatch* dispatch = dedup_runtime_dispatch_get();
    ck_assert_ptr_nonnull(dispatch);
    ck_assert_str_eq("xxhash", dispatch->fast_hash_name);
    ck_assert_str_eq("none", dispatch->strong_hash_name);
    ck_assert_str_eq("none", dispatch->witness_name);
    ck_assert_str_eq("memcmp", dispatch->exact_small_name);
    const bool cpu_tiles_wins = caps->apple_arm64 && caps->exact_cpu_tiles_gib_s_1m > 0.0 &&
                                 caps->exact_cpu_tiles_gib_s_1m >= caps->memcmp_gib_s_1m;
    const size_t expected_exact_large_threshold = cpu_tiles_wins ? (1024U * 1024U) : (64U * 1024U);
    ck_assert_str_eq(cpu_tiles_wins ? "cpu_tiles" : "memcmp", dispatch->exact_large_name);
    ck_assert_uint_eq(expected_exact_large_threshold, dispatch->exact_large_threshold);
    ck_assert_uint_gt(dispatch->witness_threshold, 0);
    ck_assert_uint_gt(dispatch->gpu_batch_threshold, 0);
} END_TEST

START_TEST(runtime_dispatch_honors_overrides) {
    clear_runtime_env();
    setenv("DEDUP_FORCE_WITNESS", "cpu_witness", 1);
    setenv("DEDUP_FORCE_EXACT_COMPARE", "cpu_tiles", 1);
    setenv("DEDUP_WITNESS_THRESHOLD_BYTES", "131072", 1);
    setenv("DEDUP_GPU_BATCH_THRESHOLD", "32", 1);

    dedup_runtime_dispatch_reset_for_tests();
    const DedupRuntimeDispatch* dispatch = dedup_runtime_dispatch_get();

    ck_assert_ptr_nonnull(dispatch);
    ck_assert_str_eq("cpu_witness", dispatch->witness_name);
    ck_assert_str_eq("cpu_tiles", dispatch->exact_small_name);
    ck_assert_str_eq("cpu_tiles", dispatch->exact_large_name);
    ck_assert_uint_eq(131072, dispatch->witness_threshold);
    ck_assert_uint_eq(32, dispatch->gpu_batch_threshold);

    clear_runtime_env();
    dedup_runtime_dispatch_reset_for_tests();
} END_TEST

START_TEST(runtime_exact_compare_uses_bound_backend) {
    clear_runtime_env();
    dedup_runtime_dispatch_reset_for_tests();

    char* dir = make_temp_dir("exact");
    char a[PATH_MAX] = {0}, b[PATH_MAX] = {0};
    snprintf(a, sizeof(a), "%s/a", dir);
    snprintf(b, sizeof(b), "%s/b", dir);

    write_bytes(a, "same-data", 9);
    write_bytes(b, "same-data", 9);
    ck_assert(dedup_runtime_exact_compare(a, b, 9));

    write_bytes(b, "diff-data", 9);
    ck_assert(!dedup_runtime_exact_compare(a, b, 9));

    ck_assert_int_eq(0, unlink(a));
    ck_assert_int_eq(0, unlink(b));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

START_TEST(runtime_witness_compare_defaults_to_non_rejecting) {
    clear_runtime_env();
    setenv("DEDUP_FORCE_WITNESS", "cpu_witness", 1);
    dedup_runtime_dispatch_reset_for_tests();

    char* dir = make_temp_dir("witness");
    char a[PATH_MAX] = {0}, b[PATH_MAX] = {0};
    snprintf(a, sizeof(a), "%s/a", dir);
    snprintf(b, sizeof(b), "%s/b", dir);

    write_bytes(a, "abc", 3);
    write_bytes(b, "xyz", 3);
    ck_assert(dedup_runtime_witness_compare(a, b, 3));

    ck_assert_int_eq(0, unlink(a));
    ck_assert_int_eq(0, unlink(b));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);

    clear_runtime_env();
    dedup_runtime_dispatch_reset_for_tests();
} END_TEST

Suite* runtime_dispatch_suite(void) {
    TCase* tc = tcase_create("runtime_dispatch");
    tcase_add_test(tc, runtime_caps_are_cached_and_resettable);
    tcase_add_test(tc, runtime_dispatch_has_expected_defaults);
    tcase_add_test(tc, runtime_dispatch_honors_overrides);
    tcase_add_test(tc, runtime_exact_compare_uses_bound_backend);
    tcase_add_test(tc, runtime_witness_compare_defaults_to_non_rejecting);

    Suite* s = suite_create("runtime_dispatch");
    suite_add_tcase(s, tc);
    return s;
}
