/* mgensys: Help data and printout code.
 * Copyright (c) 2020-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#include "common.h"
#include <stdio.h>

/* Macro used to declare and define help sets of items. */
#define MGS_HELP__ITEMS(X) \
	X(help, Help) \
	X(line, Line) \
	X(noise, Noise) \
	X(wave, Wave) \
	//
#define MGS_HELP__X_ID(NAME, ARRAY) MGS_HELP_N_##NAME,
#define MGS_HELP__X_NAME(NAME, ARRAY) #NAME,
#define MGS_HELP__X_ARRAY(NAME, ARRAY) mgs##ARRAY##_names

/**
 * Named help types.
 */
enum {
	MGS_HELP__ITEMS(MGS_HELP__X_ID)
	MGS_HELP_NAMED
};

/** Names of help types, with an extra NULL pointer at the end. */
extern const char *const mgsHelp_names[MGS_HELP_NAMED + 1];

const char *const *mgs_find_help(const char *restrict str);

/*
 * Name array functions of more general use.
 */

bool mgs_find_name(const char *const *restrict namearr,
		const char *restrict str, size_t *restrict id);
bool mgs_print_names(const char *const *restrict namearr,
		const char *restrict headstr,
		FILE *restrict out);
