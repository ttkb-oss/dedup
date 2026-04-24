// Copyright © 2025 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __DEDUP_SIGNATURE_H__
#define __DEDUP_SIGNATURE_H__

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Lightweight file signature using strategic sampling
// Used as a fast candidate filter before exact byte-for-byte verification.
typedef struct FileSignature {
    dev_t device;        // Device ID (from stat)
    uint64_t size;       // File size (from stat)
    int32_t samples[4];  // Sampled int32 values at strategic positions
    uint64_t quick_hash; // xxHash64 of first 4KB (or entire file if smaller)
} FileSignature;

// Compute signature for a file
// Returns NULL on error
FileSignature* compute_signature(const char* path, dev_t device, uint64_t size);

// Free signature
void free_signature(FileSignature* sig);

// Compare two signatures as a fast candidate filter.
// Returns true if files should be considered for exact verification.
bool signatures_match(const FileSignature* a, const FileSignature* b);

// Compare full file contents byte-for-byte.
bool files_match_exact(const char* a_path, const char* b_path);

// Named exact-compare backend using memcmp on chunked reads.
bool files_match_exact_memcmp(const char* a_path, const char* b_path);

// Compare full file contents using a streaming XOR/OR reduction.
bool files_match_exact_xor_or(const char* a_path, const char* b_path);

// Compare full file contents using a larger tiled XOR/OR backend tuned for
// large Apple SoC files.
bool files_match_exact_cpu_tiles(const char* a_path, const char* b_path);

// Public fast-hash backend used by runtime dispatch.
uint64_t signature_fast_hash_bytes(const void* data, size_t len);

// Hash a signature for use in hash table
uint64_t hash_signature(const FileSignature* sig);

#endif // __DEDUP_SIGNATURE_H__
