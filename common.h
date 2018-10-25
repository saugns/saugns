/* saugns: Common definitions.
 * Copyright (c) 2011-2012, 2018-2019 Joel K. Pettersson
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
 * Utility functions.
 */

void SAU_warning(const char *restrict label, const char *restrict fmt, ...)
	SAU__printflike(2, 3);
void SAU_error(const char *restrict label, const char *restrict fmt, ...)
	SAU__printflike(2, 3);

void *SAU_memdup(const void *restrict src, size_t size) SAU__malloclike;

/*
 * Debugging options.
 */

/* Run scanner instead of lexer in 'test-builder' program. */
#define SAU_TEST_SCANNER 0
/* Print test statistics for scanner. */
#define SAU_SCANNER_STATS 0
/* Print hash collision info for symtab. */
#define SAU_HASHTAB_STATS 0
/* Make test lexer quiet enough to time it. */
#define SAU_LEXER_QUIET 1
