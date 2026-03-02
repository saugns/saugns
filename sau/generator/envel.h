/* SAU library: Envelope generator module.
 * Copyright (c) 2025-2026 Joel K. Pettersson
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
#include "../line.h"

typedef struct sauEnvGen {
	uint32_t time[SAU_ENV_TIMES];
	uint8_t line[SAU_ENV_TIMES];
	uint8_t mode;
	uint8_t stage;      // 0 if unused, otherwise indicates what to run
	bool r_stretch : 1; // stretch release to take over sustain?
	bool keep_last : 1; // is a final declick value set for use?
	float s_val;
	uint32_t pos;       // time position in current stage
	float e_last;       // saved last value for declicking the attack
} sauEnvGen;

static bool sauEnvGen_has_time(sauEnvGen *restrict o) {
	if (o->r_stretch)
		return true;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) if (o->time[i] > 0)
		return true;
	return false;
}

/**
 * Set line. Flips 'exp' and 'log' to compensate for the flipped
 * top and bottom values/direction when envelope lines are produced.
 */
static inline void
sauEnvGen_set_line(sauEnvGen *restrict o, unsigned i, uint8_t line) {
	o->line[i] = sauLine_flip_exp_log(line);
}

static void sauEnvGen_set_lines(sauEnvGen *restrict o, uint8_t line_all) {
	for (int i = 0; i < SAU_ENV_TIMES; ++i)
		sauEnvGen_set_line(o, i, line_all);
}

static void sauEnvGen_set_par(sauEnvGen *restrict o,
		const sauEnvPar *restrict src, uint32_t srate) {
	if (src->flags & SAU_ENVP_MODE)
		o->mode = src->mode;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) {
		if (!(src->time_flags & SAU_ENVP_TIME(i))) continue;
		o->time[i] = sau_ms_in_samples(src->time_ms[i], srate, NULL);
	}
	if (src->time_flags & SAU_ENVP_TIME(SAU_ENV_TIME_R))
		o->r_stretch = src->flags & SAU_ENVP_R_STRETCH;
	if (src->line_all_p1)
		sauEnvGen_set_lines(o, src->line_all_p1 - 1);
	for (int i = 0; i < SAU_ENV_TIMES; ++i) {
		if (src->line_p1[i])
			sauEnvGen_set_line(o, i, src->line_p1[i] - 1);
	}
	if (src->flags & SAU_ENVP_S)
		o->s_val = src->s_val;
	// Suspend or resume envelope?
	if (o->mode != SAU_ENV_FN_OFF &&
			(sauEnvGen_has_time(o) || o->s_val != 1.f)) {
		if (!o->stage) o->stage = ~0;
	} else {
		o->stage = 0;
		o->pos = 0;
	}
}

static inline void sau_init_EnvGen(sauEnvGen *restrict o) {
	sauEnvGen_set_lines(o, SAU_LINE_N_lin);
	o->mode = SAU_ENV_FN_DEFAULT;
	o->s_val = 1.f;
	o->e_last = 1.f;
}

static uint32_t sauEnvGen_run_line(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len,
		uint32_t e_time[static SAU_ENV_TIMES],
		unsigned par_i, float v0, float vt) {
	uint32_t time = e_time[par_i], full_time = o->time[par_i];
	if (o->mode == SAU_ENV_FN_LOOP)
		time = full_time; // Looping needs the full envelope shape.
	uint32_t n = len, rem = 0;
	if (o->pos + n > time) {
		rem = (o->pos + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	uint32_t line_time = (o->mode == SAU_ENV_FN_SHRINK) ? time : full_time;
	sauLine_fill_funcs[o->line[par_i]](buf, n,
			v0, vt, o->pos, line_time);
	o->pos += n;
	if (o->pos >= time) {
		if (o->mode != SAU_ENV_FN_DECLICK || full_time == 0) {
			if (!o->keep_last)
				o->e_last = vt;
		} else if (n > 0) {
			uint32_t pos =
				o->pos < line_time ? o->pos + 1 : line_time;
			sauLine_fill_funcs[o->line[par_i]](&o->e_last, 1,
					v0, vt, pos, line_time);
			if (time < full_time)
				o->keep_last = true;
		}
		++o->stage;
		o->pos = 0;
	}
	return rem;
}

static uint32_t sauEnvGen_run_sustain(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len,
		uint32_t e_time[static SAU_ENV_TIMES], uint32_t time) {
	uint32_t n = len, rem = 0;
	if (o->mode == SAU_ENV_FN_LOOP)
		goto SKIP_STAGE; // Use time for loop instead of sustain.
	if (o->r_stretch) {
		// Yield the sustain to a stretched release stage.
		o->time[SAU_ENV_TIME_R] = e_time[SAU_ENV_TIME_R] = time;
		goto SKIP_STAGE;
	}
	if (o->pos + n > time) {
		rem = (o->pos + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	float x = 1.f - o->s_val;
	for (uint32_t i = 0; i < n; ++i) buf[i] = x;
	o->pos += n;
	if (o->pos >= time) {
		if (n > 0) o->e_last = x;
		++o->stage;
		o->pos = 0;
	}
	return rem;
SKIP_STAGE:
	++o->stage;
	return len;
}

/**
 * Produce clamped time values, limited for the current note duration.
 * If there's no sustain time, later stages are reduced before earlier
 * in length, so all can finish (or possibly be omitted).
 */
static uint32_t sauEnvGen_get_time_clamped(sauEnvGen *restrict o,
		uint32_t e_time[static SAU_ENV_TIMES], uint32_t note_dur) {
	uint32_t e_total = 0;
	// Copy release time value, and avoid clobbering it in stretch mode.
	e_time[SAU_ENV_TIME_R] = o->time[SAU_ENV_TIME_R];
	for (int i = 0, n = (SAU_ENV_TIMES - o->r_stretch); i < n; ++i) {
		uint32_t t = o->time[i];
		e_total += t;
		if (e_total > note_dur) {
			e_time[i] = t - (e_total - note_dur);
			e_total = note_dur;
			for (int j = i+1; j < n; ++j) e_time[j] = 0;
			break;
		}
		e_time[i] = t;
	}
	return e_total;
}

/**
 * Run for \p len samples, filling \p buf with a signal for applying an
 * envelope to a parameter.
 *
 * The signal has its top and bottom flipped, for use in the range mapping
 * code where 0.0 corresponds to the main value, 1.0 to the second value.
 */
static sauMaybeUnused void sauEnvGen_run(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len, uint32_t note_dur) {
	if (!o->stage)
		return;
	uint32_t e_time[SAU_ENV_TIMES];
	uint32_t e_total = sauEnvGen_get_time_clamped(o, e_time, note_dur);
	uint32_t n;
	float s_val = 1.f - o->s_val;
	do {
		switch (o->stage) {
		default:
			o->keep_last = false;
			o->stage = 1; /* fall-through */
		case 1: n = sauEnvGen_run_line(o, buf, len, e_time,
					SAU_ENV_TIME_A, o->e_last, 0.f);
			break;
		case 2: n = sauEnvGen_run_line(o, buf, len, e_time,
					SAU_ENV_TIME_D, 0.f, s_val);
			break;
		case 3: n = sauEnvGen_run_sustain(o, buf, len, e_time,
					note_dur - e_total);
			break;
		case 4: n = sauEnvGen_run_line(o, buf, len, e_time,
					SAU_ENV_TIME_R, s_val, 1.f);
			break;
		}
		buf += len - n;
		len = n;
	} while (n > 0);
}
