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
#if defined(__APPLE__)
#include <sys/clonefile.h>
#include <copyfile.h>
#endif
#include <sys/stat.h>
#if defined(__FREEBSD__)
#include <sys/ioctl.h>
#endif

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "clone.h"

int find_zero_file(const char* restrict path) {
    if (access(path, W_OK)) {
        return 1;
    }

    struct stat s = { 0 };
    if (stat(path, &s)) {
        fprintf(stderr, "Could not stat %s\n", path);
        perror("stat(2)");
        return 2;
    }

    if (s.st_size == 0) {
        return 3;
    }

    return 0;
}

// Expects `out` to be char[PATH_MAX].
//
char* tmp_name(const char* restrict path, char* restrict out, size_t size) {
    out[0] = '\0';

    char dir[PATH_MAX] = { 0 };
    if (!dirname_r(path, dir)) {
        return NULL;
    }

    char base[PATH_MAX] = { 0 };
    if (!basename_r(path, base)) {
        return NULL;
    }

    if (strlcat(out, dir, size) >= size ||
        strlcat(out, "/.~.", size) >= size ||
        strlcat(out, base, size) >= size) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    if (access(out, F_OK) == 0) {
        fprintf(stderr,
                "Staging file %s already exists. Remove it to replace %s with a clone\n",
                out,
                path);
        errno = EEXIST;
        return NULL;
    }

    return out;
}

int genfile_clone(const char* src, const char* dst) {
#if defined(__APPLE__)
    return clonefile(src, dst, 0);
#elif defined(__FREEBSD__)
    // n.b.! This is completely untested and probably erases
    //       everything it touches. There are currently no
    //       equivalents to volume capability checks on the
    //       frontend, so it's not even clear that the files
    //       that are being passed in here are on a partition
    //       that can be cloned.
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("open(2) failed");
        return errno;
    }
    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL);
    if (dst_fd < 0) {
        close(src_fd);
        perror("open(2) failed");
        return errno;
    }
    result = ioctl(dst_fd, CF_FICLONE, src_fd);
    int errno_saved = errno;
    close(src_fd);
    close(dst_fd);
    errno = errno_saved;
    return result;
#else
#error Operating system not supported.
#endif
}

int replace_with_clone(const char* src, const char* dst) {
    char path[PATH_MAX] = { 0 };
    if (!tmp_name(dst, path, PATH_MAX)) {
        return errno;
    }

    errno = 0;
    int result = genfile_clone(src, path);

    if (result) {
        perror("could not clonefile");
        unlink(path); // if it exists
        return result;
    }

    if (find_zero_file(path)) {
        fprintf(stderr,
                "invalid file created by clonefile(2)\n");
        unlink(path);
        return ENOENT;
    }

#if defined(__APPLE__)
    // TODO: use COPYFILE_CHECK during dry-run and
    //       higher verbosity levels
    int check = copyfile(dst,
                         path,
                         NULL,
                         COPYFILE_CHECK | COPYFILE_METADATA);
    if (check & COPYFILE_DATA) {
        perror("copyfile(3) should not copy data");
        unlink(path);
        return check;
    }

    result = copyfile(dst,
                      path,
                      NULL,
                      COPYFILE_METADATA | (1<<31));
    if (result) {
        perror("could not copy metadata");
        unlink(path);
        return result;
    }

    if (find_zero_file(path)) {
        fprintf(stderr,
                "invalid file created by copyfile(3)\n");
        unlink(path);
        return ENOENT;
    }
#else
#error Operating system not supported
#endif

    // TODO: use COPYFILE_CHECK to verify that nothing
    //       would be copied back to the original file

    result = rename(path, dst);
    if (result) {
        perror("could not replace existing file");
        unlink(path);
        return result;
    }

    return 0;
}
