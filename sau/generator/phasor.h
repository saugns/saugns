/* SAU library: Phase signal generation for oscillators.
 * Copyright (c) 2022-2025 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

/** Convert (in-place) 32-bit unsigned integer phase to 0.0-1.0 float phase. */
static void sau_phase_nui32tof(void *restrict a, size_t n) {
	const uint32_t *x=a;
	float *y=a;
	for (size_t i=0; i<n; ++i) y[i]=x[i]*0x1p-32f;
}

/** Convert (in-place) 0.0-1.0 float phase to 32-bit unsigned integer phase. */
static void sau_phase_nftoui32(void *restrict a, size_t n) {
	const float *x=a;
	uint32_t *y=a;
	for (size_t i=0; i<n; ++i) y[i]=sau_ftoi(x[i]*0x1p32f);
}

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define sauPhasor_COEFF(srate) SAU_INV_FREQ(32, srate)

typedef struct sauPhasor {
	uint64_t cycle_phase; // cycle counter upper 32 bits, phase lower
	float coeff;
	bool rate2x : 1; // cycle vs. half-cycle -- fill, PD, use mode toggle
	bool preinc : 1; // align phase signal ahead by 1 -- fill mode toggle
	bool i_to_f : 1; // is last fill int phase currently float converted?
} sauPhasor;

static inline uint32_t sauPhasor_get_cycle(sauPhasor *restrict o) {
	uint32_t cycle = o->cycle_phase >> 32;
	/*
	 * Phase in rate2x mode extends to include the lowest cycle bit,
	 * which then marks even vs. odd half-cycle instead of cycle. To
	 * keep behavior consistent instead of keeping vs. losing a seed
	 * bit depending on whether or not mode is toggled, always mask.
	 */
	return cycle & ~1;
}

static inline uint32_t sauPhasor_get_phase(sauPhasor *restrict o) {
	return o->rate2x ? (o->cycle_phase >> 1) : o->cycle_phase;
}

static inline void sauPhasor_set_cycle(sauPhasor *restrict o, uint32_t cycle) {
	uint64_t phase64 = sauPhasor_get_phase(o);
	if (o->rate2x) phase64 <<= 1;
	/*
	 * Phase in rate2x mode extends to include the lowest cycle bit,
	 * which then marks even vs. odd half-cycle instead of cycle. To
	 * keep behavior consistent instead of keeping vs. losing a seed
	 * bit depending on whether or not mode is toggled, always mask.
	 */
	o->cycle_phase = ((uint64_t)(cycle & ~1)) << 32 | phase64;
}

static inline void sauPhasor_set_phase(sauPhasor *restrict o, uint32_t phase) {
	uint64_t cycle64 = sauPhasor_get_cycle(o);
	uint64_t phase64 = (o->rate2x) ? ((uint64_t)phase) << 1 : phase;
	o->cycle_phase = cycle64 << 32 | phase64;
}

/**
 * Fill cycle-value and phase-value buffers for use with an oscillator.
 *
 * "Cycles" may have 2x the normal speed while mapped to line sgements.
 * Most simple waveforms need two line segments per cycle, sawtooth and
 * similar being the one-segment exceptions. Randomization maps a cycle
 * to a PRNG state with two neighboring states used for a line segment.
 *
 * A NULL \p cycle_ui32 is allowed. This can be used for an oscillator
 * which does not need cycle data. It is also allowed for PD *_pdist_*
 * functions when they're not ran in rate2x mode.
 */
static sauMaybeUnused void sauPhasor_fill(sauPhasor *restrict o,
		uint32_t *restrict cycle_ui32,
		uint32_t *restrict phase_ui32,
		size_t len,
		const float *restrict freq_f,
		const float *restrict pm_f) {
#define PRE(inc, ofs)  ofs + (o->cycle_phase += inc) // be ahead one sample
#define POST(inc, ofs) ofs + o->cycle_phase; (o->cycle_phase += inc)
	float coeff = o->coeff;
	float phase_scale = 0x1p31f;
	if (o->rate2x) {
		coeff *= 2;
		phase_scale *= 2;
	}
	o->i_to_f = false; // new fill
#define FILL(FREQ, P, PM_IN) \
	for (size_t i = 0; i < len; ++i) { \
		uint64_t cycle_phase = P(sau_ftoi(coeff * (FREQ)), (PM_IN)); \
		if (cycle_ui32) \
			cycle_ui32[i] = cycle_phase >> 32; \
		phase_ui32[i] = cycle_phase; \
	} \
/**/
	if (o->preinc) { // compensate for 1-sample off delay
		if (!pm_f) FILL(freq_f[i], PRE, 0)
		else       FILL(freq_f[i], PRE, sau_ftoi(pm_f[i]*phase_scale))
	} else {
		if (!pm_f) FILL(freq_f[i], POST, 0)
		else       FILL(freq_f[i], POST, sau_ftoi(pm_f[i]*phase_scale))
	}
#undef PRE
#undef POST
#undef FILL
}

/** Ensure phase data from last fill is float. */
static sauMaybeUnused void
sauPhasor_ui32tof(sauPhasor *restrict o, void *restrict phase, size_t len) {
	if (o->i_to_f)
		return; // nothing to do
	sau_phase_nui32tof(phase, len);
	o->i_to_f = true;
}

/** Ensure phase data from last fill is 32-bit unsigned integer. */
static sauMaybeUnused void
sauPhasor_ftoui32(sauPhasor *restrict o, void *restrict phase, size_t len) {
	if (!o->i_to_f)
		return; // nothing to do
	sau_phase_nftoui32(phase, len);
	o->i_to_f = false;
}

typedef void (*sauPhasor_pdist_fn)(sauPhasor *restrict o,
		float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		size_t len,
		const float *restrict v_f,
		const float *restrict f_f,
		float f_fval,
		const float *restrict p_f,
		float p_fval);

/*
 * PD get/set phase macros. The versions differ as follows:
 * - _FMUL: Multiply frequency as last step, used to rescale it.
 * - _POFS: Phase offset parameter is supported.
 * - _SUBF: Apply subfrequency, dividing phase into sub-areas,
 *   e.g. resulting in odd harmonics for distortion when using
 *   even integers. Phase offset parameter is also supported.
 * - _RATE2X: Use twice the phase range, borrowing a cycle bit.
 *
 * Some functions use simpler code than these macros,
 * if it's trivial and works faster than a more complex macro.
 */
#define PD_GET_POFS(x, phase_f, offset) PD_GET_SUBF(x, phase_f, offset, 1)
#define PD_GET_SUBF(x, phase_f, offset, f_mul) \
	float c_f = (offset);                                      \
	float x = (phase_f - c_f) * f_mul;                         \
	float f_adj = sau_i32floorf(x);                            \
	x -= f_adj;                                                \
//
#define PD_GET_RATE2X(x, phase_f, cycle_ui32) \
	int32_t cycle = (cycle_ui32 & 1);                          \
	float x = (phase_f + cycle) * 0.5f;                        \
//
#define PD_GET_RATE2X_POFS(x, phase_f, cycle_ui32, offset) \
	float c_f = (offset);                                      \
	int32_t cycle = (cycle_ui32 & 1);                          \
	float x = (phase_f + cycle) * 0.5f - c_f;                  \
	float f_adj = sau_i32floorf(x);                            \
	x -= f_adj;                                                \
//
#define PD_GET_RATE2X_SUBF(x, phase_f, cycle_ui32, offset, fhalf) \
	float c_f = (offset)*2;                                    \
	int32_t cycle = (cycle_ui32 & 1);                          \
	float x = (phase_f + cycle - c_f) * fhalf;                 \
	float f_adj = sau_i32floorf(x);                            \
	x -= f_adj;                                                \
//
#define PD_SET(x, phase_f) \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail()             */ \
//
#define PD_SET_FMUL(x, phase_f, f_mul) \
	x *= f_mul;                                                \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_fmul()        */ \
//
#define PD_SET_FMUL_POFS(x, phase_f, f_mul) \
	x = (x + f_adj) * f_mul + c_f;                             \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_fmul()        */ \
//
#define PD_SET_SUBF(x, phase_f, f_mul_inv) \
	x = (x + f_adj) * f_mul_inv + c_f;                         \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail()             */ \
//
#define PD_SET_RATE2X(x, phase_f) \
	x += x;                                                    \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_rate2x()      */ \
//
#define PD_SET_RATE2X_FMUL(x, phase_f, f_f) \
	x *= f_f*2;                                                \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_rate2x_fmul() */ \
//
#define PD_SET_RATE2X_FMUL_POFS(x, phase_f, f_f) \
	x = ((x + f_adj) * f_f + c_f)*2;                           \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_rate2x_fmul() */ \
//
#define PD_SET_RATE2X_SUBF(x, phase_f, fhalf_inv) \
	x = (x + f_adj) * fhalf_inv + c_f;                         \
	phase_f = x;                                               \
	/* tail after is in loop func pd_set_tail_rate2x()      */ \
//

static void pd_set_tail_rate2x_fmul_val(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		float f_fval,
		size_t n) {
	int32_t cycle_mul = sau_i32floorf(f_fval);
	if (!cycle_mul) cycle_mul = 1;
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle = cycle_ui32[i] & 1;
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		cycle_ui32[i] = cycle_mul * (cycle_ui32[i] - cycle) + cycle_adj;
		phase_f[i] -= cycle_adj;
	}
}

static void pd_set_tail_rate2x_fmul_arr(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		const float *restrict f_f,
		size_t n) {
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle = cycle_ui32[i] & 1;
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		int32_t cycle_mul = sau_i32floorf(f_f[i]);
		if (!cycle_mul) cycle_mul = 1;
		cycle_ui32[i] = cycle_mul * (cycle_ui32[i] - cycle) + cycle_adj;
		phase_f[i] -= cycle_adj;
	}
}

static sauNoinline void pd_set_tail_rate2x_fmul(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		const float *restrict f_f,
		float f_fval,
		size_t n) {
	if (!cycle_ui32)
		return; // skip if no cycle data (phase may be wrapped later)
	if (f_f) pd_set_tail_rate2x_fmul_arr(phase_f, cycle_ui32, f_f, n);
	else     pd_set_tail_rate2x_fmul_val(phase_f, cycle_ui32, f_fval, n);
}

static void pd_set_tail_fmul_val(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		float f_fval,
		size_t n) {
	int32_t cycle_mul = sau_i32floorf(f_fval);
	if (!cycle_mul) cycle_mul = 1;
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		cycle_ui32[i] = cycle_mul * cycle_ui32[i] + cycle_adj;
		phase_f[i] -= cycle_adj;
	}
}

static void pd_set_tail_fmul_arr(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		const float *restrict f_f,
		size_t n) {
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		int32_t cycle_mul = sau_i32floorf(f_f[i]);
		if (!cycle_mul) cycle_mul = 1;
		cycle_ui32[i] = cycle_mul * cycle_ui32[i] + cycle_adj;
		phase_f[i] -= cycle_adj;
	}
}

static sauNoinline void pd_set_tail_fmul(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		const float *restrict f_f,
		float f_fval,
		size_t n) {
	if (!cycle_ui32)
		return; // skip if no cycle data (phase may be wrapped later)
	if (f_f) pd_set_tail_fmul_arr(phase_f, cycle_ui32, f_f, n);
	else     pd_set_tail_fmul_val(phase_f, cycle_ui32, f_fval, n);
}

static sauNoinline void pd_set_tail_rate2x(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		size_t n) {
	if (!cycle_ui32)
		return; // skip if no cycle data (phase may be wrapped later)
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle = cycle_ui32[i] & 1;
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		cycle_ui32[i] += cycle_adj - cycle;
		phase_f[i] -= cycle_adj;
	}
}

static sauNoinline void pd_set_tail(float *restrict phase_f,
		uint32_t *restrict cycle_ui32,
		size_t n) {
	if (!cycle_ui32)
		return; // skip if no cycle data (phase may be wrapped later)
	for (size_t i = 0; i < n; ++i) {
		int32_t cycle_adj = sau_i32floorf(phase_f[i]);
		cycle_ui32[i] += cycle_adj;
		phase_f[i] -= cycle_adj;
	}
}

/*
 * Generate simple versions of PD loops, with no extra frequency control,
 * using \p VAL_EXPR.
 */
#define PD_SIMPLE(VAL_EXPR) \
if (o->rate2x) {                                                            \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_RATE2X(x, phase_f[i], cycle_ui32[i])                 \
		x = (VAL_EXPR);                                             \
		PD_SET_RATE2X(x, phase_f[i])                                \
	}                                                                   \
	pd_set_tail_rate2x(phase_f, cycle_ui32, len);                       \
} else {                                                                    \
	for (size_t i = 0; i < len; ++i) {                                  \
		float x = phase_f[i];                                       \
		x = (VAL_EXPR);                                             \
		PD_SET(x, phase_f[i])                                       \
	}                                                                   \
	pd_set_tail(phase_f, cycle_ui32, len);                              \
}

/*
 * Generate PD loops with frequency multiplier as for zoom-PDs,
 * using \p VAL_EXPR, with no phase offset parameter.
 */
#define PD_FMUL(fmul, VAL_EXPR) \
if (o->rate2x) {                                                            \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_RATE2X(x, phase_f[i], cycle_ui32[i])                 \
		x = (VAL_EXPR);                                             \
		PD_SET_RATE2X_FMUL(x, phase_f[i], fmul)                     \
	}                                                                   \
	pd_set_tail_rate2x_fmul(phase_f, cycle_ui32, f_f, f_fval, len);     \
} else {                                                                    \
	for (size_t i = 0; i < len; ++i) {                                  \
		float x = phase_f[i];                                       \
		x = (VAL_EXPR);                                             \
		PD_SET_FMUL(x, phase_f[i], fmul)                            \
	}                                                                   \
	pd_set_tail_fmul(phase_f, cycle_ui32, f_f, f_fval, len);            \
}

/*
 * Generate PD loops with frequency multiplier as for zoom-PDs,
 * using \p VAL_EXPR, and supporting the phase offset parameter (slower).
 */
#define PD_FMUL_POFS(offset, fmul, VAL_EXPR) \
if (o->rate2x) {                                                            \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_RATE2X_POFS(x, phase_f[i], cycle_ui32[i],  offset)   \
		x = (VAL_EXPR);                                             \
		PD_SET_RATE2X_FMUL_POFS(x, phase_f[i], fmul)                \
	}                                                                   \
	pd_set_tail_rate2x_fmul(phase_f, cycle_ui32, f_f, f_fval, len);     \
} else {                                                                    \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_POFS(x, phase_f[i], offset)                          \
		x = (VAL_EXPR);                                             \
		PD_SET_FMUL_POFS(x, phase_f[i], fmul)                       \
	}                                                                   \
	pd_set_tail_fmul(phase_f, cycle_ui32, f_f, f_fval, len);            \
}

/*
 * Generate PD loops with subfrequency control acting like modulator ratio,
 * using \p VAL_EXPR.
 */
#define PD_SUBF(offset, f_f, VAL_EXPR) \
if (o->rate2x) {                                                            \
	for (size_t i = 0; i < len; ++i) {                                  \
		float fmul = f_f * 0.5f;                                    \
		PD_GET_RATE2X_SUBF(x, phase_f[i], cycle_ui32[i],            \
				offset, fmul)                               \
		x = (VAL_EXPR);                                             \
		PD_SET_RATE2X_SUBF(x, phase_f[i], 1 / fmul)                 \
	}                                                                   \
	pd_set_tail_rate2x(phase_f, cycle_ui32, len);                       \
} else {                                                                    \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_SUBF(x, phase_f[i], offset, f_f)                     \
		x = (VAL_EXPR);                                             \
		PD_SET_SUBF(x, phase_f[i], 1 / f_f)                         \
	}                                                                   \
	pd_set_tail(phase_f, cycle_ui32, len);                              \
}

/*
 * Generate PD loops with a constant value for subfrequency control,
 * using \p VAL_EXPR.
 */
#define PD_SUBF_CONST(offset, f_f, VAL_EXPR) \
if (o->rate2x) {                                                            \
	const float fmul = f_f * 0.5f;                                      \
	const float fmul_inv = 1.f / fmul;                                  \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_RATE2X_SUBF(x, phase_f[i], cycle_ui32[i],            \
				offset, fmul)                               \
		x = (VAL_EXPR);                                             \
		PD_SET_RATE2X_SUBF(x, phase_f[i], fmul_inv)                 \
	}                                                                   \
	pd_set_tail_rate2x(phase_f, cycle_ui32, len);                       \
} else {                                                                    \
	const float fmul = f_f;                                             \
	const float fmul_inv = 1.f / fmul;                                  \
	for (size_t i = 0; i < len; ++i) {                                  \
		PD_GET_SUBF(x, phase_f[i], offset, fmul)                    \
		x = (VAL_EXPR);                                             \
		PD_SET_SUBF(x, phase_f[i], fmul_inv)                        \
	}                                                                   \
	pd_set_tail(phase_f, cycle_ui32, len);                              \
}

/* Define phase distortion function. */
#define PDIST_FUNC(NAME, BODY) \
static sauMaybeUnused void                                                   \
sauPhasor_pdist_##NAME(sauPhasor *restrict o sauMaybeUnused,                 \
		float *restrict phase_f,                                     \
		uint32_t *restrict cycle_ui32,                               \
		size_t len,                                                  \
		const float *restrict v_f,                                   \
		const float *restrict f_f,                                   \
		float f_fval,                                                \
		const float *restrict p_f,                                   \
		float p_fval) {                                              \
	BODY                                                                 \
}

/* Loop bodies for sauPhasor_pdist_pulwm_mul(), sauPhasor_pdist_pulwm_div(). */
#define PULWM_APPLY(OP) \
if (p_f) {                                                                   \
	if (f_f) {                                                           \
		PD_FMUL_POFS(p_f[i], f_f[i],                                 \
				sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	} else {                                                             \
		PD_FMUL_POFS(p_f[i], f_fval,                                 \
				sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	}                                                                    \
} else if (p_fval != 0.f) {                                                  \
	if (f_f) {                                                           \
		PD_FMUL_POFS(p_fval, f_f[i],                                 \
				sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	} else {                                                             \
		PD_FMUL_POFS(p_fval, f_fval,                                 \
				sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	}                                                                    \
} else {                                                                     \
	if (f_f) {                                                           \
		PD_FMUL(f_f[i], sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	} else if (f_fval != 1.f) {                                          \
		PD_FMUL(f_fval, sau_fclampf(x OP v_f[i], -1.f, 1.f))         \
	} else {                                                             \
		if (o->rate2x) {                                             \
			for (size_t i = 0; i < len; ++i) {                   \
				PD_GET_RATE2X(x, phase_f[i], cycle_ui32[i])  \
				x = sau_fclampf(x OP v_f[i], -1.f, 1.f);     \
				PD_SET_RATE2X(x, phase_f[i])                 \
			}                                                    \
			pd_set_tail_rate2x(phase_f, cycle_ui32, len);        \
		} else {                                                     \
			for (size_t i = 0; i < len; ++i) {                   \
				float x = phase_f[i];                        \
				x = sau_fclampf(x OP v_f[i], -1.f, 1.f);     \
				phase_f[i] = x < 0.f ? x + 1.f : x;          \
			}                                                    \
		}                                                            \
	}                                                                    \
}

/*
 * Phase distortion: cycle length. Below 1 zooms in resulting in jagged shapes,
 * above 1 zooms out adding padding (the amplitude at the cycle beginning/end).
 */
PDIST_FUNC(pulwm_mul, PULWM_APPLY(*))

/*
 * Phase distortion: duty cycle. Below 1 zooms out adding padding,
 * above 1 zooms in resulting in jagged shapes.
 */
PDIST_FUNC(pulwm_div, PULWM_APPLY(/))

#undef PULWM_APPLY

/* Loop bodies for sauPhasor_pdist_hold() and others of the same form. */
#define SUBF_APPLY(val_pdist_f) \
if (p_f) {                                                                   \
	if (f_f) {                                                           \
		PD_SUBF(p_f[i], f_f[i], val_pdist_f(x, v_f[i], 1))           \
	} else {                                                             \
		PD_SUBF_CONST(p_f[i], f_fval, val_pdist_f(x, v_f[i], 1))     \
	}                                                                    \
} else if (p_fval != 0.f) {                                                  \
	if (f_f) {                                                           \
		PD_SUBF(p_fval, f_f[i], val_pdist_f(x, v_f[i], 1))           \
	} else {                                                             \
		PD_SUBF_CONST(p_fval, f_fval, val_pdist_f(x, v_f[i], 1))     \
	}                                                                    \
} else {                                                                     \
	if (f_f) {                                                           \
		PD_SUBF(0, f_f[i], val_pdist_f(x, v_f[i], 1))                \
	} else if (f_fval != 1.f) {                                          \
		PD_SUBF_CONST(0, f_fval, val_pdist_f(x, v_f[i], 1))          \
	} else {                                                             \
		if (o->rate2x) {                                             \
			for (size_t i = 0; i < len; ++i) {                   \
				PD_GET_RATE2X(x, phase_f[i], cycle_ui32[i])  \
				x = val_pdist_f(x, v_f[i], 1);               \
				PD_SET_RATE2X(x, phase_f[i])                 \
			}                                                    \
			pd_set_tail_rate2x(phase_f, cycle_ui32, len);        \
		} else {                                                     \
			for (size_t i = 0; i < len; ++i) {                   \
				float x = phase_f[i];                        \
				x = val_pdist_f(x, v_f[i], 1);               \
				phase_f[i] = x;                              \
			}                                                    \
		}                                                            \
	}                                                                    \
}

/*
 * Phase distortion: hold from beginning/end for part of a cycle.
 * Positive values hold forwards, negative values hold backwards.
 */
PDIST_FUNC(hold, SUBF_APPLY(sau_pdist_hold))

#undef SUBF_APPLY

/* Loop bodies for sauPhasor_pdist_xhalf() and others of the same form. */
#define SUBF_APPLY(val_pdist_f) \
if (p_f) {                                                                   \
	if (f_f) {                                                           \
		PD_SUBF(p_f[i], f_f[i], val_pdist_f(x, v_f[i], 1))           \
	} else {                                                             \
		PD_SUBF_CONST(p_f[i], f_fval, val_pdist_f(x, v_f[i], 1))     \
	}                                                                    \
} else if (p_fval != 0.f) {                                                  \
	if (f_f) {                                                           \
		PD_SUBF(p_fval, f_f[i], val_pdist_f(x, v_f[i], 1))           \
	} else {                                                             \
		PD_SUBF_CONST(p_fval, f_fval, val_pdist_f(x, v_f[i], 1))     \
	}                                                                    \
} else {                                                                     \
	if (f_f) {                                                           \
		PD_SUBF(0, f_f[i], val_pdist_f(x, v_f[i], 1))                \
	} else if (f_fval != 1.f) {                                          \
		PD_SUBF_CONST(0, f_fval, val_pdist_f(x, v_f[i], 1))          \
	} else {                                                             \
		PD_SIMPLE(val_pdist_f(x, v_f[i], 1))                         \
	}                                                                    \
}

/*
 * Phase distortion: half-cycle width a.k.a. size proportion of each half.
 */
PDIST_FUNC(halfx, SUBF_APPLY(sau_pdist_halfx))

/*
 * Phase distortion: half-cycle height a.k.a. change proportion of each half.
 */
PDIST_FUNC(halfy, SUBF_APPLY(sau_pdist_halfy))

#undef SUBF_APPLY

#undef PD_GET_SUBF
#undef PD_GET_RATE2X
#undef PD_GET_RATE2X_SUBF
#undef PD_SET
#undef PD_SET_FMUL
#undef PD_SET_FMUL_POFS
#undef PD_SET_SUBF
#undef PD_SET_RATE2X
#undef PD_SET_RATE2X_FMUL
#undef PD_SET_RATE2X_FMUL_POFS
#undef PD_SET_RATE2X_SUBF

#undef PD_SIMPLE
#undef PD_FMUL
#undef PD_FMUL_POFS
#undef PD_SUBF
#undef PD_SUBF_CONST

#undef PDIST_FUNC

static inline sauPhasor_pdist_fn sauPhasor_get_pdist_fn(unsigned func) {
	switch (func) {
	default:        return NULL;
	case SAU_PPD_C: return sauPhasor_pdist_pulwm_mul;
	case SAU_PPD_D: return sauPhasor_pdist_pulwm_div;
	case SAU_PPD_H: return sauPhasor_pdist_hold;
	case SAU_PPD_X: return sauPhasor_pdist_halfx;
	case SAU_PPD_Y: return sauPhasor_pdist_halfy;
	}
}
