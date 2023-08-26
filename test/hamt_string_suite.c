// Copyright © 2023 TTKB, LLC.

#include <stdlib.h>
#include <check.h>
#include "../hamt_string.h"

START_TEST(hamt_string) {
    ck_assert_uint_eq(101574, HAMTStringHash("foo"));

    ck_assert_uint_eq(0, HAMTStringHash(""));
} END_TEST

Suite* hamt_string_suite() {
    TCase* tc_core = tcase_create("hamt_string");
    tcase_add_test(tc_core, hamt_string);

    Suite* s = suite_create("hamt_string");
    suite_add_tcase(s, tc_core);

    return s;
}
