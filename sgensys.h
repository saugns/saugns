/* sgensys: Common definitions.
 * Copyright (c) 2011-2012, 2018-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
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

/* Program name string, for cli printouts. */
#define SGS_CLINAME_STR "sgensys"

/* Version printout string, for -v option. */
#define SGS_VERSION_STR "v0.2-beta"

/* Default sample rate, see -r option. */
#define SGS_DEFAULT_SRATE 96000

/*
 * Basic types.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Keyword-like macros.
 */

#if defined(__GNUC__) || defined(__clang__)
# define sgsMalloclike __attribute__((malloc))
# define sgsMaybeUnused __attribute__((unused))
# define sgsNoinline __attribute__((noinline))
# define sgsPrintflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define sgsMalloclike
# define sgsMaybeUnused
# define sgsNoinline
# define sgsPrintflike(string_index, first_to_check)
#endif

/*
 * Utility macros.
 */

/** Turn \p arg into string literal before macro-expanding it. */
#define SGS_STRLIT(arg) #arg

/** Turn \p arg into string literal after macro-expanding it. */
#define SGS_STREXP(arg) SGS_STRLIT(arg)

/** Is \p c a visible non-whitespace 7-bit ASCII character? */
#define SGS_IS_ASCIIVISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Utility functions.
 */

void SGS_warning(const char *restrict label, const char *restrict fmt, ...)
	sgsPrintflike(2, 3);
void SGS_error(const char *restrict label, const char *restrict fmt, ...)
	sgsPrintflike(2, 3);

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define SGS_MEM_DEBUG 1

/* Print hash collision info for symtab. */
//#define SGS_SYMTAB_STATS 0

/* Run scanner instead of lexer in 'test-scan' program. */
#define SGS_TEST_SCANNER 0

/* Make test lexer quiet enough to time it. */
#define SGS_LEXER_QUIET 1
