// Copyright © 2023 TTKB, LLC.

#include <stdlib.h>
#include <check.h>
#include "../hamt_string.h"

Suite* hamt_suite();
Suite* hamt_string_suite();

int main() {
    SRunner* sr = srunner_create(NULL);
    srunner_add_suite(sr, hamt_suite());
    srunner_add_suite(sr, hamt_string_suite());

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_VERBOSE);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
