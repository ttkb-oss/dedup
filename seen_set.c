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
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
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

#include "seen_set.h"

#include <stdlib.h>
#include <string.h>

#define EMPTY    0
#define OCCUPIED 1

typedef struct {
    uint64_t key;
    uint8_t  state;
} Slot;

struct SeenSet {
    Slot*  slots;
    size_t capacity; // always a power of 2
    size_t count;
};

static size_t next_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

SeenSet* new_seen_set(size_t capacity) {
    if (capacity < 16) capacity = 16;
    capacity = next_power_of_2(capacity);

    SeenSet* set = malloc(sizeof(SeenSet));
    if (!set) return NULL;

    set->slots = calloc(capacity, sizeof(Slot));
    if (!set->slots) {
        free(set);
        return NULL;
    }

    set->capacity = capacity;
    set->count = 0;
    return set;
}

void free_seen_set(SeenSet* set) {
    if (!set) return;
    free(set->slots);
    free(set);
}

// Fibonacci hashing for good distribution with power-of-2 tables
static inline size_t hash_key(uint64_t key, size_t mask) {
    return (size_t)((key * 11400714819323198485ULL) >> 32) & mask;
}

static bool seen_set_grow(SeenSet* set) {
    size_t new_cap = set->capacity * 2;
    Slot* new_slots = calloc(new_cap, sizeof(Slot));
    if (!new_slots) return false;

    size_t mask = new_cap - 1;
    for (size_t i = 0; i < set->capacity; i++) {
        if (set->slots[i].state != OCCUPIED) continue;
        uint64_t key = set->slots[i].key;
        size_t idx = hash_key(key, mask);
        while (new_slots[idx].state == OCCUPIED) {
            idx = (idx + 1) & mask;
        }
        new_slots[idx].key = key;
        new_slots[idx].state = OCCUPIED;
    }

    free(set->slots);
    set->slots = new_slots;
    set->capacity = new_cap;
    return true;
}

bool seen_set_insert(SeenSet* set, uint64_t key) {
    // Grow at 75% load
    if (set->count * 4 >= set->capacity * 3) {
        seen_set_grow(set);
    }

    size_t mask = set->capacity - 1;
    size_t idx = hash_key(key, mask);

    while (set->slots[idx].state == OCCUPIED) {
        if (set->slots[idx].key == key) {
            return true; // already present
        }
        idx = (idx + 1) & mask;
    }

    set->slots[idx].key = key;
    set->slots[idx].state = OCCUPIED;
    set->count++;
    return false; // newly inserted
}
