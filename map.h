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

void indent(const int level);
char* inode_path(const FileMetadata* fm, char dst[PATH_MAX]);
void free_metadata(FileMetadata* fm);
FileMetadata* metadata_dup(FileMetadata* fm) __attribute__((const));
void print_file_metadata(const FileMetadata* fm, const int indent) __attribute__((const));


//
// Visited Tree
//

// device -> size -> first_char -> last_char -> sha256 -> FileMetadata

typedef struct FileMetadataNode {
    rb_node_t node;
    FileMetadata fm;
} FileMetadataNode;

typedef struct CharNode {
    rb_node_t node;
    rb_tree_t children; // depending on the level, either another CharNode tree or a FileMeatadata tree
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

FileMetadataNode* new_node(FileMetadata fm) __attribute__((const));

rb_tree_t* new_visited_tree() __attribute__((const));
FileMetadataNode* visited_tree_insert(rb_tree_t* tree, FileMetadata* fm);
size_t visited_tree_count(rb_tree_t* dup_tree) __attribute__((const));
void free_visited_tree(rb_tree_t* t);

//
// Duplicate Tree
//

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

rb_tree_t* new_clone_id_counts() __attribute__((const));
size_t clone_id_tree_increment(rb_tree_t* tree, FileMetadata* fm);
FileMetadata* clone_id_tree_max(rb_tree_t* tree) __attribute__((const));
void free_clone_id_counts(rb_tree_t* tree);

#endif // __DEDUP_MAP_H__
