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

/** Concatenate the text of two arguments before macro-expanding them. */
#define MGS_CONCAT(_0, _1) _0 ## _1

/** Concatenate the text of two arguments after macro-expanding them. */
#define MGS_PASTE(_0, _1) MGS_CONCAT(_0, _1)

/** Return all arguments. Can be used to remove parentheses around a list. */
#define MGS_ARGS(...) __VA_ARGS__

/** Return the first of one or more macro arguments. */
#define MGS_ARG1(...) MGS__ARG1(__VA_ARGS__, )
#define MGS__ARG1(head, ...) head

/** Return arguments after the first macro argument. */
#define MGS_ARGS_TAIL(...) \
	MGS_PASTE(MGS__ARGS_TAIL, MGS_HAS_COMMA(__VA_ARGS__))(__VA_ARGS__)
#define MGS__ARGS_TAIL0(head)
#define MGS__ARGS_TAIL1(head, ...) __VA_ARGS__

/** Substitute the first item in a list of items. */
#define MGS_SUBST_HEAD(head, list) \
	(head MGS_PASTE(MGS__SUBST_HEAD, MGS_HAS_COMMA list)list)
#define MGS__SUBST_HEAD0(head)
#define MGS__SUBST_HEAD1(head, ...) , __VA_ARGS__

/**
 * Check whether arguments include at least one comma,
 * i.e. there's more than one argument (blank or not).
 *
 * This version only works with fewer than 16 arguments.
 */
#define MGS_HAS_COMMA(...) MGS__ARG16(__VA_ARGS__, \
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, )
#define MGS__ARG16(_0, _1, _2, _3, _4, _5, _6, _7, \
		_8, _9, _10, _11, _12, _13, _14, _15, ...) _15

/**
 * C99-compatible macro returning a comma if the argument list is not empty.
 * Derived from Jens Gustedt's empty macro arguments detection.
 */
#define MGS_COMMA_ON_ARGS(...) \
/* test for empty argument in four ways, then invert result... */       \
MGS__COMMA_ON_ARGS( \
	/* if argument has comma (not just one arg, empty or not)? */   \
	MGS_HAS_COMMA(__VA_ARGS__),                                     \
	/* if _TRIGGER_PARENTHESIS_ and argument adds a comma? */       \
	MGS_HAS_COMMA(MGS__TRIGGER_PARENTHESIS_ __VA_ARGS__),           \
	/* if the argument and a parenthesis adds a comma? */           \
	MGS_HAS_COMMA(__VA_ARGS__ (/*empty*/)),                         \
	/* if placed between _TRIGGER_PARENTHESIS_ and parentheses? */  \
	MGS_HAS_COMMA(MGS__TRIGGER_PARENTHESIS_ __VA_ARGS__ (/*empty*/))\
)
#define MGS__ARG3(_0, _1, _2, ...) _2
#define MGS__IS_EMPTY_CASE_0001 ,
#define MGS_CONCAT5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define MGS__COMMA ,
#define MGS__INVERT_COMMA(...) MGS__ARG3(__VA_ARGS__, , MGS__COMMA, )
#define MGS__COMMA_ON_ARGS(_0, _1, _2, _3) \
	MGS__INVERT_COMMA(MGS_CONCAT5(MGS__IS_EMPTY_CASE_, _0, _1, _2, _3))
#define MGS__TRIGGER_PARENTHESIS_(...) ,

/** Is \p c a visible non-whitespace 7-bit ASCII character? */
#define MGS_IS_ASCIIVISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Utility functions.
 */

extern int mgs_stdout_busy;

/** Return stream to use for printing when stdout is preferred. */
#define mgs_print_stream() (mgs_stdout_busy ? stderr : stdout)

int mgs_printf(const char *restrict fmt, ...)
	mgsPrintflike(1, 2);
void mgs_warning(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);
void mgs_error(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);
void mgs_fatal(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);

void *mgs_memdup(const void *restrict src, size_t size) mgsMalloclike;

/** mgs_getopt() data. Initialize to zero, except \a err for error messages. */
struct mgsOpt {
	int ind; /* set to zero to start over next mgs_getopt() call */
	int err;
	int pos;
	int opt;
	const char *arg;
};
int mgs_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct mgsOpt *restrict opt);

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define MGS_MEM_DEBUG 1

/* Print hash collision info for symtab. */
//#define MGS_SYMTAB_STATS 0

/* Add int mgs_testopt and "-? <number>" cli option for it? */
//#define MGS_ADD_TESTOPT 1
