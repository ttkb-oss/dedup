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

#include <sys/attr.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#define ATTR_BITMAP_COUNT 5

uint64_t get_clone_id(const char* restrict path) {
    struct attrlist attrList = {
        .bitmapcount = ATTR_BITMAP_COUNT,
        .forkattr = ATTR_CMNEXT_CLONEID,
    };

    struct UInt64Ref {
        uint32_t length;
        uint64_t value;
    } __attribute((aligned(4), packed));
    struct UInt64Ref clone_id = { 0 };

    int err = getattrlist(path, &attrList, &clone_id, sizeof(struct UInt64Ref), FSOPT_ATTR_CMN_EXTENDED);
    if (err) {
        perror("could not getattrlist");
        return 0;
    }

    return clone_id.value;
}

int may_share_blocks(const char* restrict path) {
    struct attrlist attrList = {
        .bitmapcount = ATTR_BITMAP_COUNT,
        .forkattr = ATTR_CMNEXT_EXT_FLAGS,
    };

    struct UInt64Ref {
        uint32_t length;
        uint64_t value;
    } __attribute((aligned(4), packed));
    struct UInt64Ref clone_id = { 0 };

    int err = getattrlist(path, &attrList, &clone_id, sizeof(struct UInt64Ref), FSOPT_ATTR_CMN_EXTENDED);
    if (err) {
        perror("could not getattrlist");
        return 0;
    }

    return clone_id.value | EF_MAY_SHARE_BLOCKS;
}

size_t private_size(const char* restrict path) {
    struct attrlist attrList = {
        .bitmapcount = ATTR_BITMAP_COUNT,
        .forkattr = ATTR_CMNEXT_PRIVATESIZE,
    };

    struct UInt64Ref {
        uint32_t length;
        off_t size;
    } __attribute((aligned(4), packed));
    struct UInt64Ref size_attr = { 0 };

    int err = getattrlist(path, &attrList, &size_attr, sizeof(struct UInt64Ref), FSOPT_ATTR_CMN_EXTENDED);
    if (err) {
        perror("could not getattrlist");
        return 0;
    }

    return size_attr.size;
}

FileMetadata* metadata_from_entry(FileEntry* fe) {
    FileMetadata* fm = calloc(1, sizeof(FileMetadata));

    //
    // stat attrs
    //

    fm->device = fe->device;
    fm->inode = fe->inode;
    fm->nlink = fe->nlink;

    //
    // file size
    //

    fm->size = fe->size;

    //
    // file real path
    //

    fm->path = strdup(fe->path);

    //
    // get clone id
    //

    fm->clone_id = get_clone_id(fe->path);

    //
    // first and last characters
    //

    FILE* f = fopen(fm->path, "r");
    if (!f) {
        /*
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            clear_progress();
            fprintf(stderr, "failed to open %s\n", fm->path);
            perror("open(2)");
        });
        */
        return NULL;
    }

    int c = fgetc(f);
    if (c < 0) {
        /*
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            fprintf(stderr, "failed read first byte %s\n", fm.path);
            perror("fgetc(3)");
        });
        */
        return NULL;
    }
    fm->first = c;

    if (fseek(f, -1, SEEK_END) < 0) {
        /*
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            fprintf(stderr, "failed seek %s\n", fm.path);
            perror("fseek(3)");
        });
        */
        return NULL;
    }

    c = fgetc(f);
    if (c < 0) {
        /*
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            fprintf(stderr, "failed read last byte %s\n", fm.path);
            perror("fgetc(3)");
        });
        */
        return NULL;
    }
    fm->last = c;

    fclose(f);

    return fm;
}
