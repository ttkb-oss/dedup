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

#ifndef __DEDUP_CLONE_H__
#define __DEDUP_CLONE_H__

/// replace_with_clone
///
/// The `replace_with_clone` function causes the link named `dst` to be
/// replaced with a clone of `src`. Unlike `clonefile(2)`, it is expected
/// that `dst` already exists. All metadata (mode, flags, & ACLs)
/// are retained on `dst`. The current user must have read access to both
/// `src` and `dst` and write access to `dst` and the directory where
/// `dst` resides.
///
/// `src` and `dst` must be on the same volume. (TODO: is it enough
/// that they are with in the same APFS partition, can clones span
/// volume boundaries?).
///
/// On success `dst` will have its data replaced with copy-on-write,
/// cloned blocks from `src` and `replace_with_clone` returns 0.
///
/// On failure clonefile will return one of the following values:
///
///   [ENOMEM]     the name of the temporary file is longer than `PATH_MAX`
///
///   In addition, `replace_with_clone` may return any error
///   returned by `clonefile(2)`, `copyfile(2)`, or `rename(2)`.
///
/// See also: `clonefile(2)`, `copyfile(2)`, or `rename(2)`
int replace_with_clone(const char* src, const char* dst);

#endif // __DEDUP_CLONE_H__
