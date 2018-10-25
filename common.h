/* ssndgen: Common definitions.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
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

typedef unsigned int uint;

/*
 * Keyword-like macros.
 */

#if defined(__GNUC__) || defined(__clang__)
# define SSG__malloclike __attribute__((malloc))
# define SSG__maybe_unused __attribute__((unused))
# define SSG__printflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define SSG__malloclike
# define SSG__maybe_unused
# define SSG__printflike(string_index, first_to_check)
#endif

/*
 * Utility functions.
 */

void SSG_warning(const char *label, const char *fmt, ...)
	SSG__printflike(2, 3);
void SSG_error(const char *label, const char *fmt, ...)
	SSG__printflike(2, 3);

void *SSG_memdup(const void *src, size_t size) SSG__malloclike;

/*
 * Debugging options.
 */

/* Disable old parser, run lexer testing instead. */
#define SSG_TEST_LEXER 0

#define SSG_HASHTAB_STATS 0
#define SSG_LEXER_QUIET 0
