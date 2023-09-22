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

#include <stdlib.h>
#include <string.h>

#include "queue.h"

FileEntryHead* new_file_entry_queue() {
    FileEntryHead* head = malloc(sizeof(FileEntryHead));
    STAILQ_INIT(head);

    return head;
}

void free_file_entry_queue(FileEntryHead* queue) {
    free(queue);
}

void file_entry_queue_append(FileEntryHead* queue,
                             char* path,
                             dev_t device,
                             ino_t inode,
                             nlink_t nlink,
                             uint32_t flags,
                             size_t size,
                             short level) {
    FileEntry* e = malloc(sizeof(FileEntry));
    *e = (FileEntry) {
        .path = strdup(path),
        .device = device,
        .inode = inode,
        .nlink = nlink,
        .flags = flags,
        .size = size,
        .level = level,
    };
    STAILQ_INSERT_TAIL(queue, e, entries);
}

FileEntry* file_entry_next(FileEntryHead* queue) {
     if (STAILQ_EMPTY(queue)) {
         return NULL;
     }

     FileEntry* e = STAILQ_FIRST(queue);
     STAILQ_REMOVE(queue, e, FileEntry, entries);

     return e;
}

void file_entry_free(FileEntry* fe) {
    free(fe->path);
    free(fe);
}
