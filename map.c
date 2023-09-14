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

#include "map.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

void indent(const int level) {
    for (int l = 0; l < level; l++) {
        printf("    ");
    }
}

char* inode_path_for_dev_inode(dev_t dev, ino_t inode, char dst[PATH_MAX]) {
    size_t n = snprintf(dst,
                        PATH_MAX,
                        "/.vol/%d/%llu",
                        dev,
                        inode);

    if (n >= PATH_MAX) {
        fprintf(stderr, "path too long: %zu\n", n);
        return NULL;
    }

    return dst;

}

char* inode_path(const FileMetadata* fm, char dst[PATH_MAX]) {
    return inode_path_for_dev_inode(fm->device, fm->inode, dst);
}

char* finode_path(int fd, char dst[PATH_MAX]) {
    struct stat s = { 0 };
    int r = fstat(fd, &s);
    if (r) {
        dst[0] = '\0';
        perror("Could not fstat(2)");
        return NULL;
    }

    return inode_path_for_dev_inode(s.st_dev, s.st_ino, dst);
}

void print_sha256(const uint8_t sha256[32]) {
    for (int i = 0; i < 32; i++) {
        printf("%02x", sha256[i]);
    }
}

void print_file_metadata(const FileMetadata* fm, const int level) {

    // /.vol/16777232/80475364
    // length: 64
    // offset: 8
    // full path: /Users/jonathanhohle/Projects/Dedup/test/dedup_suite.gcda
    // size: 128
    // clone id: 0x4cbf4e40000000c
    // sha256: c60f619378145d715cd88b94b28912aafd6e6f1f9cbd9ea916ff3e4b1660bebf
    char path[PATH_MAX] = { 0 };
    inode_path(fm, path);

    indent(level);
    printf("%s\n", path);

    indent(level);
    printf("full path: %s\n", fm->path);

    indent(level);
    printf("size: %zu\n", fm->size);

    indent(level);
    printf("clone id: 0x%llx\n", fm->clone_id);

    indent(level);
    printf("first: %02x\n", fm->first);

    indent(level);
    printf("last: %02x\n", fm->last);

    indent(level);
    printf("sha256: ");
    print_sha256(fm->sha256);
    putchar('\n');
}

void free_metadata(FileMetadata* fm) {
    free(fm->path);
    free(fm);
}

FileMetadata* metadata_dup(FileMetadata* fm) {
    FileMetadata* copy = malloc(sizeof(FileMetadata));
    *copy = *fm;
    copy->path = strdup(fm->path);
    return copy;
}

#define COMPARE_INT(x, y) \
    (((x) < (y))          \
        ? -1              \
        : (((x) > (y))    \
            ? 1           \
            : 0))

signed int compare_device_node(void *context, const void *node1, const void *node2) {
    const DeviceNode* a = node1, * b = node2;
    return COMPARE_INT(a->d, b->d);
}

signed int compare_device_key(void *context, const void *node, const void *key) {
    const DeviceNode* a = node;
    const dev_t* d = key;
    return COMPARE_INT(a->d, *d);
}

static const rb_tree_ops_t DEVICE_OPS = {
    .rbto_compare_nodes = compare_device_node,
    .rbto_compare_key = compare_device_key,
    .rbto_node_offset = offsetof(DeviceNode, node),
    .rbto_context = NULL,
};

signed int compare_size_node(void *context, const void *node1, const void *node2) {
    const SizeNode* a = node1, * b = node2;
    return COMPARE_INT(a->s, b->s);
}

signed int compare_size_key(void *context, const void *node, const void *key) {
    const SizeNode* a = node;
    const size_t* s = key;
    return COMPARE_INT(a->s, *s);
}

static const rb_tree_ops_t SIZE_OPS = {
    .rbto_compare_nodes = compare_size_node,
    .rbto_compare_key = compare_size_key,
    .rbto_node_offset = offsetof(SizeNode, node),
    .rbto_context = NULL,
};

signed int compare_char_node(void *context, const void *node1, const void *node2) {
    const CharNode* a = node1, * b = node2;
    return COMPARE_INT(a->c, b->c);
}

signed int compare_char_key(void *context, const void *node, const void *key) {
    const CharNode* a = node;
    const char* c = key;
    return COMPARE_INT(a->c, *c);
}

static const rb_tree_ops_t CHAR_OPS = {
    .rbto_compare_nodes = compare_char_node,
    .rbto_compare_key = compare_char_key,
    .rbto_node_offset = offsetof(CharNode, node),
    .rbto_context = NULL,
};

signed int compare_metadata_sha256_node(void *context, const void *node1, const void *node2) {
    const FileMetadataNode* a = node1, * b = node2;
    return memcmp(a->fm.sha256, b->fm.sha256, 32);
}

signed int compare_metadata_sha256_key(void *context, const void *node, const void *key) {
    const FileMetadataNode* a = node;
    const char* sha256 = key;
    return memcmp(a->fm.sha256, sha256, 32);
}

static const rb_tree_ops_t SHA256_OPS = {
    .rbto_compare_nodes = compare_metadata_sha256_node,
    .rbto_compare_key = compare_metadata_sha256_key,
    .rbto_node_offset = offsetof(FileMetadataNode, node),
    .rbto_context = NULL,
};

rb_tree_t* new_visited_tree() {
    rb_tree_t* t = malloc(sizeof(rb_tree_t));
    rb_tree_init(t, &DEVICE_OPS);

    return t;
}

rb_tree_t* visited_tree_find_or_create_sha256_tree(rb_tree_t* tree, FileMetadata* fm) {
    DeviceNode* device_node = rb_tree_find_node(tree, &fm->device);
    if (!device_node) {
        device_node = malloc(sizeof(DeviceNode));
        device_node->d = fm->device;

        rb_tree_init(&device_node->children, &SIZE_OPS);
        rb_tree_insert_node(tree, device_node);
    }

    SizeNode* size_node = rb_tree_find_node(&device_node->children, &fm->size);
    if (!size_node) {
        size_node = malloc(sizeof(SizeNode));
        size_node->s = fm->size;

        rb_tree_init(&size_node->children, &CHAR_OPS);
        rb_tree_insert_node(&device_node->children, size_node);
    }

    CharNode* first_node = rb_tree_find_node(&size_node->children, &fm->first);
    if (!first_node) {
        first_node = malloc(sizeof(CharNode));
        first_node->c = fm->first;

        rb_tree_init(&first_node->children, &CHAR_OPS);
        rb_tree_insert_node(&size_node->children, first_node);
    }

    CharNode* last_node = rb_tree_find_node(&first_node->children, &fm->last);
    if (!last_node) {
        last_node = malloc(sizeof(CharNode));
        last_node->c = fm->last;

        rb_tree_init(&last_node->children, &SHA256_OPS);
        rb_tree_insert_node(&first_node->children, last_node);
    }

    return &last_node->children;
}

FileMetadataNode* visited_tree_insert(rb_tree_t* tree, FileMetadata* fm) {
    rb_tree_t* sha256_tree = visited_tree_find_or_create_sha256_tree(tree, fm);

    FileMetadataNode* existing = rb_tree_find_node(sha256_tree, fm->sha256);
    if (existing) {
        return existing;
    }

    FileMetadataNode* fm_node = malloc(sizeof(FileMetadataNode));
    fm_node->fm = *fm;
    rb_tree_insert_node(sha256_tree, fm_node);

    return NULL;
}

size_t visited_tree_count(rb_tree_t* dup_tree) {
    size_t count = 0;

    DeviceNode* dn = NULL;
    RB_TREE_FOREACH(dn, dup_tree) {
        // printf("device: %zu\n", dn->d);

        SizeNode* sn = NULL;
        RB_TREE_FOREACH(sn, &dn->children) {
            // indent(1);
            // printf("size: %zu\n", sn->s);

            CharNode* fn = NULL;
            RB_TREE_FOREACH(fn, &sn->children) {
                // indent(2);
                // printf("first: %02hhx\n", fn->c);

                CharNode* ln = NULL;
                RB_TREE_FOREACH(ln, &fn->children) {
                    // indent(3);
                    // printf("last: %02hhx\n", ln->c);

                    size_t n = rb_tree_count(&ln->children);
                    // indent(4);
                    // printf("count: %zu\n", n);

                    count += n;
                }
            }
        }
    }

    return count;
}

signed int compare_metadata_sha256_list_node(void *context, const void *node1, const void *node2) {
    const SHA256ListNode* a = node1, * b = node2;
    return memcmp(a->sha256, b->sha256, 32);
}

signed int compare_metadata_sha256_list_key(void *context, const void *node, const void *key) {
    const SHA256ListNode* a = node;
    const char* sha256 = key;
    return memcmp(a->sha256, sha256, 32);
}

static const rb_tree_ops_t SHA256_LIST_OPS = {
    .rbto_compare_nodes = compare_metadata_sha256_list_node,
    .rbto_compare_key = compare_metadata_sha256_list_key,
    .rbto_node_offset = offsetof(SHA256ListNode, node),
    .rbto_context = NULL,
};


rb_tree_t* new_duplicate_tree() {
    rb_tree_t* t = malloc(sizeof(rb_tree_t));
    rb_tree_init(t, &SHA256_LIST_OPS);

    return t;
}

AList* duplicate_tree_find(rb_tree_t* tree, FileMetadata* fm) {
    SHA256ListNode* list_node = rb_tree_find_node(tree, fm->sha256);
    if (!list_node) {
        list_node = malloc(sizeof(SHA256ListNode));
        memcpy(list_node->sha256, fm->sha256, 32);
        list_node->list = new_alist_with_capacity(2);
        rb_tree_insert_node(tree, list_node);
    }

    return list_node->list;
}

size_t duplicate_tree_count(rb_tree_t* vis_tree) {
    size_t count = 0;
    SHA256ListNode* node = NULL;
    RB_TREE_FOREACH(node, vis_tree) {
        count += alist_size(node->list);
    }
    return count;
}

void free_duplicate_tree(rb_tree_t* t) {
}


signed int compare_metadata_clone_id_node(void *context, const void *node1, const void *node2) {
    const IDCountNode* a = node1, * b = node2;
    return COMPARE_INT(a->id, b->id);
}

signed int compare_metadata_clone_id_key(void *context, const void *node, const void *key) {
    const IDCountNode* a = node;
    const uint64_t* id = key;
    return COMPARE_INT(a->id, *id);
}

static const rb_tree_ops_t ID_COUNT_OPS = {
    .rbto_compare_nodes = compare_metadata_clone_id_node,
    .rbto_compare_key = compare_metadata_clone_id_key,
    .rbto_node_offset = offsetof(IDCountNode, node),
    .rbto_context = NULL,
};

rb_tree_t* new_clone_id_counts() {
    rb_tree_t* t = malloc(sizeof(rb_tree_t));
    rb_tree_init(t, &ID_COUNT_OPS);

    return t;
}

size_t clone_id_tree_increment(rb_tree_t* tree, FileMetadata* fm) {
    IDCountNode* node = rb_tree_find_node(tree, &fm->clone_id);
    if (!node) {
        node = malloc(sizeof(IDCountNode));
        node->fm = fm;
        node->count = 0;
        node->id = fm->clone_id;
        rb_tree_insert_node(tree, node);
    }

    node->count += 1;
    return node->count;
}

FileMetadata* clone_id_tree_max(rb_tree_t* tree) {
    size_t max = 0;
    FileMetadata* fm = NULL;
    IDCountNode* node = NULL;
    RB_TREE_FOREACH(node, tree) {
        if (node->count > max) {
            fm = node->fm;
            max = node->count;
        }
    }
    return fm;
}

void free_clone_id_counts(rb_tree_t* tree) {
    IDCountNode* node = NULL;
    while ((node = RB_TREE_MIN(tree))) {
        rb_tree_remove_node(tree, node);
        free(node);
    }
    free(tree);
}
