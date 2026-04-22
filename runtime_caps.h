// Copyright © 2026 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __DEDUP_RUNTIME_CAPS_H__
#define __DEDUP_RUNTIME_CAPS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct DedupRuntimeCaps {
    bool apple_arm64;
    bool neon;
    bool dotprod;
    bool i8mm;
    bool crc32;
    bool pmull;
    bool sha3;
    bool unified_memory;
    bool metal_available;

    double memcmp_gib_s_4k;
    double memcmp_gib_s_64k;
    double memcmp_gib_s_1m;
    double memcmp_gib_s_8m;
    double exact_cpu_tiles_gib_s_1m;
} DedupRuntimeCaps;

const DedupRuntimeCaps* dedup_runtime_caps_get(void);
void dedup_runtime_caps_reset_for_tests(void);

#endif // __DEDUP_RUNTIME_CAPS_H__
