/* SAU library: Envelope generator module.
 * Copyright (c) 2025 Joel K. Pettersson
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
	uint8_t type; // 0 if unused, otherwise indicates what to run
	uint8_t stage;
	float s_val;
	uint32_t i;
} sauEnvGen;

static uint32_t sauEnvGen_get_min_time(sauEnvGen *restrict o) {
	uint32_t e_total = 0;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) e_total += o->time[i];
	return e_total;
}

static void sauEnvGen_set_par(sauEnvGen *restrict o,
		const sauEnvPar *restrict src, uint32_t srate) {
	for (int i = 0; i < SAU_ENV_TIMES; ++i) {
		if (!(src->flags & (1U<<i))) continue;
		o->time[i] = sau_ms_in_samples(src->time_ms[i], srate, NULL);
	}
	if (src->flags & SAU_ENVP_S)
		o->s_val = src->s_val;
	o->type = sauEnvGen_get_min_time(o) > 0; // TODO: more than just if used
}

static uint32_t sauEnvGen_run_attack(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len) {
	uint32_t n = len, rem = 0, time = o->time[SAU_ENV_TIME_A];
	if (o->i + n > time) {
		rem = (o->i + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	//
	float a = 1.f / time;
	for (uint32_t i = 0; i < n; ++i) {
		float x = (i + o->i) * a;
		buf[i] += 1.f - x;
	}
	//
	o->i += n;
	if (o->i >= time) {
		++o->stage;
		o->i = 0;
	}
	return rem;
}

static uint32_t sauEnvGen_run_decay(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len) {
	uint32_t n = len, rem = 0, time = o->time[SAU_ENV_TIME_D];
	if (o->i + n > time) {
		rem = (o->i + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	//
	float a = (1.f - o->s_val) * 1.f / time;
	for (uint32_t i = 0; i < n; ++i) {
		float x = (time - (i + o->i)) * a + o->s_val;
		buf[i] += 1.f - x;
	}
	//
	o->i += n;
	if (o->i >= time) {
		++o->stage;
		o->i = 0;
	}
	return rem;
}

static uint32_t sauEnvGen_run_sustain(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len, uint32_t note_dur) {
	uint32_t e_total = sauEnvGen_get_min_time(o);
	if (e_total > note_dur) e_total = note_dur; // better handling?

	uint32_t n = len, rem = 0, time = note_dur - e_total;
	if (o->i + n > time) {
		rem = (o->i + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	//
	if (o->s_val != 1.f)
	for (uint32_t i = 0; i < n; ++i) {
		float x = o->s_val;
		buf[i] += 1.f - x;
	}
	//
	o->i += n;
	if (o->i >= time) {
		++o->stage;
		o->i = 0;
	}
	return rem;
}

static uint32_t sauEnvGen_run_release(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len) {
	uint32_t n = len, rem = 0, time = o->time[SAU_ENV_TIME_R];
	if (o->i + n > time) {
		rem = (o->i + n) - time;
		if (rem > len) rem = len;
		n -= rem;
	}
	//
	float a = o->s_val * 1.f / time;
	for (uint32_t i = 0; i < n; ++i) {
		float x = (time - (i + o->i)) * a;
		buf[i] += 1.f - x;
	}
	//
	o->i += n;
	if (o->i >= time) {
		++o->stage;
		o->i = 0;
	}
	return rem;
}

/**
 * Run for \p len samples, adding to \p buf a signal for applying an
 * envelope to a parameter.
 *
 * The signal has its top and bottom flipped, for use in the range mapping
 * code where 0.0 corresponds to the main value, 1.0 to the second value.
 */
static sauMaybeUnused void sauEnvGen_run(sauEnvGen *restrict o,
		float *restrict buf, uint32_t len, uint32_t note_dur) {
	if (!o->type)
		return;
	uint32_t n;
	do {
		switch (o->stage) {
		default: o->stage = 0; /* fall-through */
		case 0: n = sauEnvGen_run_attack(o, buf, len); break;
		case 1: n = sauEnvGen_run_decay(o, buf, len); break;
		case 2: n = sauEnvGen_run_sustain(o, buf, len, note_dur); break;
		case 3: n = sauEnvGen_run_release(o, buf, len); break;
		}
		buf += len - n;
		len = n;
	} while (n > 0);
}
