/* mgensys: Common definitions.
 * Copyright (c) 2011-2012, 2019-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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
 * Utility functions.
 */

void MGS_warning(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);
void MGS_error(const char *restrict label, const char *restrict fmt, ...)
	mgsPrintflike(2, 3);

void *MGS_memdup(const void *restrict src, size_t size) mgsMalloclike;
