/* mgensys: Common definitions.
 * Copyright (c) 2011-2012, 2019-2022 Joel K. Pettersson
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
# define mgsMalloclike __attribute__((malloc))
# define mgsMaybeUnused __attribute__((unused))
# define mgsNoinline __attribute__((noinline))
# define mgsPrintflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define mgsMalloclike
# define mgsMaybeUnused
# define mgsNoinline
# define mgsPrintflike(string_index, first_to_check)
#endif

/*
 * Utility macros.
 */

/** Turn \p arg into string literal before macro-expanding it. */
#define MGS_STRLIT(arg) #arg

/** Turn \p arg into string literal after macro-expanding it. */
#define MGS_STREXP(arg) MGS_STRLIT(arg)

/** Is \p c a visible non-whitespace 7-bit ASCII character? */
#define MGS_IS_ASCIIVISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Utility functions.
 */

extern int MGS_stdout_busy;

/** Return stream to use for printing when stdout is preferred. */
#define MGS_print_stream() (MGS_stdout_busy ? stderr : stdout)

int MGS_printf(const char *restrict fmt, ...)
	mgsPrintflike(1, 2);
void MGS_warning(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);
void MGS_error(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);

void *MGS_memdup(const void *restrict src, size_t size) mgsMalloclike;

/** MGS_getopt() data. Initialize to zero, except \a err for error messages. */
struct MGS_opt {
	int ind; /* set to zero to start over next MGS_getopt() call */
	int err;
	int pos;
	int opt;
	const char *arg;
};
int MGS_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct MGS_opt *restrict opt);

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define MGS_MEM_DEBUG 1

/* Print hash collision info for symtab. */
//#define MGS_SYMTAB_STATS 0

/* Add int MGS_testopt and "-? <number>" cli option for it? */
//#define MGS_ADD_TESTOPT 1
