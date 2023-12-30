/* saugns: Main header and definitions for cli programs.
 * Copyright (c) 2011-2013, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once
#include <sau/common.h>

/* Program name string, for cli printouts. */
#define CLINAME_STR "saugns"

/* Version printout string, for -v option. */
#define VERSION_STR "v0.4.2d"

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
