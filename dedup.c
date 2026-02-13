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
#define VERSION 0.0.0
#endif // VERSION
#ifndef BUILD_DATE
#define BUILD_DATE 00000000
#endif // BUILD_DATE
#define STR(x) #x
#define XSTR(x) STR(x)
__used static char const version[] =
    "TTKB dedup " XSTR(VERSION) " (" XSTR(BUILD_DATE) ")";
#if 0
static char sccsid[] = "@(#)dedup.c)";
#endif // 0
#endif // lint

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>

#include <assert.h>
#include <err.h>
#include <fts.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "clone.h"
#include "map.h"
#include "progress.h"
#include "queue.h"
#include "output_format.h"
#include "seen_set.h"
#include "signature.h"
#include "sig_table.h"
#include "utils.h"

#define PROGRESS_LOCK(p, m, block) do { \
        if ((p)) { \
            pthread_mutex_lock((m)); \
            block; \
            pthread_mutex_unlock((m)); \
        } \
    } while (0)

// Forward declaration
typedef struct DedupContext DedupContext;

// Clone record streaming functions
static FILE* clone_summary_stream = NULL;
static pthread_mutex_t clone_summary_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t clone_summary_count = 0;

static void clone_summary_open(const char* filepath) {
    pthread_mutex_lock(&clone_summary_mutex);
    if (!clone_summary_stream) {
        clone_summary_stream = fopen(filepath, "w");
        if (clone_summary_stream) {
            fprintf(clone_summary_stream, "DEDUP CLONING SUMMARY\n");
            fprintf(clone_summary_stream, "=====================\n");
            fprintf(clone_summary_stream, "\n");
        }
    }
    pthread_mutex_unlock(&clone_summary_mutex);
}

static void clone_summary_write(const char* origin, const char* clone, size_t size) {
    pthread_mutex_lock(&clone_summary_mutex);
    if (clone_summary_stream) {
        fprintf(clone_summary_stream, "  Origin: %s\n", origin);
        fprintf(clone_summary_stream, "    Clone: %s (size: %zu bytes)\n", clone, size);
        clone_summary_count++;
        fflush(clone_summary_stream);
    }
    pthread_mutex_unlock(&clone_summary_mutex);
}

static void clone_summary_close() {
    pthread_mutex_lock(&clone_summary_mutex);
    if (clone_summary_stream) {
        // Write count at the end
        fprintf(clone_summary_stream, "\nTotal cloning operations: %zu\n", clone_summary_count);
        fflush(clone_summary_stream);
        
        fclose(clone_summary_stream);
        clone_summary_stream = NULL;
        clone_summary_count = 0;
    }
    pthread_mutex_unlock(&clone_summary_mutex);
}

typedef enum ReplaceMode {
    DEDUP_CLONE    = 0,
    DEDUP_LINK     = 1,
    DEDUP_SYMLINK  = 2,
} ReplaceMode;

typedef struct DedupContext {
    Progress* progress;
    FileEntryHead* queue;
    FileEntryHead* raw_queue;
    SigTable* signatures;
    size_t found;
    size_t saved;
    size_t already_saved;
    size_t pruned;
    size_t total_bytes;  // Accumulated bytes of all scanned files
    size_t queued_count; // Entries in raw_queue + work_queue combined
    uint8_t scan_done;
    uint8_t prune_done;
    uint8_t thread_count;
    bool dry_run;
    uint8_t verbosity;
    bool force;
    ReplaceMode replace_mode;
    OutputFormat output_format;
    bool clone_converted;        // Whether to convert clones (true by default)
    char* summary_file;          // File to write detailed summary to (NULL by default)
    pthread_mutex_t metrics_mutex;
    pthread_mutex_t progress_mutex;
    pthread_mutex_t queue_mutex;
    pthread_mutex_t raw_queue_mutex;
    pthread_mutex_t signatures_mutex;
    pthread_mutex_t scan_done_mutex;
    pthread_mutex_t prune_done_mutex;
} DedupContext;

static int get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80; // fallback
}

// Fixed-width compact format for a file count (e.g., "2.1K", " 99K", "999K")
static void format_count(size_t count, char buf[5]) {
    buf[4] = '\0';
    
    if (count < 1000) {
        snprintf(buf, 5, "%4zu", count);
    } else if (count < 9950) {
        snprintf(buf, 5, "%.1fK", (double)count / 1000.0);
    } else if (count < 999500) {
        snprintf(buf, 5, "%3lluK", (unsigned long long)((count + 500) / 1000));
    } else if (count < 9950000) {
        snprintf(buf, 5, "%.1fM", (double)count / 1000000.0);
    } else if (count < 999500000) {
        snprintf(buf, 5, "%3lluM", (unsigned long long)((count + 500000) / 1000000));
    } else {
        snprintf(buf, 5, "%.1fG", (double)count / 1000000000.0);
    }
    
    size_t len = strlen(buf);
    if (len < 4) {
        size_t pad = 4 - len;
        memmove(buf + pad, buf, len + 1);
        for (size_t i = 0; i < pad; i++) buf[i] = ' ';
    } else if (len > 4) {
        buf[4] = '\0';
    }
}

static void display_status(DedupContext* ctx, const char* path) {
    if (!isatty(STDOUT_FILENO)) {
        return;
    }
    
    static bool header_printed = false;
    if (!header_printed) {
        fprintf(stderr, "%-4s %-4s %-4s %-5s %-20s %s\n",
                "TOTL", "QUED", "SHRD", "DELTA", "PROGRESS", "PATH");
        header_printed = true;
    }

    int width = get_terminal_width();
    if (width < 80) width = 80;
    
    pthread_mutex_lock(&ctx->metrics_mutex);
    size_t total_bytes = ctx->total_bytes;
    size_t queued = ctx->queued_count;
    size_t shared = ctx->already_saved;
    size_t delta = ctx->saved;
    pthread_mutex_unlock(&ctx->metrics_mutex);
    
    size_t completed = 0;
    size_t total = 0;
    if (ctx->progress) {
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            completed = ctx->progress->completedUnitCount;
            total = ctx->progress->totalUnitCount;
        });
    }
    
    char total_bytes_str[5];
    char queued_str[5];
    char shared_str[5];
    char delta_str[5];
    char done_str[5];
    char total_str[5];
    
    format_compact(total_bytes, total_bytes_str);
    format_compact(queued, queued_str);
    format_compact(shared, shared_str);
    format_compact(delta, delta_str);
    format_count(completed, done_str);
    format_count(total, total_str);
    
    char bar[12];
    bar[0] = '[';
    bar[10] = ']';
    bar[11] = '\0';
    
    if (total == 0) {
        for (int i = 1; i < 10; i++) bar[i] = '-';
    } else {
        double fraction = (double)completed / (double)total;
        int fill = (int)(fraction * 8.0);
        if (fill < 0) fill = 0;
        if (fill > 8) fill = 8;
        
        for (int i = 1; i <= fill; i++) bar[i] = '=';
        for (int i = fill + 1; i < 10; i++) bar[i] = '-';
    }
    
    char progress_str[21];
    snprintf(progress_str, sizeof(progress_str), "%s%s%s", done_str, bar, total_str);
    
    int fixed_width = 41;
    int path_max = width - fixed_width - 1;
    if (path_max < 10) path_max = 10;
    
    char truncated_path[256];
    memset(truncated_path, 0, sizeof(truncated_path));
    if (path) {
        size_t len = strlen(path);
        if (len <= (size_t)path_max) {
            strcpy(truncated_path, path);
        } else {
            size_t keep = path_max - 1;
            truncated_path[0] = '~';
            strncpy(truncated_path + 1, path + len - keep, keep);
            truncated_path[keep + 1] = '\0';
        }
    }
    
    char delta_display[6];
    if (delta > 0) {
        snprintf(delta_display, sizeof(delta_display), "+%-4s", delta_str);
    } else {
        snprintf(delta_display, sizeof(delta_display), " %-4s", delta_str);
    }
    delta_display[5] = '\0';
    
    int line_len = 4 + 1 + 4 + 1 + 4 + 1 + 5 + 1 + 20 + 1 + (int)strlen(truncated_path);
    int padding = width - line_len;
    if (padding < 0) padding = 0;
    
    char padding_str[256];
    if (padding > 0 && padding < (int)sizeof(padding_str) - 1) {
        memset(padding_str, ' ', padding);
        padding_str[padding] = '\0';
    } else {
        padding_str[0] = '\0';
    }
    
    fprintf(stderr, "\r%-4s %-4s %-4s %s %s %s%s\033[0m\033[K",
            total_bytes_str, queued_str, shared_str, delta_display,
            progress_str, truncated_path, padding_str);
    fflush(stderr);
}

void visit_entry(FileEntry* fe, Progress* p, DedupContext* ctx) {
    if (!fe || !ctx) {
        return;
    }

    FileSignature* sig = compute_signature(fe->path, fe->device, fe->size);
    if (!sig) {
        // Silently skip files we can't read (locked, slow FUSE mounts, etc)
        return;
    }

    // Insert into signature table and check for duplicates
    if (!ctx->signatures) {
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            clear_progress();
            fprintf(stderr, "signature table not initialized for %s\n", fe->path);
        });
        free_signature(sig);
        return;
    }

    pthread_mutex_lock(&ctx->signatures_mutex);
    size_t table_size_before = sig_table_size(ctx->signatures);
    SigTableEntry* existing = sig_table_insert(ctx->signatures, sig, fe->path, get_clone_id(fe->path));
    size_t table_size_after = sig_table_size(ctx->signatures);
    pthread_mutex_unlock(&ctx->signatures_mutex);

    // Safety check: table size should only increase by 0 or 1
    if (table_size_after > table_size_before + 1) {
        PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
            clear_progress();
            fprintf(stderr, "warning: signature table size increased by %zu (expected 0 or 1)\n",
                    table_size_after - table_size_before);
        });
    }

    bool table_took_ownership = (table_size_after > table_size_before);

    if (existing) {
        // Found a duplicate
        display_status(ctx, fe->path);

        // Check if already deduplicated
        uint64_t current_clone_id = get_clone_id(fe->path);
        if ((ctx->replace_mode == DEDUP_CLONE && current_clone_id == existing->clone_id) ||
            (ctx->replace_mode == DEDUP_LINK && fe->inode == get_inode(existing->path))) {
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->already_saved += fe->size;
            pthread_mutex_unlock(&ctx->metrics_mutex);
            free_signature(sig);
            return;
        }

        // Skip if hardlinked and not forcing
        if (!ctx->force && fe->nlink > 1) {
            if (ctx->verbosity) {
                PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                    clear_progress();
                    printf("skipping %s, hardlinked\n", fe->path);
                });
            }
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->already_saved += fe->size;
            pthread_mutex_unlock(&ctx->metrics_mutex);
            free_signature(sig);
            return;
        }

        // Skip if immutable or read-only
        if (fe->flags & UF_IMMUTABLE || fe->flags & SF_IMMUTABLE ||
            (access(fe->path, W_OK) != 0)) {
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->already_saved += fe->size;
            pthread_mutex_unlock(&ctx->metrics_mutex);
            free_signature(sig);
            return;
        }

        // Perform deduplication
        if (ctx->dry_run) {
            if (ctx->verbosity) {
                PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                    clear_progress();
                    printf("would deduplicate %s to %s\n", fe->path, existing->path);
                });
            }
            pthread_mutex_lock(&ctx->metrics_mutex);
            ctx->saved += fe->size;
            ctx->found++;
            pthread_mutex_unlock(&ctx->metrics_mutex);
        } else {
            // Check if clone conversion is disabled
            if (ctx->replace_mode == DEDUP_CLONE && !ctx->clone_converted) {
                // Skip clone conversion, just count as already saved
                pthread_mutex_lock(&ctx->metrics_mutex);
                ctx->already_saved += fe->size;
                pthread_mutex_unlock(&ctx->metrics_mutex);
                free_signature(sig);
                return;
            }
            
            int result = 0;
            switch (ctx->replace_mode) {
            case DEDUP_CLONE:
                result = replace_with_clone(existing->path, fe->path);
                break;
            case DEDUP_LINK:
                result = replace_with_link(existing->path, fe->path);
                break;
            case DEDUP_SYMLINK:
                result = replace_with_symlink(existing->path, fe->path);
                break;
            }

            if (result) {
                // Silently skip clone failures (permissions, filesystem issues, etc)
                free_signature(sig);
                return;
            } else {
                if (ctx->verbosity) {
                    PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                        clear_progress();
                        printf("deduplicated %s\n", fe->path);
                    });
                }

                // Verify clone worked if using clone mode
                if (ctx->replace_mode == DEDUP_CLONE) {
                    uint64_t new_clone_id = get_clone_id(fe->path);
                    if (new_clone_id != existing->clone_id) {
                        if (private_size(fe->path) != 0) {
                            result = -1; // Mark as failed
                        }
                        // Else: clone_id mismatch but no private data = success
                    }
                }

                if (result == 0) {
                    pthread_mutex_lock(&ctx->metrics_mutex);
                    ctx->saved += fe->size;
                    ctx->found++;
                    pthread_mutex_unlock(&ctx->metrics_mutex);
                    
                    // Write clone operation to summary file (streamed)
                    if (ctx->summary_file) {
                        clone_summary_write(existing->path, fe->path, fe->size);
                    }
                } else {
                    pthread_mutex_lock(&ctx->metrics_mutex);
                    ctx->already_saved += fe->size; // Count as already saved to avoid double counting
                    pthread_mutex_unlock(&ctx->metrics_mutex);
                }
            }
        }

        free_signature(sig);
    } else {
        // First instance of this signature
        if (table_took_ownership) {
            display_status(ctx, fe->path);
        } else {
            // Table failed to take ownership (allocation failure)
            PROGRESS_LOCK(ctx->progress, &ctx->progress_mutex, {
                clear_progress();
                fprintf(stderr, "failed to store signature for %s: memory allocation failed\n", fe->path);
            });
            free_signature(sig);
        }
    }
}

// Returns true if the entry was pruned (caller should free it), false if it survived.
static bool prune_entry(FileEntry* fe, SeenSet* seen_inodes, SeenSet* seen_clones, DedupContext* c) {
    if (fe->nlink > 1) {
        uint64_t key = (uint64_t)fe->device << 32 | (uint64_t)(fe->inode & 0xFFFFFFFF);
        if (seen_set_insert(seen_inodes, key)) {
            pthread_mutex_lock(&c->metrics_mutex);
            c->already_saved += fe->size;
            c->pruned++;
            pthread_mutex_unlock(&c->metrics_mutex);
            PROGRESS_LOCK(c->progress, &c->progress_mutex, {
                c->progress->completedUnitCount++;
            });
            display_status(c, fe->path);
            return true;
        }
    }

    uint64_t clone_id = get_clone_id(fe->path);
    if (clone_id != 0) {
        if (seen_set_insert(seen_clones, clone_id)) {
            pthread_mutex_lock(&c->metrics_mutex);
            c->already_saved += fe->size;
            c->pruned++;
            pthread_mutex_unlock(&c->metrics_mutex);
            PROGRESS_LOCK(c->progress, &c->progress_mutex, {
                c->progress->completedUnitCount++;
            });
            display_status(c, fe->path);
            return true;
        }
    }

    return false;
}

void* prune_work(void* ctx) {
    DedupContext* c = ctx;
    SeenSet* seen_inodes = new_seen_set(4096);
    SeenSet* seen_clones = new_seen_set(4096);

    for (;;) {
        pthread_mutex_lock(&c->raw_queue_mutex);
        FileEntry* fe = file_entry_next(c->raw_queue);
        pthread_mutex_unlock(&c->raw_queue_mutex);

        if (!fe) {
            pthread_mutex_lock(&c->scan_done_mutex);
            uint8_t done = c->scan_done;
            pthread_mutex_unlock(&c->scan_done_mutex);
            if (done) {
                // Drain remaining entries
                pthread_mutex_lock(&c->raw_queue_mutex);
                fe = file_entry_next(c->raw_queue);
                pthread_mutex_unlock(&c->raw_queue_mutex);
                if (!fe) break;
            } else {
                usleep(100);
                continue;
            }
        }

        // Decrement queued_count for raw_queue pop
        pthread_mutex_lock(&c->metrics_mutex);
        c->queued_count--;
        pthread_mutex_unlock(&c->metrics_mutex);

        if (prune_entry(fe, seen_inodes, seen_clones, c)) {
            file_entry_free(fe);
            continue;
        }

        // Survivor: pass to work queue
        // queued_count stays the same (file moves between queues)
        pthread_mutex_lock(&c->queue_mutex);
        file_entry_queue_append(c->queue,
                                fe->path,
                                fe->device,
                                fe->inode,
                                fe->nlink,
                                fe->flags,
                                fe->size,
                                fe->level);
        pthread_mutex_unlock(&c->queue_mutex);
        
        // Show pruner progress (survivor passes through)
        display_status(c, fe->path);
        file_entry_free(fe);
    }

    free_seen_set(seen_inodes);
    free_seen_set(seen_clones);
    return NULL;
}

void* dedup_work(void* ctx) {
    DedupContext* c = ctx;
    uint8_t done = c->prune_done;

    while (!done) {
        pthread_mutex_lock(&c->queue_mutex);
        FileEntry* fe = file_entry_next(c->queue);
        pthread_mutex_unlock(&c->queue_mutex);

        if (!fe) {
            pthread_mutex_lock(&c->prune_done_mutex);
            done = c->prune_done;
            pthread_mutex_unlock(&c->prune_done_mutex);
            if (done) {
                break;
            }
            usleep(100);
            continue;
        }

        // Decrement queued_count for work_queue pop
        pthread_mutex_lock(&c->metrics_mutex);
        c->queued_count--;
        pthread_mutex_unlock(&c->metrics_mutex);

        // Visit the entry (worker processes the file)
        visit_entry(fe, c->progress, c);

        PROGRESS_LOCK(c->progress, &c->progress_mutex, {
            c->progress->completedUnitCount++;
        });
        
        // Show worker progress (entry processed)
        display_status(c, fe->path);
        file_entry_free(fe);

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
                "  --format, -F format      Output format for byte sizes. See --help formats.\n"
                "  --one-file-system, -x    Don't evaluate directories on a different device\n"
                "                           than the starting paths.\n"
                "  --link, -l               Use hardlinks instead of clones.\n"
                "  --symlink, -s            Use symlinks instead of clones.\n"
                // "  --color, -c              Enabled colored output.\n"
                "  --no-progress, -P        Do not display a progress bar.\n"
                "  --no-clone-conversion    Do not convert clones (skip clone mode)\n"
                "  --summary, -S file       Write detailed cloning summary to file\n"
                "                           (itemized by directory hierarchy)\n"
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

int main(int argc, char* argv[]) {

    FileEntryHead* queue = new_file_entry_queue();
    FileEntryHead* raw_queue = new_file_entry_queue();
    Progress p = { 0 };
    uint16_t max_depth = UINT16_MAX;
    int user_fts_options = 0;

    DedupContext dc = {
        .progress = &p,
        .queue = queue,
        .raw_queue = raw_queue,
        .signatures = new_sig_table(65536),
        .found = 0,
        .saved = 0,
        .already_saved = 0,
        .pruned = 0,
        .total_bytes = 0,
        .queued_count = 0,
        .scan_done = 0,
        .prune_done = 0,
        .dry_run = false,
        .verbosity = 0,
        .force = 0,
        .replace_mode = DEDUP_CLONE,
        .output_format = OUTPUT_SI_HUMAN,
        .clone_converted = true,
        .summary_file = NULL,
        .thread_count = cpu_count(),
        .metrics_mutex = PTHREAD_MUTEX_INITIALIZER,
        .progress_mutex = PTHREAD_MUTEX_INITIALIZER,
        .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
        .raw_queue_mutex = PTHREAD_MUTEX_INITIALIZER,
        .signatures_mutex = PTHREAD_MUTEX_INITIALIZER,
        .scan_done_mutex = PTHREAD_MUTEX_INITIALIZER,
        .prune_done_mutex = PTHREAD_MUTEX_INITIALIZER,
    };

    // Validate signature table was created successfully
    if (!dc.signatures) {
        fprintf(stderr, "failed to create signature table\n");
        return 1;
    }

    static const struct option options[] = {
        { "ignore",          required_argument, NULL, 'I' },
        { "no-progress",     no_argument,       NULL, 'P' },
        { "version",         no_argument,       NULL, 'V' },
        { "color",           optional_argument, NULL, 'c' },
        { "depth",           required_argument, NULL, 'd' },
        { "format",          required_argument, NULL, 'F' },
        { "link",            no_argument,       NULL, 'l' },
        { "dry-run",         no_argument,       NULL, 'n' },
        { "symlink",         no_argument,       NULL, 's' },
        { "threads",         required_argument, NULL, 't' },
        { "verbose",         no_argument,       NULL, 'v' },
        { "one-file-system", no_argument,       NULL, 'x' },
        // { "force",           no_argument,       NULL, 'f' },
        { "no-clone-conversion", no_argument,   NULL, 'C' },
        { "summary",         required_argument, NULL, 'S' },
        { "help",            no_argument,       NULL, '?' },
        { NULL, 0, NULL, 0 },
    };

    bool human_readable = true;

    int ch = -1, t;
    short d;
    while ((ch = getopt_long(argc, argv, "I:PVc::d:F:hlnst:vxCS:", options, NULL)) != -1) {
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
            case 'F':
                dc.output_format = parse_output_format(optarg);
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
            case 'C':
                dc.clone_converted = false;
                break;
            case 'S':
                dc.summary_file = strdup(optarg);
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
    
    // Open summary file if requested
    if (dc.summary_file) {
        clone_summary_open(dc.summary_file);
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

    pthread_t pruner_thread = NULL;
    if (dc.thread_count > 0) {
        int r = pthread_create(&pruner_thread, NULL, prune_work, &dc);
        if (r) {
            warn("Could not create pruner thread: error %i", r);
            pruner_thread = NULL;
        }
    }

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

    // Single-threaded seen sets (only used when thread_count == 0)
    SeenSet* st_seen_inodes = NULL;
    SeenSet* st_seen_clones = NULL;
    if (dc.thread_count == 0) {
        st_seen_inodes = new_seen_set(4096);
        st_seen_clones = new_seen_set(4096);
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
            });
            display_status(&dc, entry->fts_path);
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

        // skip .padding files (browser cache files that are often locked)
        const char* basename = strrchr(entry->fts_path, '/');
        if (basename && strcmp(basename + 1, ".padding") == 0) {
            continue;
        }

        // skip cloud storage mounts (GoogleDrive, Dropbox, iCloud, OneDrive)
        if (strstr(entry->fts_path, "/Library/CloudStorage/")) {
            continue;
        }

        // skip iOS simulator cache files (often locked)
        if (strstr(entry->fts_path, "/PhotoData/Caches/")) {
            continue;
        }

        // at this point we have a regular file
        // that only has one link
        pthread_mutex_lock(&dc.metrics_mutex);
        dc.total_bytes += entry->fts_statp->st_size;
        pthread_mutex_unlock(&dc.metrics_mutex);
        PROGRESS_LOCK(dc.progress, &dc.progress_mutex, {
            dc.progress->totalUnitCount++;
        });
        display_status(&dc, entry->fts_path);

        // Track queued count (increment for raw_queue entry)
        pthread_mutex_lock(&dc.metrics_mutex);
        dc.queued_count++;
        pthread_mutex_unlock(&dc.metrics_mutex);

        if (dc.thread_count == 0) {
            // Single-threaded: prune and process inline
            file_entry_queue_append(raw_queue,
                                    entry->fts_path,
                                    entry->fts_statp->st_dev,
                                    entry->fts_statp->st_ino,
                                    entry->fts_statp->st_nlink,
                                    entry->fts_statp->st_flags,
                                    entry->fts_statp->st_size,
                                    entry->fts_level);
            FileEntry* fe = file_entry_next(raw_queue);
            // Decrement queued_count since we're processing it immediately
            pthread_mutex_lock(&dc.metrics_mutex);
            dc.queued_count--;
            pthread_mutex_unlock(&dc.metrics_mutex);
            
            if (prune_entry(fe, st_seen_inodes, st_seen_clones, &dc)) {
                file_entry_free(fe);
            } else {
                // Survivor goes to work_queue (already counted in queued_count)
                file_entry_queue_append(queue,
                                        fe->path,
                                        fe->device,
                                        fe->inode,
                                        fe->nlink,
                                        fe->flags,
                                        fe->size,
                                        fe->level);
                file_entry_free(fe);
                dedup_work(&dc);
            }
        } else {
            pthread_mutex_lock(&dc.raw_queue_mutex);
            file_entry_queue_append(raw_queue,
                                    entry->fts_path,
                                    entry->fts_statp->st_dev,
                                    entry->fts_statp->st_ino,
                                    entry->fts_statp->st_nlink,
                                    entry->fts_statp->st_flags,
                                    entry->fts_statp->st_size,
                                    entry->fts_level);
            pthread_mutex_unlock(&dc.raw_queue_mutex);
        }
    }

    fts_close(traversal);

    // Signal FTS scan complete to pruner
    pthread_mutex_lock(&dc.scan_done_mutex);
    dc.scan_done = 1;
    pthread_mutex_unlock(&dc.scan_done_mutex);

    // Wait for pruner to drain raw_queue
    if (pruner_thread) {
        if (pthread_join(pruner_thread, NULL)) {
            fprintf(stderr, "Failed to wait for pruner thread\n");
        }
    }

    // Signal pruner complete to workers
    pthread_mutex_lock(&dc.prune_done_mutex);
    dc.prune_done = 1;
    pthread_mutex_unlock(&dc.prune_done_mutex);

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

    free_seen_set(st_seen_inodes);
    free_seen_set(st_seen_clones);
    free_file_entry_queue(raw_queue); raw_queue = NULL;
    free_file_entry_queue(queue); queue = NULL;
    free_sig_table(dc.signatures); dc.signatures = NULL;

    if (dc.progress) {
        clear_progress();
    }
    printf("duplicates found: %zu\n", dc.found);
    printf("entries pruned: %zu\n", dc.pruned);

    // Fast dedup processes files immediately during traversal
    // No additional deduplication step needed

    printf("bytes saved: ");
    if (human_readable) {
        printf("%s", format_bytes(dc.saved, dc.output_format));
    } else {
        printf("%zu", dc.saved);
    }
    putchar('\n');

    printf("already saved: ");
    if (human_readable) {
        printf("%s", format_bytes(dc.already_saved, dc.output_format));
    } else {
        printf("%zu", dc.already_saved);
    }
    putchar('\n');

    // Clear status line
    if (isatty(STDOUT_FILENO)) {
        fprintf(stderr, "\r\033[K\n");
    }
    
    // Close summary file if it was opened
    if (dc.summary_file) {
        clone_summary_close();
    }

    return 0;
}
