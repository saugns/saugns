**saugns** is the Scriptable AUdio GeNeration System,
the implementation of the **SAU language** (Scriptable AUdio).
[The project website](https://sau.frama.io/) has more on them.

SAU is a simple language for mathematical audio synthesis,
without support for using pre-recorded samples.
See the [README.SAU](sau/doc/README.SAU) for the current details,
or a more how-to [language web page](https://sau.frama.io/language.html)
covering the main features.

While the language is still primitive relative to the
goal (a useful language for writing electronic music),
it makes it simple to experiment with sounds and soundscapes.
An arbitrary number of audio generators of the types provided
can be used, and combined with various types of modulation.

The program reads SAU (Scriptable AUdio) files or strings,
and can output to system audio, a 16-bit PCM WAV file,
and/or stdout (raw or AU, for interfacing with other programs).
Basic usage information is provided with the `-h` option. More
can be found in the man page and on the
[usage web page](https://sau.frama.io/usage.html).

Tested mainly on x86 and x86-64. Comes with support for
running on DragonFly, FreeBSD, Linux, NetBSD, and OpenBSD.
The following audio systems are expected:

| OS        | Supported APIs  |
| -         | -               |
| DragonFly | OSS             |
| FreeBSD   | OSS             |
| Linux     | ALSA, OSS       |
| NetBSD    | OSS             |
| OpenBSD   | sndio           |

See [CONTRIBUTING.md](CONTRIBUTING.md) if you're interested in contributing
to this project. It also has pointers on testing after making changes.

Examples
========

The example scripts under `examples/` use most features; beyond playing them,
a Makefile in that directory allows bulk-rendering (by default to MP3s) using
the SoX utility in addition to saugns. That Makefile is provided to make such
things easier for your own scripts (the Makefile can be used for other paths,
having a `DIRS` option). Apart from the `DIRS` default, it is the same as the
Makefile provided in [extra-scripts](https://codeberg.org/sau/extra-scripts).

A limited set of scripts with pre-rendered audio is on the
[examples web page](https://sau.frama.io/examples.html).

Building and installing
=======================

Building requires a C99 compiler toolchain, running `make`,
and having some GNU or BSD tools. There is no "configure" step.

On Linux distributions, the ALSA library (libasound2) or a '-dev' package
or similar for it may possibly need to be installed for building to work.
In the cases of the 4 major BSDs, the base systems have it all.

A simple test after building is the following, which should
play a sine wave at 440 Hz for 1 second: `./saugns -e "Wsin"`.

Running `make install` will by default copy `saugns` to `/usr/local/bin/`,
and the contents of `doc/` and `examples/` to
directories under `/usr/local/share/`. The BSDism is to place examples
under a "share/examples/" directory, but such are often missing on Linux.

| Files under share/      | Description                                     |
| -                       | -                                               |
| doc/saugns/CHANGELOG.md | Version history.                                |
| doc/saugns/README.md    | This file.                                      |
| doc/saugns/README.SAU   | SAU language reference.                         |
| examples/saugns/        | Example scripts, *if "share/examples/" exists*. |
| saugns/examples/        | Example scripts, *the fallback location*.       |

A `make uninstall` removes the added saugns binary and share/ subdirectories.
It's recommended before installing a new version, for a consistent file set.

After installation, `man saugns` should give basic usage information and
point to the share/ files. Without installing, try `man ./man/saugns.1`.

Licensing
=========

saugns is Copyright (c) 2011-2014, 2017-2024 Joel K. Pettersson.
As a whole it is distributed under the terms of the GNU Lesser General
Public License (LGPL), version 3 or later. See the files
[COPYING.LESSER](COPYING.LESSER) and [COPYING](COPYING) for
details, or <https://www.gnu.org/licenses/lgpl-3.0.en.html>.

All the source files outside the `sau/` library directory (and
some inside) are licensed under more permissive terms, such as
the ISC license (2-clause-BSD-equivalent), or
the 0BSD license (public-domain-equivalent shorter version);
see the heading comment text of source files.

The example and test scripts included as input for the program
are (unlike other potential works) meant for study and copying
from. Feel free to copy from them for your own scripts without
any attribution or licensing text. If that's not enough in the
future, WTFPL and/or a Creative Commons license could be used.

Documentation
-------------

Included documentation files (man page, this and others placed under `doc/`)
are written by Joel K. Pettersson and licensed under [Creative Commons
Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
These files are maintained and provided in parallel
with the [website](https://sau.frama.io).
