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
#include <sys/clonefile.h>

#include <copyfile.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <unistd.h>

#include "clone.h"

int replace_with_clone(const char* src, const char* dst) {
    char base[PATH_MAX];
    if (!basename_r(dst, base)) {
        return errno;
    }

    char dir[PATH_MAX];
    if (!dirname_r(dst, dir)) {
        return errno;
    }

    char path[PATH_MAX];
    int result = snprintf(path,
                          PATH_MAX,
                          "%s/.~.%s",
                          dir,
                          base);
    if (result >= PATH_MAX) {
        fprintf(stderr, "tmp path exceeded available path size");
        return ENAMETOOLONG;
    }

    errno = 0;
    result = clonefile(src,
                       path,
                       0);
    if (result) {
        perror("could not clonefile");
        unlink(path); // if it exists
        return result;
    }

    // TODO: use COPYFILE_CHECK during dry-run
    // TODO: can COPYFILE_EXCL be used?
    result = copyfile(dst, path, NULL,
                      COPYFILE_METADATA);
    if (result) {
        perror("could not copy metadata");
        unlink(path);
        return result;
    }

    printf("int result = rename(\"%s\", \"%s\");\n",
           path,
           dst);
    result = rename(path, dst);
    if (result) {
        perror("could not replace existing file");
        unlink(path);
        return result;
    }

    return 0;
}
