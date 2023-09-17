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

#ifndef __DEDUP_ALIST_H__
#define __DEDUP_ALIST_H__

#include <stdlib.h>
#include <stdbool.h>

#include "attr.h"

/// An `alist` is a dynamic list with similar semantics to Java's `ArrayList`,
/// but without the memory management for items in the list.
typedef struct AList {
    size_t size;
    size_t capacity;
    void** elements;
} AList;

#ifndef ALIST_DEFAULT_CAPACITY
/// When creating an `alist`, this is the default number
/// of elements that storage will be allocated to hold.
///
/// This macro can be overridden by defining the value
/// prior to including this header file.
#define ALIST_DEFAULT_CAPACITY 8
#endif

/// Deallocates storage used for an `alist`, but does not
/// free memory associated with each element. This must
/// be done by the owner externally.
void free_alist(AList* list);

/// Creates a new `alist` with the default capacity
AList* new_alist() ATTR_MALLOC(free_alist, 1);

/// Creates a new `alist` with a provided capacity
AList* new_alist_with_capacity(size_t initial);

/// Shallow copy of an `alist`
AList* alist_dup(const AList* list) ATTR_MALLOC(free_alist, 1);

/// Reduces the storage used by the `alist` to what is
/// necessary to retain the current set of elements.
void alist_trim(AList* list);

void alist_ensure(AList* list, size_t required);

/// Returns the number of elements in the `alist`.
size_t alist_size(const AList* list) __attribute__((pure));

/// Returns `true` if the `alist` has zero elements.
///
/// This is a convenience function. The following
/// lines are equivalent:
///
///     alist_empty(list)
///     alist_size(list) == 0
bool alist_empty(const AList* list) __attribute__((pure));

/// Retrieves the element at `index` from the provided
/// `alist`.
void* alist_get(const AList* list, size_t index) __attribute__((pure));

/// Appends an element to the end of the `alist`
void alist_add(AList* list, void* item);

/// Sets an `item` at the provided `index`. The
/// index
void alist_set(AList* list, size_t index, void* item);

/// Removes an item at an index
void* alist_remove(AList* list, size_t index);

#endif // __DEDUP_ALIST_H__
