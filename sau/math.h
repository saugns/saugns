/* SAU library: Math definitions.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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

#pragma once
#include "common.h"
#include <math.h>
#include <limits.h>

#define SAU_PI          3.14159265358979323846
#define SAU_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SAU_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SAU_SQRT_2      1.41421356237309504880 // sqrt(2)
#define SAU_HUMMID    632.45553203367586639978 // human hearing range geom.mean
#define SAU_GLDA        2.39996322972865332223 // golden angle 2*PI*(2.0 - phi)
#define SAU_GLDA_1_2PI  0.38196601125010515180 // (in cycle %) 2.0 - phi
#define SAU_FIBH32      0x9e3779b9             // 32-bit Fibonacci hash constant
#define SAU_FIBH64      0x9e3779b97f4a7c15     // 64-bit Fibonacci hash constant

/*
 * Format conversion & general purpose functions.
 */

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t sau_ms_in_samples(uint64_t time_ms, uint64_t srate,
		int *restrict carry) {
	uint64_t time = time_ms * srate;
	if (carry) {
		int64_t error;
		time += *carry;
		error = time % 1000;
		*carry = error;
	}
	time /= 1000;
	return time;
}

/** Apply lrint() if long can hold UINT32_MAX, otherwise llrint(). */
#define sau_ui32rint(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrint(x) : llrint(x)))

/** Apply lrintf() if long can hold UINT32_MAX, otherwise llrintf(). */
#define sau_ui32rintf(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrintf(x) : llrintf(x)))

/** Portably wrap-around behaving lrint() within 64-bit int boundaries.
    Apply lrint() if long can hold INT64_MAX, otherwise llrint(). */
#define sau_i64rint(x) ((int64_t) \
	(LONG_MAX >= INT64_MAX ? lrint(x) : llrint(x)))

/** Portably wrap-around behaving lrintf() within 64-bit int boundaries.
    Apply lrintf() if long can hold INT64_MAX, otherwise llrintf(). */
#define sau_i64rintf(x) ((int64_t) \
	(LONG_MAX >= INT64_MAX ? lrintf(x) : llrintf(x)))

/**
 * Convert cyclical value (0.0 = 0% and 1.0 = 100%, with ends
 * wrapping around) to 32-bit integer with 0 as the 0% value.
 */
static inline uint32_t sau_cyclepos_dtoui32(double x) {
	return sau_ui32rint(remainder(x, 1.f) * 0x1.0p32f);
}

/**
 * Convert fractional part of \p x into a Weyl sequence constant.
 * To be useful, \p x should usually hold some irrational number.
 */
static inline uint32_t sau_weylseq_dtoui32(double x) {
	uint32_t alpha = floor(x * 0x1.0p32f);
	return alpha | 1; // ensure odd for maximum period
}

/** Convert an unsigned 64-bit integer to 0.0 to 1.0 value. */
static inline double sau_d01_from_ui64(uint64_t x) {
	return (x >> 11) * 0x1.0p-53;
}

/** \return +1 if \p n is even, -1 if it's odd. */
static inline int sau_oddness_as_sign(int n) {
	return (1 - ((n & 1) * 2));
}

/** Portable 32-bit arithmetic right shift. */
static inline int32_t sau_sar32(int32_t x, int s) {
	return x < 0 ? ~(~x >> s) : x >> s;
}

/** 32-bit right rotation. */
static inline uint32_t sau_ror32(uint32_t x, int r) {
	return x >> r | x << (32 - r);
}

/**
 * Fold the signed 32-bit integer value if beyond half the amplitude
 * in either the positive or the negative range. Doubles the result.
 *
 * Turns a full-amplitude sawtooth wave signal into a triangle wave.
 * Replaces wrap-around discontinuities with a more mild distortion.
 *
 * \return folded value
 */
static inline int32_t sau_foldhd32(int32_t x) {
	uint32_t s = x; // unsigned to avoid C UB
	if (s + (1U<<29) > (1U<<31))
		s = (1U<<31) + (1U<<30) - s;
	s = (s - (1U<<29)) * 2;
	return s;
}

/** Pick smallest of two float values. */
static inline float sau_minf(float x, float y) {
	x = x > y ? y : x;
	return x;
}

/** Pick largest of two float values. */
static inline float sau_maxf(float x, float y) {
	x = x < y ? y : x;
	return x;
}

/** Clamp value of \p x to a float range. */
static inline float sau_fclampf(float x, float min, float max) {
	x = x < min ? min : x;
	x = x > max ? max : x;
	return x;
}

/*
 * Math functions for use in SAU scripts.
 */

/**
 * Additive recurrence base frequency. Return a multiplier from -1.0 to +1.0
 * for how much the pitch drifts when \p x is used as an additive recurrence
 * multiplier. May be negative indicating the waveform direction is flipped.
 *
 * \return multiplier for pitch
 */
static inline double sau_arbf(double x) {
	/*
	 * Same frequency for e.g. 0.1 and 0.9, but opposite direction.
	 * The factor 2 corresponds to 0.5 being the full 1x frequency.
	 */
	return remainder(x, 1.f) * -2;
}

/**
 * Additive recurrence higher frequency. Return a multiplier from -2.0 to -1.0,
 * or +1.0 to +2.0, for the first frequency above the unshifted base frequency,
 * which mirrors the value from sau_arbf() relative to +/- 1.0, with same sign.
 *
 * \return multiplier for pitch
 */
static inline double sau_arhf(double x) {
	x = remainder(x, 1.f);
	/*
	 * Invert rounded division rounding. For choices like +0.1 and -0.9,
	 * -0.1 and +0.9, pick opposite i.e. large magnitude beyond +/- 0.5.
	 */
	x += (x <= 0.f) ? +1.f : -1.f;
	return x * +2;
}

/**
 * Metallic value function. Golden ratio for \p x == 1, silver for x == 2, etc.
 * Also accepts zero (with the result one), and values in-between the integers.
 * (Maps negative infinity to 0.0, 0.0 to 1.0, and positive infinity to itself.
 * Negative values give how much the positive value would have been increased.)
 *
 * \return metallic value
 */
static inline double sau_met(double x) {
	return 0.5f * (x + sqrt(x * x + 4.f));
}

/**
 * Sign of \p x, in the form of +/- 1 if non-zero and +/- 0 if zero.
 *
 * \return -1, -0, +0, or +1
 */
static inline double sau_sgn(double x) {
	return copysign((x == 0.f ? 0.f : 1.f), x);
}

/* Macro used to declare and define math function sets of items. */
#define SAU_MATH__ITEMS(X) \
	X(abs,       VAL_F, {.val = fabs}) \
	X(arbf,      VAL_F, {.val = sau_arbf}) \
	X(arhf,      VAL_F, {.val = sau_arhf}) \
	X(cos,       VAL_F, {.val = cos}) \
	X(exp,       VAL_F, {.val = exp}) \
	X(log,       VAL_F, {.val = log}) \
	X(met,       VAL_F, {.val = sau_met}) \
	X(mf,      NOARG_F, {.noarg = mf_const}) \
	X(pi,      NOARG_F, {.noarg = pi_const}) \
	X(rand,    STATE_F, {.state = sau_rand}) \
	X(rint,      VAL_F, {.val = rint}) \
	X(sgn,       VAL_F, {.val = sau_sgn}) \
	X(sin,       VAL_F, {.val = sin}) \
	X(sqrt,      VAL_F, {.val = sqrt}) \
	X(time,    STATE_F, {.state = sau_time}) \
	//
/* Macro used to declare and define math magic variable sets of items. */
#define SAU_MATH__VARS_ITEMS(X) \
	X(seed, sau_seed) \
	//
#define SAU_MATH__X_ID(NAME, ...) SAU_MATH_N_##NAME,
#define SAU_MATH__X_VARS_ID(NAME, ...) SAU_MATH_N_V_##NAME,
#define SAU_MATH__X_NAME(NAME, ...) #NAME,
#define SAU_MATH__X_PARAMS(NAME, PARAMS, ...) SAU_MATH_##PARAMS,
#define SAU_MATH__X_SYM_F(NAME, PARAMS, SYM_F) SYM_F,
#define SAU_MATH__X_VARS_SYM_F(NAME, SYM_F) SYM_F,

/** Math symbol ids for functions and named constants. */
enum {
	SAU_MATH__ITEMS(SAU_MATH__X_ID)
	SAU_MATH_NAMED
};
/** Math symbol ids for magic variables. */
enum {
	SAU_MATH__VARS_ITEMS(SAU_MATH__X_VARS_ID)
	SAU_MATH_VARS_NAMED
};

struct sauMath_state;

/** State for math functions for each parsing and interpretation unit. */
struct sauMath_state {
	uint64_t seed64;
	uint32_t seed32;
	bool no_time;
};

/** Math function parameter type values. */
enum {
	SAU_MATH_VAL_F = 0,
	SAU_MATH_STATE_F,
	SAU_MATH_STATEVAL_F,
	SAU_MATH_NOARG_F
};
/** Math function pointer types for functions and named constants. */
union sauMath_sym_f {
	double (*val)(double x);
	double (*state)(struct sauMath_state *o);
	double (*stateval)(struct sauMath_state *o, double x);
	double (*noarg)(void);
};
/** Math function pointer type for magic variable. */
typedef double (*sauMath_vars_sym_f)(struct sauMath_state *o, double x);

/** Names of math functions, with an extra NULL pointer at the end. */
extern const char *const sauMath_names[SAU_MATH_NAMED + 1];
/** Names of math magic variables, with an extra NULL pointer at the end. */
extern const char *const sauMath_vars_names[SAU_MATH_VARS_NAMED + 1];

/** Parameter types for math functions. */
extern const uint8_t sauMath_params[SAU_MATH_NAMED];

/** Function addresses for math functions. */
extern const union sauMath_sym_f sauMath_symbols[SAU_MATH_NAMED];
/** Function addresses for math magic variables. */
extern const sauMath_vars_sym_f sauMath_vars_symbols[SAU_MATH_VARS_NAMED];

/*
 * Simple PRNGs.
 */

/**
 * 32-bit MCG. Usable together with another PRNG, for additional values
 * extended in a perpendicular sequence in a computationally cheap way.
 */
static inline uint32_t sau_mcg32(uint32_t seed) {
	return seed * 0xe47135; /* alt. 0x93d765dd; both Steele & Vigna 2021 */
}

/**
 * Random access noise, fast version with bitshifts but no bitrotation. Chaotic
 * waveshaper, which turns e.g. sawtooth-ish number sequences into white noise.
 * Lower bits have lower quality; use SplitMix32 if those need to be good, too.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline uint32_t sau_ranfast32(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	s ^= s >> 14;
	s = (s | 1) * s;
	s ^= s >> 13;
	return s;
}

/**
 * Random access noise, fast version with bitshifts but no bitrotation. Chaotic
 * waveshaper, which turns e.g. sawtooth-ish number sequences into white noise.
 * Lower bits have lower quality; use SplitMix32 if those need to be good, too.
 *
 * This "next" function returns a new value each time, corresponding to a state
 * \p pos, which is increased. It may be initialized with any seed (0 is fine).
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint32_t sau_ranfast32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += SAU_FIBH32;
	s ^= s >> 14;
	s = (s | 1) * s;
	s ^= s >> 13;
	return s;
}

/**
 * Fixed-increment SplitMix32 variant, using an alternative function
 * by TheIronBorn & Christopher Wellons's "Hash Prospector" project.
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint32_t sau_splitmix32_next(uint32_t *restrict pos) {
	uint32_t z = (*pos += SAU_FIBH32);
	z = (z ^ (z >> 16)) * 0x21f0aaad;
	z = (z ^ (z >> 15)) * 0xf35a2d97; /* similar alt. 0x735a2d97 */
	return z ^ (z >> 15);
}

/**
 * Fixed-increment SplitMix64, based on C version provided by Vigna.
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint64_t sau_splitmix64_next(uint64_t *restrict pos) {
	uint64_t z = (*pos += SAU_FIBH64);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

/**
 * Standard 32-bit PRNG for use together with math functions for SAU scripts.
 */
static inline uint32_t sau_rand32(struct sauMath_state *restrict o) {
	return sau_splitmix32_next(&o->seed32);
}

/*
 * Fast approximations.
 */

/**
 * Degree 5 sin(PI * x) approximation function for limited input range.
 *
 * For \p x domain -0.5 <= x <= +0.5; use with pre-wrapped values only.
 *
 * Almost clean spectrum, adds a 5th harmonic at slightly below -84 dB.
 */
static inline float sau_sinpi_d5f(float x) {
	/*
	 * Coefficients generated for no end-point error,
	 * on top of minimax, roughly doubling the error.
	 * Slightly lower max error than Taylor degree 7.
	 */
	const float scale[] = {
		+3.14042741234069229463,
		-5.13655757476162831091,
		+2.29939170159543653372,
	};
	float x2 = x*x;
	return x*(scale[0] + x2*(scale[1] + x2*scale[2]));
}

/*
 * Filters & envelopes.
 */

/** Inverse frequency coefficient with \p po2 power of two multiplier. */
#define SAU_INV_FREQ(po2, freq) (SAU_PASTE(0x1.0p, po2) / (freq))

/** RC time constant for \p msXsr time in ms multiplied by sample rate. */
#define SAU_RC_TIME_COEFF(msXsr) exp(-1000.f / (msXsr))

/** RC frequency constant for \p hz_sr frequency in Hz divided by sample rate.*/
#define SAU_RC_FREQ_COEFF(hz_sr) exp(-2*SAU_PI * (hz_sr))

/** Run exponential averaging for 1 sample, updating and returning state. */
#define SAU_RC_AVG_NEXT(state, in, coeff) \
	((state) = (in) + (coeff)*((state)-(in)))

/** Run zero-attack envelope for 1 sample, updating and returning state. */
#define SAU_RC_ZAENV_NEXT(state, in, coeff) \
	((state) = (in) + (((state)-(in)) > 0.f) ? (coeff)*((state)-(in)) : 0.f)

/** Run zero-release envelope for 1 sample, updating and returning state. */
#define SAU_RC_ZRENV_NEXT(state, in, coeff) \
	((state) = (in) + (((state)-(in)) < 0.f) ? (coeff)*((state)-(in)) : 0.f)

/** Run attack-release envelope for 1 sample, updating and returning state. */
#define SAU_RC_ARENV_NEXT(state, in, a_coeff, r_coeff) \
	((state) = (in) + ((((state)-(in)) < 0.f) ? (a_coeff) : (r_coeff)) * \
	                  ((state)-(in)))

/** Run DC blocker for 1 sample, updating and returning state. */
#define SAU_RC_DCBLOCK_NEXT(state, in, in_prev, coeff) \
	((state) = (in) - (in_prev) + (coeff)*(state))
