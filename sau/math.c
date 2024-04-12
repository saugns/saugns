/* SAU library: Math definitions.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include <sau/math.h>
#include <time.h>

const char *const sauMath_names[SAU_MATH_NAMED + 1] = {
	SAU_MATH__ITEMS(SAU_MATH__X_NAME) NULL
};
const char *const sauMath_vars_names[SAU_MATH_VARS_NAMED + 1] = {
	SAU_MATH__VARS_ITEMS(SAU_MATH__X_NAME) NULL
};

const uint8_t sauMath_params[SAU_MATH_NAMED] = {
	SAU_MATH__ITEMS(SAU_MATH__X_PARAMS)
};

static double mf_const(void) { return SAU_HUMMID; }
static double pi_const(void) { return SAU_PI; }

static double sau_rand(struct sauMath_state *restrict o) {
	return sau_d01_from_ui64(sau_splitmix64_next(&o->seed64));
}

static double sau_seed(struct sauMath_state *restrict o, double x) {
	union { double d; uint64_t ui64; } v;
	v.d = x;
	o->seed64 = v.ui64;
	o->seed32 = (o->seed64 >> 32) + o->seed64;
	return 0.f;
}

static double sau_seed_old(struct sauMath_state *restrict o, double x) {
	sau_warning("math", "seed() is deprecated, use \"$seed=...\"");
	return sau_seed(o, x);
}

static double sau_time(struct sauMath_state *restrict o) {
	if (o->no_time)
		return 0.0;
	/*
	 * Before converting time value to double,
	 * ensure it's not too large to preserve a
	 * difference from one second to the next.
	 * (Priority is usefulness as seed value.)
	 */
	return (double) (((int64_t) time(NULL)) & ((INT64_C(1)<<53) - 1));
}

const union sauMath_sym_f sauMath_symbols[SAU_MATH_NAMED] = {
	SAU_MATH__ITEMS(SAU_MATH__X_SYM_F)
};
const sauMath_vars_sym_f sauMath_vars_symbols[SAU_MATH_VARS_NAMED] = {
	SAU_MATH__VARS_ITEMS(SAU_MATH__X_VARS_SYM_F)
};
