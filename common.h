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
#define MGS_CAT(_0, _1) _0 ## _1

/** Concatenate the text of two arguments after macro-expanding them. */
#define MGS_PASTE(_0, _1) MGS_CAT(_0, _1)

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
 * This version only works with fewer than 32 arguments.
 */
#define MGS_HAS_COMMA(...) MGS__ARG32(__VA_ARGS__, \
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, )
#define MGS__ARG32(_0, \
 _1, _2, _3, _4, _5, _6, _7, _8, _9,_10,_11,_12,_13,_14,_15,_16, \
_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31, ...) _31

/** Return \p cond if not empty, otherwise return \p repl. */
#define MGS_NONEMPTY_OR(cond, repl) \
	MGS_PASTE(MGS__NONEMPTY_OR, MGS_IS_EMPTY(cond))(cond, repl)
#define MGS__NONEMPTY_OR0(cond, repl) cond
#define MGS__NONEMPTY_OR1(cond, repl) repl

/**
 * C99-compatible empty macro arguments detection,
 * using Jens Gustedt's technique.
 */
#define MGS_IS_EMPTY(...) \
/* test for empty argument in four ways, then produce 1 or 0   */       \
MGS__IS_EMPTY( \
	/* if argument has comma (not just one arg, empty or not)? */   \
	MGS_HAS_COMMA(__VA_ARGS__),                                     \
	/* if _TRIGGER_PARENTHESIS_ and argument adds a comma? */       \
	MGS_HAS_COMMA(MGS__TRIGGER_PARENTHESIS_ __VA_ARGS__),           \
	/* if the argument and a parenthesis adds a comma? */           \
	MGS_HAS_COMMA(__VA_ARGS__ (/*empty*/)),                         \
	/* if placed between _TRIGGER_PARENTHESIS_ and parentheses? */  \
	MGS_HAS_COMMA(MGS__TRIGGER_PARENTHESIS_ __VA_ARGS__ (/*empty*/))\
)
#define MGS__IS_EMPTY_CASE_0001 ,
#define MGS_CAT5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define MGS__IS_EMPTY(_0, _1, _2, _3) \
	MGS_HAS_COMMA(MGS_CAT5(MGS__IS_EMPTY_CASE_, _0, _1, _2, _3))
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
