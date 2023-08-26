// Copyright © 2023 TTKB, LLC.

#include <stdlib.h>
#include <stdio.h>
#include <check.h>
#include "../hamt.h"
#include "../hamt_string.h"
#include "../hamt_math.h"

START_TEST(hamt_cons_empty) {
    HAMT hamt = { 0 };
    HAMTInit(&hamt,
             HAMTStringHash,
             0,
             NULL);

    ck_assert_uint_eq(0, HAMTCount(&hamt));

    HAMTDestroy(&hamt);

    ck_assert_int_eq(0, hamt.root.bitmap);
    ck_assert_int_eq(0, hamt.root.depth);
} END_TEST

START_TEST(hamt_cons_singleton) {
    HAMT hamt = { 0 };
    // map: { foo: "bar" }
    char* values[] = {
        "foo", "bar",
    };
    HAMTInit(&hamt,
             HAMTStringHash,
             1,
             (void**) values);

    ck_assert_uint_eq(1, HAMTCount(&hamt));

    HAMTDestroy(&hamt);

    ck_assert_int_eq(0, hamt.root.bitmap);
    ck_assert_int_eq(0, hamt.root.depth);
}

START_TEST(hamt_count) {
    HAMT hamt = { 0 };

    ck_assert_uint_eq(0, HAMTCount(&hamt));
} END_TEST

START_TEST(hamt_math) {
    fprintf(stderr, "test population\n");
    uint64_t population = 0x9d;
    ck_assert_uint_eq(5, pop64(population));
    ck_assert_int_eq(0, findseg(population, 0));
    ck_assert_int_eq(0, findseg(population, 1)); // invalid
    ck_assert_int_eq(1, findseg(population, 2));
    ck_assert_int_eq(2, findseg(population, 3));
    ck_assert_int_eq(3, findseg(population, 4));
    ck_assert_int_eq(3, findseg(population, 5)); // invalid
    ck_assert_int_eq(3, findseg(population, 6)); // invalid
    ck_assert_int_eq(4, findseg(population, 7));

    fprintf(stderr, "all population\n");
    population = UINT64_MAX;
    ck_assert_uint_eq(64, pop64(population));
    for (int i = 0; i < 64; i++) {
        ck_assert_int_eq(i, findseg(population, i));
    }

    fprintf(stderr, "none population\n");
    population = 0;
    ck_assert_uint_eq(0, pop64(population));
    for (int i = 0; i < 64; i++) {
        ck_assert_int_eq(0, findseg(population, i));
    }

    fprintf(stderr, "1 in each position population\n");
    for (int i = 0; i < 64; i++) {
        population = 1ull << i;
        printf("population: %llx\n", population);
        ck_assert_uint_eq(1, pop64(population));

        // there's only 1 sement, so all should return 0 index
        for (int j = 0; j < 64; j++) {
            ck_assert_uint_eq(0, findseg(population, j));
        }
    }
} END_TEST

Suite* hamt_suite() {
    TCase* tc_cons = tcase_create("hamt");
    tcase_add_test(tc_cons, hamt_cons_empty);
    tcase_add_test(tc_cons, hamt_cons_singleton);
    tcase_add_test(tc_cons, hamt_count);
    tcase_add_test(tc_cons, hamt_math);

    Suite* s = suite_create("hamt");
    suite_add_tcase(s, tc_cons);

    return s;
}
