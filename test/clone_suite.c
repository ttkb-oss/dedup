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

#include <check.h>
#include <errno.h>

#include "../clone.h"

START_TEST(clone_path_to_long) {
    char dest[PATH_MAX + 10] = { 0 };
    memset(dest, 'x', PATH_MAX + 9);

    int r = replace_with_clone("/tmp/does-not-exist", dest);
    ck_assert_int_eq(r, ENAMETOOLONG);

    dest[PATH_MAX + 4] = '/';
    r = replace_with_clone("/tmp/does-not-exist", dest);
    ck_assert_int_eq(r, ENAMETOOLONG);
    dest[PATH_MAX + 4] = 'x';

    dest[PATH_MAX / 2] = '/';
    r = replace_with_clone("/tmp/does-not-exist", dest);
    ck_assert_int_eq(r, ENAMETOOLONG);
} END_TEST

START_TEST(clone_bad_src) {
    int r = replace_with_clone("/tmp/does-not-exist", "test-data/also-does-not-exist");
    ck_assert_int_eq(r, -1);
} END_TEST

START_TEST(clone_bad_dst) {
    int r = replace_with_clone("test-data/clone-dst-acls/bar",
                               "test-data/clone-dst-acls/bar3");
    ck_assert_int_eq(r, -1);
} END_TEST

START_TEST(clone_cannot_replace) {
    int r = replace_with_clone("test-data/clone-dst-acls/bar",
                               "test-data/clone-dst-acls/bar4");
    ck_assert_int_eq(r, 2);
} END_TEST

char* tmp_name(const char* restrict path, char* restrict out, size_t size);

START_TEST(clone_tmp_name) {
    char path[PATH_MAX] = { 0 };

    char* result = tmp_name("foo.bar", path, PATH_MAX);
    ck_assert_ptr_eq(path, result);
    ck_assert_str_eq("./.~.foo.bar", result);
    // memset(path, '\0', PATH_MAX);

    result = tmp_name("foo/bar.baz", path, PATH_MAX);
    ck_assert_ptr_eq(path, result);
    ck_assert_str_eq("foo/.~.bar.baz", result);
    // memset(path, '\0', PATH_MAX);

    result = tmp_name("/foo/bar/baz.car", path, PATH_MAX);
    ck_assert_ptr_eq(path, result);
    ck_assert_str_eq("/foo/bar/.~.baz.car", result);
    // memset(path, '\0', PATH_MAX);
} END_TEST

Suite* clone_suite() {
    TCase* tc = tcase_create("clone");
    tcase_add_test(tc, clone_path_to_long);
    tcase_add_test(tc, clone_bad_src);
    tcase_add_test(tc, clone_bad_dst);
    tcase_add_test(tc, clone_cannot_replace);
    tcase_add_test(tc, clone_tmp_name);

    Suite* s = suite_create("clone");
    suite_add_tcase(s, tc);

    return s;
}
