// Copyright © 2023 TTKB, LLC.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// SPDX-License-Identifier: BSD-2-Clause

#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/acl.h>

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../utils.h"
#include "test_utils.h"

START_TEST(dedup_empty) {
    char* output = run("../dedup test-data/clonefile/empty");
    ck_assert_str_eq("duplicates found: 0\nbytes saved: 0\nalready saved: 0\n", output);
    free(output);

    struct stat e1 = { 0 }, e2 = { 0 };
    stat("test-data/clonefile/empty/empty", &e1);
    stat("test-data/clonefile/empty/empty2", &e2);

    ck_assert_int_ne(e1.st_ino, e2.st_ino);
    ck_assert_int_eq(e1.st_dev, e2.st_dev);
    ck_assert_int_eq(0, e1.st_size);
    ck_assert_int_eq(0, e2.st_size);
} END_TEST

void check_bars() {
    uint64_t bcid = get_clone_id("test-data/clonefile/bars/bar");

    char* output = run("../dedup test-data/clonefile/bars");
    free(output);

    struct stat b1, b2, b3, b4, b5;
    stat("test-data/clonefile/bars/bar", &b1);
    stat("test-data/clonefile/bars/bar2", &b2);
    stat("test-data/clonefile/bars/bar3", &b3);
    stat("test-data/clonefile/bars/bar4", &b4);
    stat("test-data/clonefile/bars/bar5", &b5);

    ck_assert_int_ne(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    ck_assert_int_ne(b1.st_ino, b2.st_ino);
    ck_assert_int_ne(b1.st_ino, b2.st_ino);
    ck_assert_int_ne(b1.st_ino, b2.st_ino);

    uint64_t bcid1 = get_clone_id("test-data/clonefile/bars/bar"),
             bcid2 = get_clone_id("test-data/clonefile/bars/bar2"),
             bcid3 = get_clone_id("test-data/clonefile/bars/bar3"),
             bcid4 = get_clone_id("test-data/clonefile/bars/bar4"),
             bcid5 = get_clone_id("test-data/clonefile/bars/bar5");
    ck_assert_uint_eq(bcid, bcid1); // the clone origin should be "bar"
    ck_assert_uint_eq(bcid, bcid2);
    ck_assert_uint_eq(bcid, bcid3);
    ck_assert_uint_eq(bcid, bcid4);
    ck_assert_uint_eq(bcid, bcid5);
}

START_TEST(dedup_hardlinks) {
    check_bars();
    check_bars(); // assert again
} END_TEST

START_TEST(dedup_devices) {
    int r = system("../dedup test-data/clonefile/devices");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/clonefile/devices/fifo", &f);
    stat("test-data/clonefile/devices/empty", &e);

    ck_assert_uint_ne(get_clone_id("test-data/clonefile/devices/fifo"),
                      get_clone_id("test-data/clonefile/devices/empty"));
} END_TEST

START_TEST(dedup_big) {
    int r = system("../dedup test-data/clonefile/big");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/clonefile/big/big", &f);
    stat("test-data/clonefile/big/big2", &e);

    ck_assert_uint_ne(get_clone_id("test-data/clonefile/big/big"),
                      get_clone_id("test-data/clonefile/big/big2"));
} END_TEST

START_TEST(dedup_same_size) {
    int r = system("../dedup -t0 test-data/clonefile/same-size");
    ck_assert_int_eq(0, r);

    ck_assert_uint_eq(get_clone_id("test-data/clonefile/same-size/big"),
                      get_clone_id("test-data/clonefile/same-size/big2"));
} END_TEST

START_TEST(dedup_same_first_last) {
    int r = system("../dedup test-data/clonefile/same-first-last");
    ck_assert_int_eq(0, r);

    ck_assert_uint_ne(get_clone_id("test-data/clonefile/same-first-last/same-1"),
                      get_clone_id("test-data/clonefile/same-first-last/same-2"));
} END_TEST

START_TEST(dedup_flags_acls) {
    int r = system("../dedup test-data/clonefile/flags-acls");
    ck_assert_int_eq(0, r);

    struct stat b1, b3;
    stat("test-data/clonefile/flags-acls/bar", &b1);
    stat("test-data/clonefile/flags-acls/bar3", &b3);

    ck_assert_int_eq(0642 | S_IFREG, b3.st_mode);
    ck_assert_uint_eq(get_clone_id("test-data/clonefile/flags-acls/bar"),
                      get_clone_id("test-data/clonefile/flags-acls/bar3"));

    acl_t acl = acl_get_file("test-data/clonefile/flags-acls/bar3", ACL_TYPE_EXTENDED);
    ck_assert_ptr_nonnull(acl);
    acl_entry_t entry;
    acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
    ck_assert_ptr_nonnull(entry);
    char* acl_text = acl_to_text(acl, NULL);
    ck_assert_int_eq(0, strncmp("!#acl 1", acl_text, 7));
    ck_assert_int_eq(0, strncmp("nobody", acl_text + 50 , 6));
    ck_assert_int_eq(0, strncmp("deny", acl_text + 60 , 4));
    ck_assert_int_eq(0, strncmp("append", acl_text + 65 , 6));
    free(acl_text);
    acl_free(acl);
} END_TEST

START_TEST(dedup_hfs) {
    char* output = run("../dedup -Phx /Volumes/dedup-test-hfs-clonefile 2>&1");
    ck_assert_str_eq("dedup: Skipping /Volumes/dedup-test-hfs-clonefile: cloning not supported\nduplicates found: 0\nbytes saved: 0 bytes\nalready saved: 0 bytes\n", output);
    free(output);
} END_TEST

START_TEST(dedup_does_not_exist) {
    int r = system("../dedup '' ''");
    ck_assert_int_eq(1, WEXITSTATUS(r));
} END_TEST

START_TEST(dedup_negative_threads) {
    int r = system("../dedup -t -1 test-data/clonefile/bars");
    ck_assert_int_eq(1, WEXITSTATUS(r));
} END_TEST

START_TEST(dedup_help) {
    int r = system("../dedup --help");
    ck_assert_int_eq(1, WEXITSTATUS(r));
} END_TEST

START_TEST(dedup_dry_run) {
    int r = system("../dedup -nP /Volumes/dedup-test-hfs-clonefile test-data/clonefile/bars");
    ck_assert_int_eq(0, WEXITSTATUS(r));
} END_TEST

Suite* dedup_suite() {
    TCase* tc = tcase_create("dedup");
    tcase_add_test(tc, dedup_empty);
    tcase_add_test(tc, dedup_hardlinks);
    tcase_add_test(tc, dedup_devices);
    tcase_add_test(tc, dedup_big);
    tcase_add_test(tc, dedup_same_size);
    tcase_add_test(tc, dedup_same_first_last);
    tcase_add_test(tc, dedup_flags_acls);
    tcase_add_test(tc, dedup_hfs);
    tcase_add_test(tc, dedup_does_not_exist);
    tcase_add_test(tc, dedup_negative_threads);
    tcase_add_test(tc, dedup_help);
    tcase_add_test(tc, dedup_dry_run);

    Suite* s = suite_create("dedup");
    suite_add_tcase(s, tc);

    return s;
}
