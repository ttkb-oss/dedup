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
#ifndef VERSION
#define VERSION "0.0.0"
#endif // VERSION
#ifndef BUILD_DATE
#define BUILD_DATE "00000000"
#endif // BUILD_DATE
__used static char const version[] =
    "TTKB dedup " VERSION " (" BUILD_DATE ")";
#if 0
static char sccsid[] = "@(#)dedup.c)";
#endif // 0
#endif // lint

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <err.h>
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
#include "utils.h"

#define PROGRESS_LOCK(p, m, block) do { \
        if ((p)) { \
            pthread_mutex_lock((m)); \
            block; \
            pthread_mutex_unlock((m)); \
        } \
    } while (0)

typedef enum ReplaceMode {
    DEDUP_CLONE    = 0,
    DEDUP_LINK     = 1,
    DEDUP_SYMLINK  = 2,
} ReplaceMode;

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
    bool force;
    ReplaceMode replace_mode;
    pthread_mutex_t metrics_mutex;
    pthread_mutex_t progress_mutex;
    pthread_mutex_t queue_mutex;
    pthread_mutex_t visited_mutex;
    pthread_mutex_t duplicates_mutex;
    pthread_mutex_t done_mutex;
} DedupContext;


void visit_entry(FileEntry* fe, Progress* p, DedupContext* ctx) {

    FileMetadata* fm = metadata_from_entry(fe);

    if (!fm) {
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            clear_progress();
            fprintf(stderr,
                    "could not populate metadata for %s\n",
                    fe->path);
            perror("metadata_from_entry");
        });
        return;
    }

    pthread_mutex_lock(&ctx->visited_mutex);
    FileMetadata* old = visited_tree_insert(ctx->visited, fm);
    old = metadata_dup(old);
    pthread_mutex_unlock(&ctx->visited_mutex);

    if (old) {
        pthread_mutex_lock(&ctx->duplicates_mutex);
        AList* list = duplicate_tree_find(ctx->duplicates, fm);
        if (alist_empty(list)) {
            alist_add(list, metadata_dup(old));
        }

        if (ctx->verbosity) {
            PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                clear_progress();
                printf("%s has %zu duplicates\n",
                       fm->path,
                       alist_size(list));
                for (size_t i = 0; i < alist_size(list); i++) {
                    printf("\t%s\n",
                           ((FileMetadata*) alist_get(list, i))->path);
                }
            });
        }

        // ownership transferred to the list
        alist_add(list, fm);
        pthread_mutex_unlock(&ctx->duplicates_mutex);

        if (fm->clone_id != old->clone_id) {
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->found++;
            pthread_mutex_unlock(&ctx->metrics_mutex);
            if (ctx->verbosity > 1) {
                PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                    clear_progress();
                    printf("'%s' is duplicated by '%s' (%zu bytes) [found: %zu]\n",
                           old->path,
                           fm->path,
                           fm->size,
                           ctx->found);
                });
            }
        }
        free_metadata(old);
    } else {
        free_metadata(fm);
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

        if (rb_tree_count(clone_counts) == 1) {
            origin = alist_get(metadata_set, 0);
            ctx->already_saved += origin->size * (alist_size(metadata_set) - 1);
            if (ctx->verbosity) {
                printf("%s is already cloned to\n",
                       origin->path);
                for (size_t i = 1; i < alist_size(metadata_set); i++) {
                    FileMetadata* fm = alist_get(metadata_set, i);
                    printf("\t%s\n", fm->path);
                }
            }
            free_clone_id_counts(clone_counts);
            clone_counts = NULL;
            return 0;
        }

        // none of the files are cloned (they all of a clone count of 1)
        if (rb_tree_count(clone_counts) == alist_size(metadata_set)) {
            // find the first file that is not compressed
            //
            // n.b.! transparently compressed files will have the UF_COMPRESSED
            //       flag set, compressed data in the resource fork, and an
            //       xattr named `com.apple.decmpfs`. since the data for the
            //       file is actually stored in metadata, these files cannot
            //       share data blocks, so they are worthless for using as
            //       clone origins.
            for (size_t i = 0; i < alist_size(metadata_set); i++) {
                FileMetadata* fm = alist_get(metadata_set, i);
                if (fm->flags & UF_COMPRESSED) {
                    if (ctx->verbosity > 1) {
                        printf("found a compressed file: %s\n", fm->path);
                    }
                    continue;
                }
                origin = fm;
                reason = "first seen";
                break;
            }

            if (!origin) {
                if (ctx->verbosity) {
                    printf("All files in this set use HFS compression. Remove HFS compression from at least one to "
                           "replace with clones:\n");
                    for (size_t i = 0; i < alist_size(metadata_set); i++) {
                        FileMetadata* fm = alist_get(metadata_set, i);
                        printf("\t%s\n", fm->path);
                    }
                }
                free_clone_id_counts(clone_counts);
                return 0;
            }
        } else {
            origin = clone_id_tree_max(clone_counts);
            reason = "most clones";
        }
        free_clone_id_counts(clone_counts);
        clone_counts = NULL;
    }

    printf("using %s as the clone origin (%s)\n",
           origin->path,
           reason);

    uint64_t origin_clone_id = get_clone_id(origin->path);

    for (size_t i = 0; i < alist_size(metadata_set); i++) {
        FileMetadata* fm = alist_get(metadata_set, i);

        if (fm == origin) {
            continue;
        }

        if (!ctx->force && fm->nlink > 1) {
            printf("\tskipping %s, hardlinked\n",
                   fm->path);
            ctx->already_saved += fm->size;
            continue;
        }

        if ((ctx->replace_mode == DEDUP_CLONE && fm->clone_id == origin->clone_id) ||
            (ctx->replace_mode == DEDUP_LINK && fm->inode == origin->inode)) {
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

        int result = 0;
        switch (ctx->replace_mode) {
        case DEDUP_CLONE:
            result = replace_with_clone(origin->path,
                                        fm->path);
            break;
        case DEDUP_LINK:
            result = replace_with_link(origin->path,
                                       fm->path);
            break;
        case DEDUP_SYMLINK:
            result = replace_with_symlink(origin->path,
                                          fm->path);
            break;
        }

        if (result) {
            perror("clone failed");
            fprintf(stderr,
                    "\tcould not clone %s\n",
                    fm->path);
            continue;
        }

        printf("\tcloned to %s\n",
               fm->path);

        if (ctx->replace_mode == DEDUP_CLONE && origin_clone_id != get_clone_id(fm->path)) {
            if (private_size(fm->path) == 0) {
                fprintf(stderr,
                        "\t\tclonefile(2) did not clone %s as expected, but it is a clone\n",
                        fm->path);
                ctx->already_saved += fm->size;
                continue;
            } else {
                fprintf(stderr,
                        "\t\tclonefile(2) did not clone %s as expected, did not report an error\n",
                        fm->path);
                continue;
            }
        }

        ctx->saved += fm->size;
    }

    return 0;
}

__attribute__((noreturn))
static void usage(char* pgm, DedupContext* ctx) {
    fprintf(stderr,
            "%s\nusage: %s [-I pattern] [-t n] [-PVcnvx] [-d n] [file ...]\n\n"
                "Options:\n"
                // "  --ignore, -I pattern     Exclude a pattern from being used as a clone\n"
                // "                           source or being replaced by a clone. This option\n"
                // "                           can be specified multiple times.\n"
                "  --dry-run, -n            Don't replace file content, just print what \n"
                "                           would have happend.\n"
                "  --depth, -d depth        Don't descend further than the specified depth.\n"
                "  --one-file-system, -x    Don't evaluate directories on a different device\n"
                "                           than the starting paths.\n"
                "  --link, -l               Use hardlinks instead of clones.\n"
                "  --symlink, -s            Use symlinks instead of clones.\n"
                // "  --color, -c              Enabled colored output.\n"
                "  --no-progress, -P        Do not display a progress bar.\n"
                "  --threads, -t n          The number of threads to use for file building\n"
                "                           lookup tables and replacing clones. Default: %d\n"
                "  --verbose, -v            Increase verbosity. May be used multiple times.\n"
                "  --version, -V            Print the version and exit\n"
                // "  --force, -f              Don't preserve existing hardlinks.\n"
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
        .force = 0,
        .replace_mode = DEDUP_CLONE,
        .thread_count = cpu_count(),
        .metrics_mutex = PTHREAD_MUTEX_INITIALIZER,
        .progress_mutex = PTHREAD_MUTEX_INITIALIZER,
        .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
        .visited_mutex = PTHREAD_MUTEX_INITIALIZER,
        .duplicates_mutex = PTHREAD_MUTEX_INITIALIZER,
        .done_mutex = PTHREAD_MUTEX_INITIALIZER,
    };

    static const struct option options[] = {
        { "ignore",          required_argument, NULL, 'I' },
        { "no-progress",     no_argument,       NULL, 'P' },
        { "version",         no_argument,       NULL, 'V' },
        { "color",           optional_argument, NULL, 'c' },
        { "depth",           required_argument, NULL, 'd' },
        { "link",            no_argument,       NULL, 'l' },
        { "dry-run",         no_argument,       NULL, 'n' },
        { "symlink",         no_argument,       NULL, 's' },
        { "threads",         required_argument, NULL, 't' },
        { "verbose",         no_argument,       NULL, 'v' },
        { "one-file-system", no_argument,       NULL, 'x' },
        // { "force",           no_argument,       NULL, 'f' },
        { "help",            no_argument,       NULL, '?' },
        { NULL, 0, NULL, 0 },
    };

    bool human_readable = false;

    int ch = -1, t;
    short d;
    while ((ch = getopt_long(argc, argv, "I:PVc::d:fhlnst:vx?", options, NULL)) != -1) {
        switch (ch) {
            case 'I':
                fprintf(stderr, "-I is unimplemented\n");
                break;
            case 'P':
                dc.progress = NULL;
                break;
            case 'V':
                fprintf(stderr, "%s\n", version);
                return 1;
            case 'c':
                fprintf(stderr, "-c is unimplemented\n");
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
            case 'h':
                human_readable = true;
                break;
            case 'l':
                dc.replace_mode = DEDUP_LINK;
                break;
            case 'n':
                dc.dry_run = true;
                break;
            case 's':
                dc.replace_mode = DEDUP_SYMLINK;
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
            case 'v':
                dc.verbosity++;
                break;
            case 'x':
                user_fts_options |= FTS_XDEV;
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

    for (int i = 0; i < argc; i++) {
        if (access(argv[i], F_OK) < 0) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            perror("access(2)");
            return 1;
        }
    }

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
        return 1;
    }
    // LCOV_EXCL_STOP

    pthread_t* threads = calloc(dc.thread_count, sizeof(pthread_t));
    for (int i = 0; i < dc.thread_count; i++) {
        int r = pthread_create(&threads[i], NULL, dedup_work, &dc);
        if (r) {
            warn("Could not create threads: error %i\nRunning single threaded.",
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
            PROGRESS_LOCK(dc.progress, &dc.progress_mutex, {
                clear_progress();
                warnx("%s: error (%d): %s",
                      entry->fts_path,
                      entry->fts_errno,
                      e);
                display_progress(dc.progress);
            });
            continue;
        }

        if (entry->fts_level > (max_depth + 1)) {
            fts_set(traversal, entry, FTS_SKIP);
            continue;
        }

        if (dc.replace_mode == DEDUP_CLONE &&
            current_dev != entry->fts_statp->st_dev) {
            current_dev = entry->fts_statp->st_dev;
            clonefile_supported = is_clonefile_supported(entry->fts_path);

            if (!clonefile_supported) {
                warnx("Skipping %s: cloning not supported", entry->fts_path);

                // if FTS_XDEV is set, we can't accidentally cross into a
                // volume that does support clonefile, so skip everything else
                if (user_fts_options & FTS_XDEV) {
                    fts_set(traversal, entry, FTS_SKIP);
                    continue;
                }
            }
        }

        if (dc.replace_mode == DEDUP_CLONE &&
            !clonefile_supported) {
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

        // the file looks like a previously failed clone
        if (strnlen(entry->fts_path, PATH_MAX) > 3 &&
            entry->fts_path[0] == '.' &&
            entry->fts_path[1] == '~' &&
            entry->fts_path[2] == '.') {
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

    fts_close(traversal);

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

    free_file_entry_queue(queue); queue = NULL;
    free_visited_tree(dc.visited); dc.visited = NULL;

    if (dc.progress) {
        clear_progress();
    }
    printf("duplicates found: %zu\n", dc.found);

    SHA256ListNode* duplicate_set = NULL;
    RB_TREE_FOREACH(duplicate_set, dc.duplicates) {
        deduplicate(duplicate_set->list, &dc);
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
        printf("%zu", dc.already_saved);
    }
    putchar('\n');

    free_duplicate_tree(dc.duplicates); dc.duplicates = NULL;
    return 0;
}
