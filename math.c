/* saugns: Math definitions.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#include "math.h"
#include <time.h>

const char *const SAU_Math_names[SAU_MATH_NAMED + 1] = {
	SAU_MATH__ITEMS(SAU_MATH__X_NAME)
	NULL
};

const uint8_t SAU_Math_params[SAU_MATH_NAMED] = {
	SAU_MATH__ITEMS(SAU_MATH__X_PARAMS)
};

static double mf_const(void) { return SAU_HUMMID; }
static double pi_const(void) { return SAU_PI; }

static double SAU_rand(struct SAU_Math_state *restrict o) {
	return SAU_d01_from_ui64(SAU_splitmix64_next(&o->seed));
}

static double SAU_seed(struct SAU_Math_state *restrict o, double x) {
	union { double d; uint64_t ui64; } v;
	v.d = x;
	o->seed = v.ui64;
	return 0.f;
}

static double SAU_time(struct SAU_Math_state *restrict o) {
	(void) o;
	/*
	 * Before converting time value to double,
	 * ensure it's not too large to preserve a
	 * difference from one second to the next.
	 * (Priority is usefulness as seed value.)
	 */
	return (double) (((int64_t) time(NULL)) & ((INT64_C(1)<<53) - 1));
}

const union SAU_Math_sym_f SAU_Math_symbols[SAU_MATH_NAMED] = {
	SAU_MATH__ITEMS(SAU_MATH__X_SYM_F)
};
