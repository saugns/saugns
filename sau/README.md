libsau -- the SAU (Scriptable AUdio) library
--------------------------------------------

This is the static C library that comes with the 'saugns' program,
and which implements support for the SAU audio scripting language.
[The project website](https://sau.frama.io/) has more on all this.

There's Makefile targets for installing and uninstalling it,
by default under `/usr/local/`. This is not done by 'saugns'
which just links to it and has its own Makefile. However, it
can be used for other software which doesn't include it all.

Licensing
=========

The SAU library is Copyright (c) 2011-2014, 2017-2024 Joel K. Pettersson.
As a whole it is distributed under the terms of the GNU Lesser General
Public License (LGPL), version 3 or later. See the files
[COPYING.LESSER](COPYING.LESSER) and [COPYING](COPYING) for
details, or <https://www.gnu.org/licenses/lgpl-3.0.en.html>.

Some files are licensed under more permissive terms, such as
the ISC license (2-clause-BSD-equivalent), or
the 0BSD license (public-domain-equivalent shorter version);
see the heading comment text of source files.

*Permissive licensing is mainly used for general utility and
supporting portability code. You can however also request it
for smaller key parts of audio and other components, for use
in your permissively licensed non-profit software projects.*

Documentation
-------------

Included documentation files (this and others placed under `doc/`)
are written by Joel K. Pettersson and licensed under [Creative Commons
Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
These files are maintained and provided in parallel
with the [website](https://sau.frama.io).
