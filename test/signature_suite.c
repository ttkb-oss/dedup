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

#include "../signature.h"
#include "test_utils.h"

bool files_match_exact_xor_or(const char* a_path, const char* b_path);
bool files_match_exact_cpu_tiles(const char* a_path, const char* b_path);

static void write_bytes(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    ck_assert_ptr_nonnull(f);
    ck_assert_uint_eq(size, fwrite(data, 1, size, f));
    ck_assert_int_eq(0, fclose(f));
}

static char* make_temp_dir(const char* suffix) {
    size_t len = strlen("/tmp/dedup-signature--XXXXXX") + strlen(suffix) + 1;
    char* dir = calloc(len, 1);
    ck_assert_ptr_nonnull(dir);
    snprintf(dir, len, "/tmp/dedup-signature-%s-XXXXXX", suffix);
    ck_assert_ptr_nonnull(mkdtemp(dir));
    return dir;
}

START_TEST(signature_supports_subword_files) {
    char* dir = make_temp_dir("small");
    char path[PATH_MAX] = {0};
    snprintf(path, sizeof(path), "%s/a", dir);
    write_bytes(path, "A", 1);

    struct stat st = {0};
    ck_assert_int_eq(0, stat(path, &st));

    FileSignature* sig = compute_signature(path, st.st_dev, (uint64_t)st.st_size);
    ck_assert_ptr_nonnull(sig);
    free_signature(sig);

    ck_assert_int_eq(0, unlink(path));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

START_TEST(dedup_detects_small_duplicate_files) {
    char* dir = make_temp_dir("small-dedup");
    char a[PATH_MAX] = {0}, b[PATH_MAX] = {0}, cmd[PATH_MAX * 2] = {0};
    snprintf(a, sizeof(a), "%s/a", dir);
    snprintf(b, sizeof(b), "%s/b", dir);
    write_bytes(a, "A", 1);
    write_bytes(b, "A", 1);

    snprintf(cmd, sizeof(cmd), "../dedup -nP %s", dir);
    char* output = run(cmd);
    ck_assert_ptr_nonnull(strstr(output, "duplicates found: 1\n"));
    ck_assert_ptr_nonnull(strstr(output, "bytes saved: 1 bytes\n"));
    free(output);

    ck_assert_int_eq(0, unlink(a));
    ck_assert_int_eq(0, unlink(b));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

START_TEST(dedup_rejects_sample_only_signature_collisions) {
    char* dir = make_temp_dir("collision");
    char base[PATH_MAX] = {0}, variant[PATH_MAX] = {0}, cmd[PATH_MAX * 2] = {0};
    snprintf(base, sizeof(base), "%s/base.bin", dir);
    snprintf(variant, sizeof(variant), "%s/variant.bin", dir);

    const size_t size = 16384;
    unsigned char* file_a = malloc(size);
    unsigned char* file_b = malloc(size);
    ck_assert_ptr_nonnull(file_a);
    ck_assert_ptr_nonnull(file_b);

    memset(file_a, 'A', size);
    memcpy(file_b, file_a, size);
    file_b[7000] = 'B';
    file_b[12000] = 'C';

    write_bytes(base, file_a, size);
    write_bytes(variant, file_b, size);
    free(file_a);
    free(file_b);

    snprintf(cmd, sizeof(cmd), "../dedup -nP %s", dir);
    char* output = run(cmd);
    ck_assert_ptr_nonnull(strstr(output, "duplicates found: 0\n"));
    ck_assert_ptr_nonnull(strstr(output, "bytes saved: 0 bytes\n"));
    free(output);

    ck_assert_int_eq(0, unlink(base));
    ck_assert_int_eq(0, unlink(variant));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

START_TEST(files_match_exact_xor_or_handles_equal_and_different_files) {
    char* dir = make_temp_dir("xor-or");
    char a[PATH_MAX] = {0}, b[PATH_MAX] = {0};
    snprintf(a, sizeof(a), "%s/a", dir);
    snprintf(b, sizeof(b), "%s/b", dir);

    write_bytes(a, "same-data", 9);
    write_bytes(b, "same-data", 9);
    ck_assert(files_match_exact_xor_or(a, b));

    write_bytes(b, "diff-data", 9);
    ck_assert(!files_match_exact_xor_or(a, b));

    ck_assert_int_eq(0, unlink(a));
    ck_assert_int_eq(0, unlink(b));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

START_TEST(files_match_exact_cpu_tiles_handles_equal_and_different_files) {
    char* dir = make_temp_dir("cpu-tiles");
    char a[PATH_MAX] = {0}, b[PATH_MAX] = {0};
    snprintf(a, sizeof(a), "%s/a", dir);
    snprintf(b, sizeof(b), "%s/b", dir);

    write_bytes(a, "same-data", 9);
    write_bytes(b, "same-data", 9);
    ck_assert(files_match_exact_cpu_tiles(a, b));

    write_bytes(b, "diff-data", 9);
    ck_assert(!files_match_exact_cpu_tiles(a, b));

    ck_assert_int_eq(0, unlink(a));
    ck_assert_int_eq(0, unlink(b));
    ck_assert_int_eq(0, rmdir(dir));
    free(dir);
} END_TEST

Suite* signature_suite() {
    TCase* tc = tcase_create("signature");
    tcase_add_test(tc, signature_supports_subword_files);
    tcase_add_test(tc, dedup_detects_small_duplicate_files);
    tcase_add_test(tc, dedup_rejects_sample_only_signature_collisions);
    tcase_add_test(tc, files_match_exact_xor_or_handles_equal_and_different_files);
    tcase_add_test(tc, files_match_exact_cpu_tiles_handles_equal_and_different_files);

    Suite* s = suite_create("signature");
    suite_add_tcase(s, tc);
    return s;
}
