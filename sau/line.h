/* SAU library: Value line module.
 * Copyright (c) 2011-2013, 2017-2024 Joel K. Pettersson
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
#include "math.h"

/* Macro used to declare and define line type sets of items. */
#define SAU_LINE__ITEMS(X) \
	X(cos, (.perlin_amp = 2.f)) \
	X(lin, (.perlin_amp = 2.f)) \
	X(sah, (.perlin_amp = 1.f)) \
	X(exp, (.perlin_amp = 1.55845810035f)) \
	X(log, (.perlin_amp = 1.55845810035f)) \
	X(xpe, (.perlin_amp = 1.55845810035f)) \
	X(lge, (.perlin_amp = 1.55845810035f)) \
	X(sqe, (.perlin_amp = 1.89339094650f)) \
	X(cub, (.perlin_amp = 2.f)) \
	X(smo, (.perlin_amp = 2.f)) \
	X(ncl, (.perlin_amp = 2.f)) \
	X(nhl, (.perlin_amp = 1.89339094650f)) \
	X(uwh, (.perlin_amp = 1.f)) \
	X(yme, (.perlin_amp = 1.f)) \
	//
#define SAU_LINE__X_ID(NAME, ...) SAU_LINE_N_##NAME,
#define SAU_LINE__X_NAME(NAME, ...) #NAME,
#define SAU_LINE__X_PROTOTYPES(NAME, ...) \
void sauLine_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time, \
		const float *restrict mulbuf); \
void sauLine_map_##NAME(float *restrict buf, uint32_t len, \
		const float *restrict end0, const float *restrict end1); \
/*float sauLine_val_##NAME(float x, float a, float b);*/ /* inlined */ \
/**/
#define SAU_LINE__X_FILL_ADDR(NAME, ...) sauLine_fill_##NAME,
#define SAU_LINE__X_MAP_ADDR(NAME, ...) sauLine_map_##NAME,
#define SAU_LINE__X_VAL_ADDR(NAME, ...) sauLine_val_##NAME,
#define SAU_LINE__X_COEFFS(NAME, COEFFS) {SAU_ARGS COEFFS},

/**
 * Line type shapes.
 */
enum {
	SAU_LINE__ITEMS(SAU_LINE__X_ID)
	SAU_LINE_NAMED
};

SAU_LINE__ITEMS(SAU_LINE__X_PROTOTYPES)

/** Information about or for use with a line type. */
struct sauLineCoeffs {
	float perlin_amp; // from 1.0 to 2.0 depending on shape for +/- 1.0 out
};

/** Extra values for use with line-using algorithms. */
extern const struct sauLineCoeffs sauLine_coeffs[SAU_LINE_NAMED];

/** Names of line type shapes, with an extra NULL pointer at the end. */
extern const char *const sauLine_names[SAU_LINE_NAMED + 1];

typedef void (*sauLine_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

typedef void (*sauLine_map_f)(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1);

typedef float (*sauLine_val_f)(float x, float a, float b);

/**
 * Fill functions for line type shapes. See comments per function.
 */
extern const sauLine_fill_f sauLine_fill_funcs[SAU_LINE_NAMED];

/**
 * Map functions for line type shapes.
 *
 * Map positions in \p buf (values from 0.0 to 1.0) to some trajectory,
 * writing \p len values between those of \p end0 and \p end1 into \p buf.
 */
extern const sauLine_map_f sauLine_map_funcs[SAU_LINE_NAMED];

/**
 * Single value functions for line type shapes. See comments per function.
 */
extern const sauLine_val_f sauLine_val_funcs[SAU_LINE_NAMED];

/**
 * Line parameter flags.
 */
enum {
	SAU_LINEP_STATE       = 1<<0, // v0 set
	SAU_LINEP_STATE_RATIO = 1<<1,
	SAU_LINEP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	SAU_LINEP_GOAL_RATIO  = 1<<3,
	SAU_LINEP_TYPE        = 1<<4, // type set
	SAU_LINEP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	SAU_LINEP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Line parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct sauLine {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t type;
	uint8_t flags;
} sauLine;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define sauLine_ENABLED(o) \
	((o)->flags & (SAU_LINEP_STATE | SAU_LINEP_GOAL))

/** Needed before get, run, or skip when a line is not copy-initialized. */
static inline void sauLine_setup(sauLine *restrict o, uint32_t srate) {
	o->end = sau_ms_in_samples(o->time_ms, srate, NULL);
}
void sauLine_copy(sauLine *restrict o,
		const sauLine *restrict src,
		uint32_t srate);

uint32_t sauLine_get(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauLine_run(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauLine_skip(sauLine *restrict o, uint32_t skip_len);

/*
 * Inline functions for single value line functions.
 */

/** Single value sample and hold, returns \p a. */
static inline float sauLine_val_sah(float x, float a, float b) {
	(void)x;
	(void)b;
	return a;
}

/** Single value \p x in line from \p a to \p b. */
static inline float sauLine_val_lin(float x, float a, float b) {
	return a + (b - a) * x;
}

/**
 * Scaled and shifted sine ramp, using degree 5 polynomial
 * with no error at ends and double the minimax max error.
 *
 * Note: Needs \p x in, returns in range from -0.5 to 0.5.
 *
 * If used for oscillator, would have a roughly -84 dB 5th
 * harmonic distortion but nothing else above 16-bit noise
 * floor. http://joelkp.frama.io/blog/modified-taylor.html
 */
static inline float sau_sinramp(float x) {
	const float scale[] = {
		/* constants calculated with 80-bit "long double" use */
		+1.5702137061703461473139223358864L,
		-2.568278787380814155456160152724L,
		+1.1496958507977182668618673644367L,
	};
	float x2 = x*x;
	return x*(scale[0] + x2*(scale[1] + x2*scale[2]));
}

/** Single value \p x in cos-based sinuous S-curve line from \p a to \p b. */
static inline float sauLine_val_cos(float x, float a, float b) {
	return a + (b - a) * (sau_sinramp(x - 0.5f) + 0.5f);
}

/**
 * My 2011 exponential curve approximation, kept from early versions.
 *
 * Steepness matches a downscaled exp(6*x), for 0 <= x <= 1.
 */
static inline float sau_expramp6(float x) {
	float x2 = x * x;
	float x3 = x2 * x;
	return x3 + (x2 * x3 - x2) *
		(x * (629.f/1792.f) + x2 * (1163.f/1792.f));
}

/** Single value \p x in exponential trajectory from \p a to \p b. */
static inline float sauLine_val_exp(float x, float a, float b) {
	return (a > b) ?
		b + (a - b) * sau_expramp6(1.f-x) :
		a + (b - a) * sau_expramp6(x);
}

/** Single value \p x in logarithmic trajectory from \p a to \p b. */
static inline float sauLine_val_log(float x, float a, float b) {
	return (a < b) ?
		b + (a - b) * sau_expramp6(1.f-x) :
		a + (b - a) * sau_expramp6(x);
}

/** Single value \p x, exponential saturate or decay curve from \p a to \p b. */
static inline float sauLine_val_xpe(float x, float a, float b) {
	return b + (a - b) * sau_expramp6(1.f-x);
}

/** Single value \p x, logarithmic saturate or decay curve from \p a to \p b. */
static inline float sauLine_val_lge(float x, float a, float b) {
	return a + (b - a) * sau_expramp6(x);
}

/** Single value \p x, x-squared envelope trajectory from \p a to \p b. */
static inline float sauLine_val_sqe(float x, float a, float b) {
	x = 1.f - x;
	return b + (a - b) * (x * x);
}

/** Single value \p x, x-cubed trajectory from \p a to \p b. */
static inline float sauLine_val_cub(float x, float a, float b) {
	x = (0.5f - x)*2;
	return b + (a - b) * (x * x * x * 0.5f + 0.5f);
}

/** Single value \p x, smoothstep (degree 5) trajectory from \p a to \p b. */
static inline float sauLine_val_smo(float x, float a, float b) {
	return a + (b - a) * x*x*x*(+10.f + x*(-15.f + x*+6.f));
//	const float x2 = x*x;
//	return a + (b - a) * x2*x2*(+35.f + x*(-84.f + x*(+70.f + x*-20.f)));
}

/** Single value from \p a to \p b using \p x as uniform random PRNG seed. */
static inline float sauLine_val_uwh(float x, float a, float b) {
	union {float f; int32_t i;} xs = {.f = x};
	int32_t s = sau_ranfast32(xs.i);
	return a + (b - a) * (0.5f + (0.5f*0x1p-31f) * s);
}

/** Single value from \p a to \p b placing \p x in "noise camel line". */
static inline float sauLine_val_ncl(float x, float a, float b) {
	float xb = x; xb -= (3.f - (xb+xb))*xb*xb;
	union {float f; int32_t i;} xs = {.f = x};
	int32_t s = sau_ranfast32(xs.i);
	return a + (b - a) * (x + xb * s * (0.5f*0x1p-31f));
}

/** Single value from \p a to \p b placing \p x in "noise hump line". */
static inline float sauLine_val_nhl(float x, float a, float b) {
	float xb = x; xb -= xb*xb;
	union {float f; int32_t i;} xs = {.f = x};
	int32_t s = sau_ranfast32(xs.i);
	return a + (b - a) * (x + xb * s * 0x1p-31f);
}

/** Single value from \p a to \p b placing \p x in rough YM2612 attack/decay. */
static inline float sauLine_val_yme(float x, float a, float b) {
	float v;
	/*v = x;
	float v2 = v*v, v4 = v2*v2, v8 = v4*v4 + v*(v2 - v4);
	v = (a < b) ? x : v8*v4;*/
	//x = (exp(x * 8.f) - 1.f) / (2980.95798704172827474359 - 1.f);
	v = (exp(x * 11.f) - 1.f) / (59874.14171519781845532648 - 1.f);
	v = (a < b) ? x : v;
	return a + (b - a) * v;
	//
	//return b + (a - b) * expramp2(1.f - x);
}
