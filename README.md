# `dedup(1)`

Replace duplicate file data with a copy-on-write clone.

# SYNOPSIS

**dedup** `[-PVcnvx]` [`-t`&nbsp;**threads**] [`-d`&nbsp;**depth**] [*file&nbsp;...*]

# DESCRIPTION

**dedup** finds files with identical content in
*file ...* and replaces the data on disk of a duplicate with a clone (using
clonefile(2)) of the other. If no files is specified the current directory is used. The
cloned file shares its data blocks with the original file. Unlike a hardlinked file,
any future writes to either the clone or the original file will be remain private to
that file.

**dedup** works in two phases. First it evaluates all of the paths provided recursively
looking for duplicates. Once all duplicates are found, any files that are not
already clones of best clone source are replaced with clones.

There are limits to the files that can be cloned:

1.	the file must be a regular file

2.	the file must have only one hardlink

3.	the file and its directory must be writable by the user

To determine the best source for clones is chosen by finding the file with the
most hardlinks. If all files have a single link, a file which shares the most
clones with others is chosen. If none of the files have clones, the first file
encountered will be chosen. The file with the most hardlinks is prioritized due
to the likelihood of its data continuing to exist even if the file is deleted.
A file with the most clones is prioritized to reduce the number of files to be
cloned and to provided some determinism when running over a directory multiple
times; most previously cloned files should remain untouched and new files will
be assimilated into the shared data set.

Files with multiple hardlinks are not replaced because it is not possible to
gaurantee all other links to that inode exist within the tree(s) being evaluated.
Future versions of
**dedup**
may consider cloning files with multiple hardlinks if all links are in the tree.
Replacing a link with a clone changes the semantics from two link pointing at
the same, mutable shared storage to two links pointing at the same copy-on-write
storage. For scenarios where hardlinks were previously being used because
clones were not available, future versions may provide a flag to destructively
replace hardlinks with clones.

**dedup**
will only work on volumes that have the
*VOL\_CAP\_INT\_CLONE*
capability. Currently that is limited to APFS.

# OPTIONS

The following options are available:

**-c**, **-&#45;color**

> Use ANSI colors when displaying output.

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

> The number of threads to use for evaluating files. By default thi is the same
> as the number of CPUs on the host as described by the
> *hw.ncpu*
> value returned by
> sysctl(8).
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

# ENVIRONMENT

If the environment variables
`CLICOLOR`
or
`COLORTERM`
are set while running in an interactive terminal,
`ANSI`
color sequences will be used.

# EXIT STATUS

The
**dedup**
utility exits 0 and &gt;0 if an error occurs.

It is possible that during abnormal termination a temporary clone will be left
before it is moved to the path it is replacing. In such cases a file with the
prefix .~. followed by the name of the file that was to be replaced will exist
in the same directory as target file.

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