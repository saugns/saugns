saugns is the Scriptable AUdio GeNeration System,
the implementation of the SAU (Scriptable AUdio) language.

SAU is a simple language for mathematical sound synthesis,
without support for the use of pre-recorded samples.
See [doc/README.SAU](doc/README.SAU) for a concise SAU language reference,
or the more how-to [web page on it](https://saugns.github.io/language.html).
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
<https://saugns.github.io/>.

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
play a sine wave at 444Hz for 1 second: `./saugns -e "Osin"`.

Running `make install` will by default copy 'saugns' to '/usr/local/bin/',
and the contents of 'doc/' and 'examples/' to
directories under '/usr/local/share/'.

| Files under share/    | Description               |
| -                     | -                         |
| doc/saugns/README.SAU | SAU language reference.   |
| examples/saugns/      | Example scripts.          |

A `make uninstall` removes the added saugns binary and share/ subdirectories.
It's recommended before installing a new version, for a consistent file set.

After installation, `man saugns` should give basic usage information and
point to the share/ files. Without installing, try `man man/saugns.1`.

Licensing
=========

saugns is distributed under the terms of the GNU Lesser General
Public License (LGPL), version 3 or later. See the file [COPYING](COPYING)
for details, or <https://www.gnu.org/licenses/>.

Some files are licensed under more permissive terms, such as
the ISC license (2-clause-BSD-equivalent), or
the 0BSD license (public-domain-equivalent shorter version);
see the heading comment text of source files.

The example and test scripts included as input for the program
currently do not have any explicit licensing. If needed in the
future, WTFPL and/or a Creative Commons license could be used.
Feel free to copy from the current files for your own scripts.
