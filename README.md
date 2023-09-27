<img src="doc/banner.png" width="200px">

# `dedup(1)`

A macOS utility to replace duplicate file data with a copy-on-write clone.

# SYNOPSIS

**dedup** `[-PVnvx]` [`-t`&nbsp;**threads**] [`-d`&nbsp;**depth**] [*file&nbsp;...*]

# DESCRIPTION

**dedup** finds files with identical content using the provided *file* arguments.
Duplicates are replaced with a clone of another (using [`clonefile(2)`](https://www.unix.com/man-page/mojave/2/clonefile/)).
If no *file* is specified, the current directory is used.

Cloned file share data blocks with the file they were cloned from, saving space
on disk. Unlike a hardlinked file, any future modification to either the clone
or the original file will be remain private to that file (copy-on-write).

**dedup** works in two phases. First it evaluates all of the paths provided
recursively, looking for duplicates. Once all duplicates are found, any files
that are not already clones of "best" clone source are replaced with clones.

There are limits which files can be cloned:

1. the file must be a regular file
2. the file must have only one link
3. the file and its directory must be writable by the user

The "best" source is chosen by first finding the file with the most hard links.
Files with multiple hard links will not be replaced, so using them as the source
of other clones allows their blocks to be shared without modifying the data to
which they point. If all files have a single link, a file which shares the most
clones with others is chosen. This ensures that files which have been previously
processed will not need to be replaced during subsequent evaluations of the same
directory. If none of the files have multiple links or clones, the first file
encountered will be chosen.

Files with multiple hard links are not replaced because it is not possible to
guarantee all other links to that inode exist within the tree(s) being
evaluated. Replacing a link with a clone changes the semantics from two link
pointing at the same, mutable shared storage to two links pointing at the same
copy-on-write storage. For scenarios where hard links were previously being
used because clones were not available, future versions may provide a flag to
destructively replace hard links with clones. Future versions may also consider
cloning files with multiple hard links if all links are within the space being
evaluated and two or more hard link clusters reference duplicated data.

If all files in a matched set are compressed with HFS transparent compression,
none of the files with be deduplicated. Future versions of **dedup** may
select one file from the set to decompress in place and then use that file
as a clone source.

**dedup** will only work on volumes that have the `VOL_CAP_INT_CLONE`
capability. Currently that is limited to APFS.

# OPTIONS

The following options are available:

**-d** *depth*, **-&#45;depth** *depth*

> Only traverse
> *depth*
> directories deep into each provided path.

**-h**

> Display sizes using SI suffixes with 2-4 digits of precision.

**-n**, **-&#45;dry-run**

> Evaluate all files and find all duplicates but only print what would be done
> and do not modify any files.

**-P**, **-&#45;no-progress**

> Do not display a progress bar.

**-t** *threads*

> The number of threads to use for evaluating files. By default this is the same
> as the number of CPUs on the host as described by the `hw.ncpu` value returned
> by [`sysctl(8)`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/sysctl.3.html).
> If the value 0 is provided all evaluation will be done serially in the main
> thread.

**-V**, **-&#45;version**

> Print the version and exit

**-v**, **-&#45;verbose**

> Increase verbosity. May be specified multiple times.

**-x**, **-&#45;one-file-system**

> Prevent
> **dedup**
> from descending into directories that have a device number different than that
> of the file from which the descent began.

**-**?, **-&#45;help**

> Print a summary of options and exit.

# EXIT STATUS

The **dedup** utility exits 0 and &gt;0 if an error occurs.

It is possible that during abnormal termination a temporary clone will be left
before it is moved to the path it is replacing. In such cases a file with the
prefix `.~.` followed by the name of the file that was to be replaced will exist
in the same directory as target file.

`find(1)` can be used to find these temporary files if necessary.

    $ find . -name '.~.*'

# EXAMPLES

Find all duplicates and display which files would be replaced by clones along
with the estimated space that would be saved.

	$ dedup -n

Limit execution to a single thread, disable progress, and display human readable
output while deduplicating files in `~/Downloads` and `/tmp`

    $ dedup -t1 -Ph ~/Downloads /tmp

# SEE ALSO

[`clonefile(2)`](https://www.unix.com/man-page/mojave/2/clonefile/)

# STANDARDS

Sometimes.

# AUTHORS

**dedup**
was written by
Jonathan Hohle &lt;[jon@ttkb.co](mailto:jon@ttkb.co)&gt;.

# SECURITY CONSIDERATIONS

**dedup**
makes a best effort attempt to replace original files with clones that have
the same permissions, metadata, and ACLs. Support for copying metadata comes
from
[`copyfile(3)`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/copyfile.3.html)

`dedup` shouldn't be used on directories or files where files are actively being
modified. In it's current implementation `dedup` doesn't lock any files which
causes several race conditions if underlying files are being modified. If a file
acting as a clone source or target is modified between any of the following
events, a file may be replaced by something that resembles it's previous state.

1. File metadata retrieval and comparison to previously seen files
2. Creation of a clone from a source
3. Application of file metadata from the target
4. Replacing the target with the clone

For example, if file *a* is seen and later file *b* is found to be a match, *a*
may be changed, causing *b* to be cloned to its new content. Likewise, file *b*
may be changed and then overwritten by a clone with it's previous content.

It may be reasonable for future versions to include additional checks and locks
to ensure modifications are detected prior to clone replacement.

# HISTORY

If the author was more clever, he might have named this program
[`avril`](https://www.google.com/search?q=replaced+with+a+clone).

# INSTALLATION

Download the [latest release](https://github.com/ttkb-oss/dedup/releases/latest)
and extract the files in the `$PREFIX` of your choice.

*or*

Build a copy locally

```
git clone https://github.com/ttkb-oss/dedup.git
cd dedup
make && sudo make install
```

Packages for package managers will be provided in the future.

# DEVELOPMENT

Up to this point development has only been done on macOS 13, but all APIs used
were available in macOS 12, so it may work there as well.

For building the app, a recent version of [Xcode](https://developer.apple.com/xcode/)
is required along with Command Line Tools (`xcode-select --install`).

[Check](https://libcheck.github.io/check/) is used for testing.

[Aspell](http://aspell.net) is used for spell checking.

[LCOV](https://github.com/linux-test-project/lcov) is used for reporting test coverage.

[MacPorts](https://www.macports.org) users can

```bash
sudo port install lcov aspell check
```

Homebrew users should install things the way they choose to. They may need to
add header and library search paths when running the `check` target (or any
of the targets that compile things in `test/Makefile`.

```bash
CFLAGS='-I/usr/local/include' LDFLAGS='-L/usr/local/lib' make check
```

# CONTRIBUTING

Feel free to send a PR for build, code, test, or documentation changes. If the
PR is approved, you will be asked to assign copyright of the submitted code.

# FAQ

## How does it work?

A directory contains files "a", "b", and "c". "a" and "b" are links to the same
inode which points to a data block containing "hello". "c" is a unique link to
a different inode pointing to a different data block which also contains
"hello".

    dir    ╔═══════╗   ╔═════════╗
    ⎺⎺⎺┌──▶║ ino 1 ║──▶║ "hello" ║
    a ─┘┌─▶╚═══════╝   ╚═════════╝
    b ──┘  ╔═══════╗   ╔═════════╗
    c ────▶║ ino 2 ║──▶║ "hello" ║
           ╚═══════╝   ╚═════════╝

When **dedup** is run on `dir`, "c"'s is updated to point to a new inode that
shares its data blocks with the inode which "a" and "c" are linked.

    dir    ╔═══════╗   ╔═════════╗
    ⎺⎺⎺┌──▶║ ino 1 ║──▶║ "hello" ║
    a ─┘┌─▶╚═══════╝   ╚═════════╝
    b ──┘  ╔═══════╗        ▲
    c ────▶║ ino 3 ║────────┘
           ╚═══════╝

If the data is modified using the "a" or "b" link (file name), the data block
shared with "c" will be disconnected from their "inode" and a new block (or
blocks) will be created.

                        ╔═════════╗
                     ┌─▶║ "world" ║
                     │  ╚═════════╝
    dir    ╔═══════╗ │ ╔═════════╗
    ⎺⎺⎺┌──▶║ ino 1 ║─┘ ║ "hello" ║
    a ─┘┌─▶╚═══════╝   ╚═════════╝
    b ──┘  ╔═══════╗        ▲
    c ────▶║ ino 3 ║────────┘
           ╚═══════╝

Likewise, if the data is modified using the "c" link,
"a" and "b" will continue to point to the original data block and "c" will
now point to a newly created data block.

    dir    ╔═══════╗   ╔═════════╗
    ⎺⎺⎺┌──▶║ ino 1 ║──▶║ "hello" ║
    a ─┘┌─▶╚═══════╝   ╚═════════╝
    b ──┘  ╔═══════╗   ╔═════════╗
    c ────▶║ ino 3 ║──▶║ "world" ║
           ╚═══════╝   ╚═════════╝

## How Can it be Checked?

If you run **dedup** again on the same directory tree multiple times, it will
output the amount of space that is already saved by clones and hard links.

A [patched version of du(1)](https://github.com/hohle/file_cmds/commit/6fd06e315b6213aa55516f5507cf60a869c0d599)
will also ignore clones it encounters multiple times just like hard links
to the same inode are ignored. The the patched `du` will display smaller
block usage if data can be deduplicated.

If you want to ensure a directory contains the same content before and after
running **dedup**, you can create a checksum file and validate it immediately
after cloning:

```bash
TARGET_TREE=some/path
find "$TARGET_TREE" -type f -print0 | xargs -0 -n1 shasum -a 256 > original.sha256
dedup "$TARGET_TREE"
shasum -c original.sha256 | grep -v ': OK$'
```

The final command should output nothing (`$?` will be `1` because `grep`
selects no lines).

## Are Any Others Operating Systems Supported?

`dedup` leverages both file system support for creating clones as well as the
appropriate system calls. [OpenZFS support for sharing blocks](https://github.com/openzfs/zfs/pull/13392)
may make FreeBSD support possible in the future.

## Why Aren't HFS Compressed Files Cloned?

APFS supports HFS compression just as any file system with xattrs and support
for flags would. Howver, HFS compression does not store data in a file's data
fork, but in a file's xattrs. Since APFS clones data blocks, not file metadata,
there's nothing to share between clones of HFS compressed files.

HFS transparent compression uses a file's resource fork (the
`com.apple.ResourceFork` xattr) along with a `com.apple.decmps` xattr
describing compression details (algorithm, original file size), and the
`UF_COMPRESSED` flag. There is no data in any data blocks (né data fork).
