// Copyright © 2026 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __DEDUP_RUNTIME_DISPATCH_H__
#define __DEDUP_RUNTIME_DISPATCH_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*dedup_fast_hash_fn)(const void* data, size_t len);
typedef bool (*dedup_pair_witness_fn)(const char* a_path, const char* b_path, uint64_t size);
typedef bool (*dedup_exact_compare_fn)(const char* a_path, const char* b_path);

typedef struct DedupRuntimeDispatch {
    const char* fast_hash_name;
    const char* strong_hash_name;
    const char* witness_name;
    const char* exact_small_name;
    const char* exact_large_name;

    dedup_fast_hash_fn fast_hash;
    dedup_fast_hash_fn strong_hash;
    dedup_pair_witness_fn witness;
    dedup_exact_compare_fn exact_small;
    dedup_exact_compare_fn exact_large;

    size_t witness_threshold;
    size_t exact_large_threshold;
    size_t gpu_batch_threshold;
} DedupRuntimeDispatch;

const DedupRuntimeDispatch* dedup_runtime_dispatch_get(void);
bool dedup_runtime_witness_compare(const char* a_path, const char* b_path, uint64_t size);
bool dedup_runtime_exact_compare(const char* a_path, const char* b_path, uint64_t size);
void dedup_runtime_dispatch_reset_for_tests(void);

#endif // __DEDUP_RUNTIME_DISPATCH_H__
