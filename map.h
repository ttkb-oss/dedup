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

#ifndef __DEDUP_MAP_H__
#define __DEDUP_MAP_H__

#include <sys/attr.h>
#include <sys/rbtree.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "alist.h"
#include "attr.h"

typedef struct FileMetadata {
    dev_t device;
    ino_t inode;
    nlink_t nlink;
    uint32_t flags;
    uint64_t clone_id;
    size_t size;
    char* path;
    uint8_t sha256[32];
    char first;
    char last;
} FileMetadata;

void free_metadata(FileMetadata* fm);
FileMetadata* metadata_dup(FileMetadata* fm) ATTR_MALLOC(free_metadata, 1);

/// Visited Tree
///
/// The tree of visited file metadata is constructed in a way
/// to find the uniqueness of a file as quickly as possible
/// before falling back to a more thorough, but cacheable,
/// method.
///
/// Clones created by `clonefile(2)` are restricted to the same
/// filesystem, so that is used as the first layer of the tree.
/// Typically `dedup` will be run on a single filesystem, but
/// because that cannot be gauranteed, files from different
/// filesystems are evaluated separately.
///
/// The next level of the tree compares file sizes. Files with
/// differing sizes cannot be identical, the file size is provided
/// by `stat(2)` data available early on during file traversal.
///
/// The next two layers are a heuristic for file formats that might
/// be written in fixed blocks. The first character and last
/// character of the file are compared.
///
/// At this point in the tree the file metadata is stashed until
/// another file with the same device, size, first and last
/// character is found. When that occurs, a SHA-256 hash is
/// computed for both files. If they are the same, the tree does
/// not change. If they are different, a new layer is added to
/// the tree based on the hash.
///
/// device ->
///   size ->
///     first_char ->
///       last_char ->
///         sha256 ->
///           FileMetadata
///
/// The visited tree is currently implemented with `rbtree(3)`
/// but may benefit in both time and space from being
/// implemented as a hash table instead. `rbtree(3)` was chosen
/// to reduce development time, not for any ideological reason.
/// A performance stress test should be written to verify any
/// changes to this structure.

typedef struct FileMetadataNode {
    rb_node_t node;
    FileMetadata fm;
} FileMetadataNode;

typedef struct CharNode {
    rb_node_t node;
    rb_tree_t children; // depending on the level, either another CharNode tree or a FileMeatadata tree
    // pre-hash check
    FileMetadata* fm;
    char c;
} CharNode;

typedef struct SizeNode {
    rb_node_t node;
    rb_tree_t children;
    size_t s;
} SizeNode;

typedef struct DeviceNode {
    rb_node_t node;
    rb_tree_t children;
    dev_t d;
} DeviceNode;

FileMetadataNode* new_node(FileMetadata fm) __attribute__((malloc));
rb_tree_t* new_visited_tree() ATTR_MALLOC(free_visited_tree, 1);
FileMetadata* visited_tree_insert(rb_tree_t* tree, FileMetadata* fm);
size_t visited_tree_count(rb_tree_t* dup_tree) __attribute__((pure));
void free_visited_tree(rb_tree_t* t);

/// Duplicate Tree
///
/// The duplicate tree is used keep track of files with matching
/// device, size, first char, last char, and SHA-256 hash.
/// This tree is eventually used to perform the deduplication
/// operation.

typedef struct SHA256ListNode {
    rb_node_t node;
    uint8_t sha256[32];
    AList* list;
} SHA256ListNode;

rb_tree_t* new_duplicate_tree();
AList* duplicate_tree_find(rb_tree_t* tree, FileMetadata* fm);
size_t duplicate_tree_count(rb_tree_t* vis_tree);
void free_duplicate_tree(rb_tree_t* t);

//
// ID Tree (inodes, clone_id, etc.)
//

typedef struct IDCountNode {
    rb_node_t node;
    FileMetadata* fm;
    uint64_t id;
    size_t count;
} IDCountNode;

rb_tree_t* new_clone_id_counts() ATTR_MALLOC(free_clone_id_counts, 1);
size_t clone_id_tree_increment(rb_tree_t* tree, FileMetadata* fm);
FileMetadata* clone_id_tree_max(rb_tree_t* tree) __attribute__((pure));
void free_clone_id_counts(rb_tree_t* tree);

#endif // __DEDUP_MAP_H__
