/* sgensys: Common definitions.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
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
 * Keyword-like macros and compiler-dependent definitions.
 */

#if defined(__GNUC__) || defined(__clang__)
# define SGS__format(archetype, string_index, first_to_check) \
	__attribute__((format(archetype, string_index, first_to_check)))
# define SGS__malloc __attribute__((malloc))
# define SGS__maybe_unused __attribute__((unused))
#else
# define SGS__format(archetype, string_index, first_to_check)
# define SGS__malloc
# define SGS__maybe_unused
#endif

/*
 * Utility functions.
 */

void SGS_warning(const char *label, const char *fmt, ...)
	SGS__format(printf, 2, 3);
void SGS_error(const char *label, const char *fmt, ...)
	SGS__format(printf, 2, 3);

void *SGS_memdup(const void *src, size_t size) SGS__malloc;
char *SGS_strdup(const char *src) SGS__malloc;

/*
 * Debugging options.
 */

#define SGS_DEBUG_PRINT_PROGRAM 1

/* Disable old parser, run test code instead. */
#define SGS_TEST_LEXER 0

#define SGS_HASHTAB_STATS 0
#define SGS_LEXER_QUIET 0
