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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../utils.h"
#include "test_utils.h"

START_TEST(dedup_symlink_empty) {
    char* output = run("../dedup -s test-data/symlink/empty");
    ck_assert_str_eq("duplicates found: 0\nbytes saved: 0\nalready saved: 0\n", output);
    free(output);

    struct stat e1 = { 0 }, e2 = { 0 };
    stat("test-data/symlink/empty/empty", &e1);
    stat("test-data/symlink/empty/empty2", &e2);

    ck_assert_int_ne(e1.st_ino, e2.st_ino);
    ck_assert_int_eq(e1.st_dev, e2.st_dev);
    ck_assert_int_eq(0, e1.st_size);
    ck_assert_int_eq(0, e2.st_size);
} END_TEST

void symlink_check_bars() {
    uint64_t bcid = get_clone_id("test-data/symlink/bars/bar");

    char* output = run("../dedup -s test-data/symlink/bars");
    free(output);

    struct stat b1, b2, b3, b4, b5;
    stat("test-data/symlink/bars/bar", &b1);
    stat("test-data/symlink/bars/bar2", &b2);
    stat("test-data/symlink/bars/bar3", &b3);
    stat("test-data/symlink/bars/bar4", &b4);
    stat("test-data/symlink/bars/bar5", &b5);

    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    ck_assert_int_eq(b1.st_ino, b4.st_ino);
    ck_assert_int_eq(b1.st_ino, b5.st_ino);

    uint64_t bcid1 = get_clone_id("test-data/symlink/bars/bar"),
             bcid2 = get_clone_id("test-data/symlink/bars/bar2"),
             bcid3 = get_clone_id("test-data/symlink/bars/bar3"),
             bcid4 = get_clone_id("test-data/symlink/bars/bar4"),
             bcid5 = get_clone_id("test-data/symlink/bars/bar5");
    ck_assert_uint_eq(bcid, bcid1); // the clone origin should be "bar"
    ck_assert_uint_eq(bcid, bcid2);
    ck_assert_uint_eq(bcid, bcid3);
    ck_assert_uint_eq(bcid, bcid4);
    ck_assert_uint_eq(bcid, bcid5);
}

START_TEST(dedup_symlink_hardlinks) {
    symlink_check_bars();
    symlink_check_bars(); // assert again
} END_TEST

START_TEST(dedup_symlink_devices) {
    int r = system("../dedup -s test-data/symlink/devices");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/symlink/devices/fifo", &f);
    stat("test-data/symlink/devices/empty", &e);

    ck_assert_uint_ne(get_clone_id("test-data/symlink/devices/fifo"),
                      get_clone_id("test-data/symlink/devices/empty"));
} END_TEST

START_TEST(dedup_symlink_big) {
    int r = system("../dedup -s test-data/symlink/big");
    ck_assert_int_eq(0, r);

    struct stat f, e;
    stat("test-data/symlink/big/big", &f);
    stat("test-data/symlink/big/big2", &e);

    ck_assert_uint_ne(get_clone_id("test-data/symlink/big/big"),
                      get_clone_id("test-data/symlink/big/big2"));
} END_TEST

START_TEST(dedup_symlink_same_size) {
    int r = system("../dedup -s -t0 test-data/symlink/same-size");
    ck_assert_int_eq(0, r);

    int cwd = open(".", O_RDONLY | O_DIRECTORY);
    struct stat b1, b2;
    fstatat(cwd, "test-data/symlink/same-size/big", &b1, AT_SYMLINK_NOFOLLOW);
    fstatat(cwd, "test-data/symlink/same-size/big2", &b2, AT_SYMLINK_NOFOLLOW);
    close(cwd);

    ck_assert(S_ISLNK(b2.st_mode));

    char path[PATH_MAX] = { 0 };
    size_t len = readlink("test-data/symlink/same-size/big2", path, PATH_MAX);
    path[len] = '\0';

    // TODO ck_assert_str_eq("big"

    ck_assert_uint_eq(get_clone_id("test-data/symlink/same-size/big"),
                      get_clone_id("test-data/symlink/same-size/big2"));
} END_TEST

START_TEST(dedup_symlink_same_first_last) {
    int r = system("../dedup -s test-data/symlink/same-first-last");
    ck_assert_int_eq(0, r);

    ck_assert_uint_ne(get_clone_id("test-data/symlink/same-first-last/same-1"),
                      get_clone_id("test-data/symlink/same-first-last/same-2"));
} END_TEST

START_TEST(dedup_symlink_flags_acls) {
    int r = system("../dedup -s test-data/symlink/flags-acls");
    ck_assert_int_eq(0, r);

    struct stat b1, b3;
    stat("test-data/symlink/flags-acls/bar", &b1);
    stat("test-data/symlink/flags-acls/bar3", &b3);

    // symlink reads through to target's permissions
    ck_assert_int_eq(0644 | S_IFREG, b3.st_mode);
    ck_assert_uint_eq(get_clone_id("test-data/symlink/flags-acls/bar"),
                      get_clone_id("test-data/symlink/flags-acls/bar3"));

    // the ACL is going to get nuked
    acl_t acl = acl_get_file("test-data/symlink/flags-acls/bar3", ACL_TYPE_EXTENDED);
    ck_assert_ptr_null(acl);
} END_TEST

START_TEST(dedup_symlink_hfs) {
#define HFS_MOUNT_PREFIX "/Volumes/dedup-test-hfs-symlink"
    uint64_t bcid = get_clone_id(HFS_MOUNT_PREFIX "/bar");
    char* output = run("../dedup -sPhx " HFS_MOUNT_PREFIX " 2>&1");
    free(output);

    // this test should work identically to symlink_check_bars
    struct stat b1, b2, b3, b4, b5;
    stat(HFS_MOUNT_PREFIX "/bar", &b1);
    stat(HFS_MOUNT_PREFIX "/bar2", &b2);
    stat(HFS_MOUNT_PREFIX "/bar3", &b3);
    stat(HFS_MOUNT_PREFIX "/bar4", &b4);
    stat(HFS_MOUNT_PREFIX "/bar5", &b5);

    ck_assert_int_eq(b1.st_ino, b2.st_ino);
    ck_assert_int_eq(b1.st_ino, b3.st_ino);
    // TODO: missing assertions

    uint64_t bcid1 = get_clone_id(HFS_MOUNT_PREFIX "/bar"),
             bcid2 = get_clone_id(HFS_MOUNT_PREFIX "/bar2"),
             bcid3 = get_clone_id(HFS_MOUNT_PREFIX "/bar3"),
             bcid4 = get_clone_id(HFS_MOUNT_PREFIX "/bar4"),
             bcid5 = get_clone_id(HFS_MOUNT_PREFIX "/bar5");
    ck_assert_uint_eq(bcid, bcid1); // the clone origin should be "bar"
    // symlink, no clone attribute
    ck_assert_uint_eq(0, bcid2);
    ck_assert_uint_eq(bcid, bcid3);
    // symlink, no clone attribute
    ck_assert_uint_eq(0, bcid4);
    // symlink, no clone attribute
    ck_assert_uint_eq(0, bcid5);

} END_TEST

START_TEST(dedup_symlink_dry_run) {
    int r = system("../dedup -s -nP /Volumes/dedup-test-hfs-symlink test-data/symlink/bars");
    ck_assert_int_eq(0, WEXITSTATUS(r));
} END_TEST

Suite* dedup_symlink_suite() {
    TCase* tc = tcase_create("dedup_symlink");
    tcase_add_test(tc, dedup_symlink_empty);
    tcase_add_test(tc, dedup_symlink_hardlinks);
    tcase_add_test(tc, dedup_symlink_devices);
    tcase_add_test(tc, dedup_symlink_big);
    tcase_add_test(tc, dedup_symlink_same_size);
    tcase_add_test(tc, dedup_symlink_same_first_last);
    tcase_add_test(tc, dedup_symlink_flags_acls);
    tcase_add_test(tc, dedup_symlink_hfs);
    tcase_add_test(tc, dedup_symlink_dry_run);

    Suite* s = suite_create("dedup_symlink");
    suite_add_tcase(s, tc);

    return s;
}
