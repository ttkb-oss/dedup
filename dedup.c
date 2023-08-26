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

#include <sys/cdefs.h>
#ifndef lint
__used static char const copyright[] =
    "@(#) Copyright © 2023\n"
        "TTKB, LLC. All rights reserved.\n";
__used static char const version[] =
    "TTKB dedup 20230907";
#if 0
static char sccsid[] = "@(#)dedup.c)";
#endif
#endif // lint

#define _DARWIN_FEATURE_64_BIT_INODE

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <CommonCrypto/CommonDigest.h>
#include <fts.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "clone.h"
#include "map.h"
#include "progress.h"
#include "queue.h"

#define ATTR_BITMAP_COUNT 5

#define PROGRESS_LOCK(p, m, block) do { \
        if ((p)) { \
            pthread_mutex_lock((m)); \
            block; \
            pthread_mutex_unlock((m)); \
        } \
    } while (0)

typedef struct DedupContext {
    Progress* progress;
    FileEntryHead* queue;
    rb_tree_t* visited;
    rb_tree_t* duplicates;
    size_t found;
    size_t saved;
    size_t already_saved;
    uint8_t done;
    uint8_t thread_count;
    bool dry_run;
    uint8_t verbosity;
    pthread_mutex_t metrics_mutex;
    pthread_mutex_t progress_mutex;
    pthread_mutex_t queue_mutex;
    pthread_mutex_t visited_mutex;
    pthread_mutex_t duplicates_mutex;
    pthread_mutex_t done_mutex;
} DedupContext;

void visit_entry(FileEntry* fe, Progress* p, DedupContext* ctx) {

    FileMetadata fm = { 0 };

    //
    // stat attrs
    //

    fm.device = fe->device;
    fm.inode = fe->inode;
    fm.nlink = fe->nlink;

    //
    // file size
    //

    fm.size = fe->size;

    //
    // file real path
    //
    fm.path = strdup(fe->path);

    //
    // get clone id
    //

    struct attrlist attrList = {
        .bitmapcount = ATTR_BITMAP_COUNT,
        .forkattr = ATTR_CMNEXT_CLONEID,
    };

    struct UInt64Ref {
        uint32_t length;
        uint64_t value;
    } __attribute((aligned(4), packed));
    struct UInt64Ref clone_id = { 0 };

    int err = getattrlist(fm.path, &attrList, &clone_id, sizeof(struct UInt64Ref), FSOPT_ATTR_CMN_EXTENDED);
    if (err) {
        perror("could not getattrlist");
        return;
    }

    fm.clone_id = clone_id.value;

    int fd = open(fm.path, O_RDONLY);
    char* buffer = mmap((caddr_t) 0,
                        fm.size,
                        PROT_READ,
                        MAP_PRIVATE,
                        fd,
                        0);
    if (buffer == MAP_FAILED) {
        fprintf(stderr, "failed to mmap %s\n", fm.path);
        perror("could not mmap");
        return;
    }

    //
    // first and last characters
    //
    if (fm.size == 0) {
        fm.first = fm.last = '\0';
    } else {
        fm.first = buffer[0];
        fm.last = buffer[fm.size - 1];
    }

    //
    // calculate SHA-256
    //

    CC_SHA256_CTX c = { 0 };
    CC_SHA256_Init(&c);
    err = CC_SHA256_Update(&c, buffer, fm.size);
    if (err != 1) {
        fprintf(stderr, "error: %d\n", err);
        perror("sha256 error");
        return;
    }

    munmap(buffer, fm.size);
    close(fd);

    CC_SHA256_Final(fm.sha256, &c);

    pthread_mutex_lock(&ctx->visited_mutex);
    FileMetadataNode* old = visited_tree_insert(ctx->visited, &fm);
    pthread_mutex_unlock(&ctx->visited_mutex);

    if (old) {

        pthread_mutex_lock(&ctx->duplicates_mutex);
        AList* list = duplicate_tree_find(ctx->duplicates, &fm);
        if (alist_empty(list)) {
            alist_add(list, metadata_dup(&old->fm));
        }

        if (ctx->verbosity) {
            PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                clear_progress();
                printf("%s has %zu duplicates\n",
                       fm.path,
                       alist_size(list));
                for (size_t i = 0; i < alist_size(list); i++) {
                    printf("\t%s\n",
                           ((FileMetadata*) alist_get(list, i))->path);
                }
            });
        }

        alist_add(list, metadata_dup(&fm));
        pthread_mutex_unlock(&ctx->duplicates_mutex);

        if (fm.clone_id != old->fm.clone_id) {
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->found++;
            pthread_mutex_unlock(&ctx->metrics_mutex);
            if (ctx->verbosity > 1) {
                PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                    clear_progress();
                    printf("'%s' is duplicated by '%s' (%zu bytes) [found: %zu]\n",
                           old->fm.path,
                           fm.path,
                           fm.size,
                           ctx->found);
                });
            }
        }
    }
}

void* dedup_work(void* ctx) {
    DedupContext* c = ctx;
    uint8_t done = c->done;

    while (!done) {
        pthread_mutex_lock(&c->queue_mutex);
        FileEntry* fe = file_entry_next(c->queue);
        pthread_mutex_unlock(&c->queue_mutex);

        if (!fe) {
            pthread_mutex_lock(&c->done_mutex);
            done = c->done;
            pthread_mutex_unlock(&c->done_mutex);
            if (done) {
                break;
            }
            usleep(100);
            continue;
        }

        visit_entry(fe, c->progress, c);
        file_entry_free(fe);

        PROGRESS_LOCK(c->progress, &c->progress_mutex, {
            c->progress->completedUnitCount++;
            display_progress(c->progress);
        });

        if (c->thread_count == 0) {
            break;
        }
    }

    return NULL;
}

size_t deduplicate(AList* metadata_set, DedupContext* ctx) {
    FileMetadata* origin = NULL;
    char* reason = NULL;
    // if there is a file with more than one hard link, use that as the
    // source candidate (optimally a hardlink with the most links)
    for (size_t i = 0; i < alist_size(metadata_set); i++) {
        FileMetadata* fm = alist_get(metadata_set, i);
        if (fm->nlink > 1 && (!origin || origin->nlink < fm->nlink)) {
            origin = fm;
            reason = "most hardlinks";
        }
    }

    // otherwise, use the file with the most clones.
    if (!origin) {
        rb_tree_t* clone_counts = new_clone_id_counts();
        for (size_t i = 0; i < alist_size(metadata_set); i++) {
            FileMetadata* fm = alist_get(metadata_set, i);
            clone_id_tree_increment(clone_counts, fm);
        }
        origin = clone_id_tree_max(clone_counts);
        free_clone_id_counts(clone_counts);
        if (rb_tree_count(clone_counts) == alist_size(metadata_set)) {
            reason = "first seen";
        } else {
            reason = "most clones";
        }
    }

    printf("using %s as the clone origin (%s)\n",
           origin->path,
           reason);

    for (size_t i = 0; i < alist_size(metadata_set); i++) {
        FileMetadata* fm = alist_get(metadata_set, i);

        if (fm == origin) {
            continue;
        }

        if (fm->nlink > 1) {
            printf("\tskipping %s, hardlinked\n",
                   fm->path);
            ctx->already_saved += fm->size;
            continue;
        }

        if (fm->clone_id == origin->clone_id) {
            printf("\tskipping %s, already cloned\n",
                   fm->path);
            ctx->already_saved += fm->size;
            continue;
        }

        if (fm->flags & UF_IMMUTABLE ||
            fm->flags & SF_IMMUTABLE) {
            printf("\tskipping %s, immutable\n",
                   fm->path);
            continue;
        }

        if (ctx->dry_run) {
            printf("\tcloning to %s\n",
                   fm->path);

            ctx->saved += fm->size;
            continue;
        }

        int result = replace_with_clone(origin->path,
                                        fm->path);
        if (result) {
            perror("clone failed");
        }

        printf("\tmoved %s to use %s data saved %zu (previously saved: %zu)\n",
               fm->path,
               origin->path,
               fm->size,
               ctx->saved);
        ctx->saved += fm->size;
    }

    return 0;
}

__attribute__((noreturn))
void usage(char* pgm, DedupContext* ctx) {
    fprintf(stderr,
            "%s\nusage: %s [-I pattern] [-t n] [-PVcnvx] [-d n] [file ...]\n\n"
                "Options:\n"
                // "  --ignore, -I pattern     Exclude a pattern from being used as a clone\n"
                // "                           source or being replaced by a clone. This option\n"
                // "                           can be specified multiple times.\n"
                "  --threads, -t n          The number of threads to use for file building\n"
                "                           lookup tables and replacing clones. Default: %d\n"
                "  --dry-run, -n            Don't replace file content, just print what \n"
                "                           would have happend.\n"
                "  --one-file-system, -x    Don't evaluate directories on a different device\n"
                "                           than the starting paths.\n"
                "  --depth, -d depth        Don't descend further than the specified depth.\n"
                "  --color, -c              Enabled colored output.\n"
                "  --no-progress, -P        Do not display a progress bar.\n"
                "  --verbose, -v            Increase verbosity. May be used multiple times.\n"
                "  --version, -V            Print the version and exit\n"
                "  -h                       Human readable output.\n"
                "  --help                   Show this help.\n",
            version,
            pgm,
            ctx->thread_count);

    exit(1);
}

__attribute__((const))
int32_t cpu_count() {
    int32_t c = 0;
    size_t len = sizeof(int32_t);
    sysctlbyname("hw.ncpu", &c, &len, NULL, 0);
    return c;
}

__attribute__((const))
bool is_vol_cap_supported(char* path, int vol_cap) {
    struct VolAttrsBuf {
        u_int32_t length;
        vol_capabilities_attr_t capabilities;
        vol_attributes_attr_t attributes;
    } __attribute__((aligned(4), packed));
    struct VolAttrsBuf vol_attrs;

    struct attrlist attr_list = {
        .bitmapcount = ATTR_BIT_MAP_COUNT,
        .volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES | ATTR_VOL_ATTRIBUTES,
    };
    // get the file system's mount point path for the input path
    struct statfs stat_buf;
    int result = statfs(path, &stat_buf);

    if (result) {
        perror("Could not get volume stat");
        // TODO: exit?
        return false;
    }

    // get the supported capabilities and attributes
    result = getattrlist(stat_buf.f_mntonname,
                         &attr_list,
                         &vol_attrs,
                         sizeof(vol_attrs),
                         FSOPT_ATTR_CMN_EXTENDED);
    if (result) {
        perror("Could not retrieve volume attributes");
        // TODO: exit?
        return false;
    }

     #define VOL_CAPABILITIES_FORMAT     0
     #define VOL_CAPABILITIES_INTERFACES 1
     #define VOL_CAPABILITIES_RESERVED1  2
     #define VOL_CAPABILITIES_RESERVED2  3

    return vol_attrs.capabilities.capabilities[VOL_CAPABILITIES_INTERFACES] & vol_cap;
}

__attribute__((const))
bool is_clonefile_supported(char* path) {
    return is_vol_cap_supported(path, VOL_CAP_INT_CLONE);
}

__attribute__((unused))
__attribute__((const))
bool are_acls_supported(char* path) {
    return is_vol_cap_supported(path, VOL_CAP_INT_RENAME_SWAP);
}

void print_human_bytes(uint64_t bytes) {
    double v = bytes;
    char* unit = " bytes";

    if (v > 1000.0) {
        v /= 1000.0;
        unit = "kB";
    }
    if (v > 1000.0) {
        v /= 1000.0;
        unit = "MB";
    }
    if (v > 1000.0) {
        v /= 1000.0;
        unit = "GB";
    }
    if (v > 1000.0) {
        v /= 1000.0;
        unit = "TB";
    }

    printf("%0.f%s", v, unit);
}

int main(int argc, char* argv[]) {

    FileEntryHead* queue = new_file_entry_queue();
    Progress p = { 0 };
    uint16_t max_depth = UINT16_MAX;
    int user_fts_options = 0;

    DedupContext dc = {
        .progress = &p,
        .queue = queue,
        .visited = new_visited_tree(),
        .duplicates = new_duplicate_tree(),
        .found = 0,
        .saved = 0,
        .already_saved = 0,
        .done = 0,
        .dry_run = false,
        .verbosity = 0,
        .thread_count = cpu_count(),
        .metrics_mutex = PTHREAD_MUTEX_INITIALIZER,
        .progress_mutex = PTHREAD_MUTEX_INITIALIZER,
        .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
        .visited_mutex = PTHREAD_MUTEX_INITIALIZER,
        .duplicates_mutex = PTHREAD_MUTEX_INITIALIZER,
        .done_mutex = PTHREAD_MUTEX_INITIALIZER,
    };

    static const struct option options[] = {
        { "ignore", required_argument, NULL, 'I' },
        { "threads", required_argument, NULL, 't' },
        { "one-file-system", no_argument, NULL, 'x' },
        { "depth", required_argument, NULL, 'd' },
        { "color", optional_argument, NULL, 'c' },
        { "dry-run", no_argument, NULL, 'n' },
        { "verbose", no_argument, NULL, 'v' },
        { "version", no_argument, NULL, 'V' },
        { "no-progress", no_argument, NULL, 'P' },
        { "help", no_argument, NULL, '?' },
        { NULL, 0, NULL, 0 },
    };

    bool human_readable = false;

    int ch = -1, t;
    short d;
    while ((ch = getopt_long(argc, argv, "I:PVt:xd:c::nvh", options, NULL)) != -1) {
        switch (ch) {
            case 'I':
                fprintf(stderr, "-I is unimplemented\n");
                break;
            case 't':
                t = atoi(optarg);
                if (t < 0) {
                    fprintf(stderr,
                            "Thread count must be a positive value: %s\n",
                            optarg);
                    usage(argv[0], &dc);
                }
                dc.thread_count = t;
                break;
            case 'x':
                user_fts_options |= FTS_XDEV;
                break;
            case 'd':
                d = atoi(optarg);
                if (d < 0) {
                    fprintf(stderr, "Depth must be a positive value: %s\n",
                            optarg);
                    usage(argv[0], &dc);
                }
                max_depth = d;
                break;
            case 'c':
                fprintf(stderr, "-c is unimplemented\n");
                break;
            case 'n':
                dc.dry_run = true;
                break;
            case 'h':
                human_readable = true;
                break;
            case 'v':
                dc.verbosity++;
                break;
            case 'V':
                fprintf(stderr, "%s\n", version);
                return 1;
            case 'P':
                dc.progress = NULL;
                break;
            case '?':
            default:
                usage(argv[0], &dc);
        }
    }
    argc -= optind;
    argv += optind;

    if (!isatty(STDOUT_FILENO)) {
        dc.progress = NULL;
    }

    static const char* const DEFAULT_PATHS[] = {
        ".",
        NULL,
    };

    char** paths = (argc > 0)
        ? argv
        : (char**) DEFAULT_PATHS;

    FTS* traversal = fts_open(paths,
                              FTS_NOCHDIR | FTS_PHYSICAL | user_fts_options,
                              NULL);

    // n.b! as of Libc-1244.1.7 fts_open will only fail if it cannot
    //      allocate memory. this makes this check extremely unlikely
    //      to ever fail, but if it does fail, `traversal` will be
    //      invalid.
    // LCOV_EXCL_START
    if (!traversal) {
        perror("Could not open starting directories");
        exit(1);
    }
    // LCOV_EXCL_STOP

    pthread_t* threads = calloc(dc.thread_count, sizeof(pthread_t));
    for (int i = 0; i < dc.thread_count; i++) {
        int r = pthread_create(&threads[i], NULL, dedup_work, &dc);
        if (r) {
            fprintf(stderr,
                    "Could not create threads: error %i\nRunning single threaded.\n",
                    r);
            dc.thread_count = i;
            break;
        }
    }

    dev_t current_dev = -1;
    bool clonefile_supported = false;
    FTSENT* entry = NULL;
    while ((entry = fts_read(traversal)) != NULL) {
        if (entry->fts_errno) {
            char* e = strerror(entry->fts_errno);
            fprintf(stderr,
                    "%s: error (%d): %s\n",
                    entry->fts_path,
                    entry->fts_errno,
                    e);
            exit(1);
        }

        if (entry->fts_level > (max_depth + 1)) {
            fts_set(traversal, entry, FTS_SKIP);
            continue;
        }

        if (current_dev != entry->fts_statp->st_dev) {
            clonefile_supported = is_clonefile_supported(entry->fts_path);
            current_dev = entry->fts_statp->st_dev;

            if (!clonefile_supported) {
                fprintf(stderr, "Skipping %s: cloning not supported\n", entry->fts_path);

                // if FTS_XDEV is set, we can't accidentally cross into a
                // volume that does support clonefile, so skip everything else
                if (user_fts_options & FTS_XDEV) {
                    fts_set(traversal, entry, FTS_SKIP);
                    continue;
                }
            }
        }

        if (!clonefile_supported) {
            continue;
        }

        if (entry->fts_info == FTS_DP) {
            continue;
        }

        // the file cannot be a directory
        if (entry->fts_info == FTS_D ||
            entry->fts_info == FTS_DC ||
            entry->fts_info == FTS_DNR ||
            entry->fts_info == FTS_DOT ||
            entry->fts_info == FTS_DP) {
            continue;
        }

        // make sure named pipes (fifo), character special,
        // block special, symlinks, whiteout, etc.
        if (entry->fts_info != FTS_F) {
            continue;
        }

        // the file cannot be empty
        if (entry->fts_statp->st_size == 0) {
            continue;
        }

        // at this point we have a regular file
        // that only has one link
        PROGRESS_LOCK(dc.progress, &dc.progress_mutex, {
            dc.progress->totalUnitCount++;
            display_progress(dc.progress);
        });

        pthread_mutex_lock(&dc.queue_mutex);
        file_entry_queue_append(queue,
                                entry->fts_path,
                                entry->fts_statp->st_dev,
                                entry->fts_statp->st_ino,
                                entry->fts_statp->st_nlink,
                                entry->fts_statp->st_flags,
                                entry->fts_statp->st_size,
                                entry->fts_level);
        pthread_mutex_unlock(&dc.queue_mutex);

        if (dc.thread_count == 0) {
            dedup_work(&dc);
        }
    }
    pthread_mutex_lock(&dc.done_mutex);
    dc.done = 1;
    pthread_mutex_unlock(&dc.done_mutex);

    for (int i = 0; i < dc.thread_count; i++) {
        // clang-analyzer thinks threads[i] can be NULL, but `pthread_t`
        // is an opaque type (to us). if `pthread_create` is successful
        // the API contract says we can pass that value to pthread_join.
        // the assertion is less intrusive than suppressing the warning
        // by checking for the _clang_analyzer macro
        assert(threads[i] != NULL);
        if (pthread_join(threads[i], NULL)) {
            fprintf(stderr, "Failed to wait for thread %i\n", i);
        }
    }
    free(threads); threads = NULL;

    if (dc.progress) {
        clear_progress();
    }
    printf("duplicates found: %zu\n", dc.found);

    fts_close(traversal);

    SHA256ListNode* duplicate_set = NULL;
    RB_TREE_FOREACH(duplicate_set, dc.duplicates) {
        deduplicate(duplicate_set->list, &dc);
        for (size_t i = 0; i < alist_size(duplicate_set->list); i++) {
            FileMetadata* fm = alist_get(duplicate_set->list, i);
            free_metadata(fm);
            alist_set(duplicate_set->list, i, NULL);
        }
        free_alist(duplicate_set->list);
        duplicate_set->list = NULL;
    }

    printf("bytes saved: ");
    if (human_readable) {
        print_human_bytes(dc.saved);
    } else {
        printf("%zu", dc.saved);
    }
    putchar('\n');

    printf("already saved: ");
    if (human_readable) {
        print_human_bytes(dc.already_saved);
    } else {
        printf("%zu", dc.saved);
    }
    putchar('\n');

    return 0;
}
