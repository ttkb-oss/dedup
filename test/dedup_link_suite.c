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

START_TEST(dedup_link_empty) {
    char* output = run("../dedup -l test-data/link/empty");
    ck_assert_str_eq("duplicates found: 0\nbytes saved: 0\nalready saved: 0\n", output);
    free(output);

    struct stat e1 = { 0 }, e2 = { 0 };
    stat("test-data/link/empty/empty", &e1);
    stat("test-data/link/empty/empty2", &e2);

    ck_assert_int_ne(e1.st_ino, e2.st_ino);
    ck_assert_int_eq(e1.st_dev, e2.st_dev);
    ck_assert_int_eq(0, e1.st_size);
    ck_assert_int_eq(0, e2.st_size);
} END_TEST

void link_check_bars() {
    uint64_t bcid = get_clone_id("test-data/link/bars/bar");

    char* output = run("../dedup -l test-data/link/bars");
    free(output);

    struct stat b1, b2, b3, b4, b5;
    stat("test-data/link/bars/bar", &b1);
    stat("test-data/link/bars/bar2", &b2);
    stat("test-data/link/bars/bar3", &b3);
    stat("test-data/link/bars/bar4", &b4);
    stat("test-data/link/bars/bar5", &b5);

    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);

    uint64_t bcid1 = get_clone_id("test-data/link/bars/bar"),
             bcid2 = get_clone_id("test-data/link/bars/bar2"),
             bcid3 = get_clone_id("test-data/link/bars/bar3"),
             bcid4 = get_clone_id("test-data/link/bars/bar4"),
             bcid5 = get_clone_id("test-data/link/bars/bar5");
    ck_assert_uint_eq(bcid, bcid1); // the clone origin should be "bar"
    ck_assert_uint_eq(bcid, bcid2);
    ck_assert_uint_eq(bcid, bcid3);
    ck_assert_uint_eq(bcid, bcid4);
    ck_assert_uint_eq(bcid, bcid5);
}

START_TEST(dedup_link_hardlinks) {
    link_check_bars();
    link_check_bars(); // assert again
} END_TEST

START_TEST(dedup_link_devices) {
    int r = system("../dedup -l test-data/link/devices");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/link/devices/fifo", &f);
    stat("test-data/link/devices/empty", &e);

    ck_assert_uint_ne(get_clone_id("test-data/link/devices/fifo"),
                      get_clone_id("test-data/link/devices/empty"));
} END_TEST

START_TEST(dedup_link_big) {
    int r = system("../dedup -l test-data/link/big");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/link/big/big", &f);
    stat("test-data/link/big/big2", &e);

    ck_assert_uint_ne(get_clone_id("test-data/link/big/big"),
                      get_clone_id("test-data/link/big/big2"));
} END_TEST

START_TEST(dedup_link_same_size) {
    int r = system("../dedup -l -t0 test-data/link/same-size");
    ck_assert_int_eq(0, r);

    ck_assert_uint_eq(get_clone_id("test-data/link/same-size/big"),
                      get_clone_id("test-data/link/same-size/big2"));
} END_TEST

START_TEST(dedup_link_same_first_last) {
    int r = system("../dedup -l test-data/link/same-first-last");
    ck_assert_int_eq(0, r);

    ck_assert_uint_ne(get_clone_id("test-data/link/same-first-last/same-1"),
                      get_clone_id("test-data/link/same-first-last/same-2"));
} END_TEST

START_TEST(dedup_link_flags_acls) {
    struct stat b1, b3;
    stat("test-data/link/flags-acls/bar3", &b3);
    ck_assert_int_eq(0642 | S_IFREG, b3.st_mode);

    int r = system("../dedup -l test-data/link/flags-acls");
    ck_assert_int_eq(0, r);

    stat("test-data/link/flags-acls/bar", &b1);
    stat("test-data/link/flags-acls/bar3", &b3);

    // unlink the clone version, the link version's mode will be set
    ck_assert_int_eq(0644 | S_IFREG, b3.st_mode);
    ck_assert_uint_eq(get_clone_id("test-data/link/flags-acls/bar"),
                      get_clone_id("test-data/link/flags-acls/bar3"));

    // the ACL will no longer exist
    acl_t acl = acl_get_file("test-data/link/flags-acls/bar3", ACL_TYPE_EXTENDED);
    ck_assert_ptr_null(acl);
} END_TEST

START_TEST(dedup_link_hfs) {
#define HFS_MOUNT_PREFIX "/Volumes/dedup-test-hfs-link"
    uint64_t bcid = get_clone_id(HFS_MOUNT_PREFIX "/bar");
    char* output = run("../dedup -l -Phx " HFS_MOUNT_PREFIX " 2>&1");
    free(output);

    // this test should work identically to link_check_bars
    struct stat b1, b2, b3, b4, b5;
    stat(HFS_MOUNT_PREFIX "/bar", &b1);
    stat(HFS_MOUNT_PREFIX "/bar2", &b2);
    stat(HFS_MOUNT_PREFIX "/bar3", &b3);
    stat(HFS_MOUNT_PREFIX "/bar4", &b4);
    stat(HFS_MOUNT_PREFIX "/bar5", &b5);

    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b2.st_ino);

    uint64_t bcid1 = get_clone_id(HFS_MOUNT_PREFIX "/bar"),
             bcid2 = get_clone_id(HFS_MOUNT_PREFIX "/bar2"),
             bcid3 = get_clone_id(HFS_MOUNT_PREFIX "/bar3"),
             bcid4 = get_clone_id(HFS_MOUNT_PREFIX "/bar4"),
             bcid5 = get_clone_id(HFS_MOUNT_PREFIX "/bar5");
    ck_assert_uint_eq(bcid, bcid1); // the clone origin should be "bar"
    ck_assert_uint_eq(bcid, bcid2);
    ck_assert_uint_eq(bcid, bcid3);
    ck_assert_uint_eq(bcid, bcid4);
    ck_assert_uint_eq(bcid, bcid5);
} END_TEST

START_TEST(dedup_link_does_not_exist) {
    int r = system("../dedup -l '' ''");
    ck_assert_int_eq(1, WEXITSTATUS(r));
} END_TEST

START_TEST(dedup_link_dry_run) {
    int r = system("../dedup -l -nP /Volumes/dedup-test-hfs-link test-data/link/bars");
    ck_assert_int_eq(0, WEXITSTATUS(r));
} END_TEST

Suite* dedup_link_suite() {
    TCase* tc = tcase_create("dedup_link");
    tcase_add_test(tc, dedup_link_empty);
    tcase_add_test(tc, dedup_link_hardlinks);
    tcase_add_test(tc, dedup_link_devices);
    tcase_add_test(tc, dedup_link_big);
    tcase_add_test(tc, dedup_link_same_size);
    tcase_add_test(tc, dedup_link_same_first_last);
    tcase_add_test(tc, dedup_link_flags_acls);
    tcase_add_test(tc, dedup_link_hfs);
    tcase_add_test(tc, dedup_link_does_not_exist);
    tcase_add_test(tc, dedup_link_dry_run);

    Suite* s = suite_create("dedup_link");
    suite_add_tcase(s, tc);

    return s;
}
