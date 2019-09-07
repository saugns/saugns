Contributing
============

Bug reports are very welcome, with or without fixes. Many bugs
could have been fixed much sooner if only they had been found earlier.

General feedback, ideas, and proposed changes are also welcome. I'm
open to extending and reworking the SAU language, though features and
computational complexity will always be limited.

You can open an issue on Codeberg, or alternatively Framagit or GitHub,
or send an email to <joelkp@tuta.io>.

There's no specific conduct rules yet, because there's so little
outside contribution there's yet to be any discussion on such.
Just make a good-faith effort to be constructive.

Source branches are frequently rebased in this project, but handling
conflicts from that (unless they're huge) can be done for you. Make a
new branch available with your contributions for review if you have
some code ready, and if approved it can be integrated.

How to test saugns in development
---------------------------------

The main `Makefile` contains targets useful for quick tests.
* `make check ARGS=-p > X` after a clean build makes a file `X`
  which contains `-p` printouts for `examples/` and `devtests/`.
  Making another file `Y` after changes to parsing, etc. in
  libsau, and diffing the two is often a good sanity check.
* `make fullcheck ARGS="-r6000 -o0.wav"` can allow fuller tests
  after changes and rebuilds for audio rendering etc. Change the
  filename and diff the WAV files, and for many types of changes
  they should be identical. For other changes, and when using
  different compilers etc., given float optimizations files may
  differ. Yet `sox` can be used to diff spectrums for files then
  and a blank or really quiet result is good. This kind of test
  requires the sample-by-sample timing to be unaltered, though.
  A reduced sample rate (e.g. 6000 Hz) reduces file sizes while
  the diff-test still covers all the logic and float math.

The tests in `examples/` and `devtests/` are currently not
fully exhaustive in the features they use. Initially they
grew in part for debugging purposes, though later on,
new features almost always have accompanying scripts.

The `-p` printout format and info for scripts initially
developed for debugging purposes. It does not include all
information, and may change further if/as needed.

After installing `sox` and `feh` (image viewer, other commands
could also be used), the following script can be used to make a
`soxdiff` command for comparing two audio files, e.g. WAV files.
This diffing assumes the files have the same timing in samples,
and the same timing logic prior to the float math should make it
so for identical scripts. Contents are compared by subtraction.
```
#/bin/sh
sox -m -v 1 $1 -v -1 $2 -n spectrogram -o - | feh -
```
