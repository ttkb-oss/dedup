// Copyright © 2025 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __DEDUP_SIG_TABLE_H__
#define __DEDUP_SIG_TABLE_H__

#include "signature.h"
#include <stddef.h>

// Entry in the signature hash table
typedef struct SigTableEntry {
    FileSignature* signature;
    char* path;
    uint64_t clone_id;
    struct SigTableEntry* next;  // Collision chain
} SigTableEntry;

// Signature-based hash table for fast duplicate detection
typedef struct SigTable {
    SigTableEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
} SigTable;

// Create a new signature table
SigTable* new_sig_table(size_t bucket_count);

// Free signature table
void free_sig_table(SigTable* table);

// Insert or find matching signature
// Returns:
//   - Pointer to existing entry if match found (caller still owns sig)
//   - NULL if successfully inserted new entry (table owns sig)
//   - NULL if insertion failed (caller still owns sig, check table size)
SigTableEntry* sig_table_insert(SigTable* table, FileSignature* sig, const char* path, uint64_t clone_id);

// Check if clone_id already seen
bool sig_table_has_clone_id(const SigTable* table, uint64_t clone_id);

// Get statistics
size_t sig_table_size(const SigTable* table);
size_t sig_table_collisions(const SigTable* table);

#endif // __DEDUP_SIG_TABLE_H__
