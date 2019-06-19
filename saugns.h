/* saugns: Main header and definitions for cli programs.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <sau/common.h>

/* Program name string, for cli printouts. */
#define CLINAME_STR "saugns"

/* Version printout string, for -v option. */
#define VERSION_STR "v0.3-dev"

/*
 * Configuration.
 */

/* Default sample rate, see -r option. */
#define DEFAULT_SRATE 96000

/* Run scanner instead of lexer in 'test-scan' program. */
#define TEST_SCANNER 0

/* Add int SGS_testopt and "-? <number>" cli option for it? */
//#define SGS_ADD_TESTOPT 1

#if SGS_ADD_TESTOPT
extern int SGS_testopt; /* defaults to 0, set using debug option "-?" */
#endif
