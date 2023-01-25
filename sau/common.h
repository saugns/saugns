/* SAU library: Common definitions.
 * Copyright (c) 2011-2012, 2018-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
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

/* Version printout string, for -v option. */
#define SAU_VERSION_STR "v0.2-dev"

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
# define sauMalloclike __attribute__((malloc))
# define sauMaybeUnused __attribute__((unused))
# define sauNoinline __attribute__((noinline))
# define sauPrintflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define sauMalloclike
# define sauMaybeUnused
# define sauNoinline
# define sauPrintflike(string_index, first_to_check)
#endif

/*
 * Utility macros.
 */

/** Turn \p arg into string literal before macro-expanding it. */
#define SAU_STRLIT(arg) #arg

/** Turn \p arg into string literal after macro-expanding it. */
#define SAU_STREXP(arg) SAU_STRLIT(arg)

/** Is \p c a visible non-whitespace 7-bit ASCII character? */
#define SAU_IS_ASCIIVISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Utility functions.
 */

extern int sau_stdout_busy;

/** Return stream to use for printing when stdout is preferred. */
#define sau_print_stream() (sau_stdout_busy ? stderr : stdout)

int sau_printf(const char *restrict fmt, ...)
	sauPrintflike(1, 2);
void sau_warning(const char *restrict label, const char *restrict fmt, ...)
	sauPrintflike(2, 3);
void sau_error(const char *restrict label, const char *restrict fmt, ...)
	sauPrintflike(2, 3);

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define SAU_MEM_DEBUG 1

/* Print hash collision info for symtab. */
//#define SAU_SYMTAB_STATS 0

/* Print test statistics for scanner. */
#define SAU_SCANNER_STATS 0

/* Make test lexer quiet enough to time it. */
#define SAU_LEXER_QUIET 1
