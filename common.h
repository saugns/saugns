/* saugns: Common definitions.
 * Copyright (c) 2011-2012, 2019-2021 Joel K. Pettersson
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
# define SAU__malloclike __attribute__((malloc))
# define SAU__maybe_unused __attribute__((unused))
# define SAU__noinline __attribute__((noinline))
# define SAU__printflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define SAU__malloclike
# define SAU__maybe_unused
# define SAU__noinline
# define SAU__printflike(string_index, first_to_check)
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

void SAU_warning(const char *restrict label, const char *restrict fmt, ...)
	SAU__printflike(2, 3);
void SAU_error(const char *restrict label, const char *restrict fmt, ...)
	SAU__printflike(2, 3);

void *SAU_memdup(const void *restrict src, size_t size) SAU__malloclike;

/** SAU_getopt() data. Initialize to zero, except \a err for error messages. */
struct SAU_opt {
	int ind; /* set to zero to start over next SAU_getopt() call */
	int err;
	int pos;
	int opt;
	const char *arg;
};
int SAU_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct SAU_opt *restrict opt);

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define SAU_MEM_DEBUG 1

/* Print symbol table statistics for testing? */
//#define SAU_SYMTAB_STATS 0

/* Make test lexer quiet enough to time it. */
#define SAU_LEXER_QUIET 1

/* Run scanner directly instead of lexer in 'test-scan' program. */
#define SAU_TEST_SCANNER 0

/* Print test statistics for scanner. */
#define SAU_SCANNER_STATS 0
