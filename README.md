saugns is the Scriptable AUdio GeNeration System,
the implementation of the SAU (Scriptable AUdio) language.

SAU is a simple language for mathematical audio synthesis,
without support for the use of pre-recorded samples.
See [doc/README.SAU](doc/README.SAU) for a concise SAU language reference,
or the more how-to [web page on it](https://sau.frama.io/language.html).
Example scripts in examples/ use the main features of
the language.

While the language is still primitive relative to the
goal (a useful language for writing electronic music),
it makes it simple to experiment with sounds.
A collection of basic wave types are supported, as well
as AM/RM, FM, and PM (the "FM" of most commercial synthesizers).
An arbitrary number of oscillators can be used.

The program reads SAU (Scriptable AUdio) files or strings,
and can output to system audio and/or a 16-bit PCM WAV file.
Basic usage information is provided with the -h option. More
can be found in the man page and on the website,
<https://sau.frama.io/>.

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

Building and installing
=======================

Building requires a C99 compiler toolchain and
running `make` (GNU or BSD). (There is no "configure" step.)

On Linux systems, the ALSA library (libasound2) or a '-dev' package
for it may possibly need to be installed for building to work.
In the cases of the 4 major BSDs, the base systems have it all.

A simple test after building is the following, which should
play a sine wave at 440 Hz for 1 second: `./saugns -e "Osin"`.

Running `make install` will by default copy 'saugns' to '/usr/local/bin/',
and the contents of 'doc/' and 'examples/' to
directories under '/usr/local/share/'.

| Files under share/    | Description               |
| -                     | -                         |
| doc/saugns/README.md  | This file.                |
| doc/saugns/README.SAU | SAU language reference.   |
| examples/saugns/      | Example scripts.          |

A `make uninstall` removes the added saugns binary and share/ subdirectories.
It's recommended before installing a new version, for a consistent file set.

After installation, `man saugns` should give basic usage information and
point to the share/ files. Without installing, try `man man/saugns.1`.

Licensing
=========

saugns is Copyright (c) 2011-2014, 2017-2022 Joel K. Pettersson.
As a whole it is distributed under the terms of the GNU Lesser General
Public License (LGPL), version 3 or later. See the file [COPYING](COPYING)
for details, or <https://www.gnu.org/licenses/>.

Some files are licensed under more permissive terms, such as
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

Contributing
============

Bug reports are very welcome, with or without fixes. Many bugs
could have been fixed much sooner if only they had been found earlier.

General feedback, ideas, and proposed changes are also welcome. I'm
open to extending and reworking the SAU language, though features will
always be limited. Most valuable are those things that may provide the
most with the least. I'm looking to increase flexibility and elegance,
before or after adding new components.
