/* sgensys: Sound generator module.
 * Copyright (c) 2011-2013, 2017 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"

typedef struct SGSResultVoiceData {
	uint32_t voice_id;
	uint32_t input_block_id;
	int32_t panning_block_id; /* -1 if none */
	const int32_t *operator_list;
	uint32_t operator_c;
	uint8_t attr;
	float panning;
	SGSProgramValit valitpanning;
} SGSResultVoiceData;

typedef struct SGSResultOperatorData {
	uint32_t operator_id;
	uint32_t output_block_id;
	int32_t freq_block_id, /* -1 if none */
		freq_mod_block_id,
		phase_mod_block_id,
		amp_block_id,
		amp_mod_block_id;
	uint8_t attr, wave;
	int32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGSProgramValit valitfreq, valitamp;
} SGSResultOperatorData;

typedef struct SGSResultEvent {
	int32_t wait_ms;
	uint32_t params;
	const SGSResultVoiceData *voice;
	const SGSResultOperatorData *operator;
} SGSResultEvent;

struct SGSResult {
	const SGSResultEvent *events;
	uint32_t eventc,
		blockc,
		voicec,
		operatorc;
};
typedef struct SGSResult SGSResult;

/*
 * SGSGenerator
 */

struct SGSGenerator;
typedef struct SGSGenerator SGSGenerator;

SGSGenerator* SGS_generator_create(uint32_t srate, SGSResult *prg);
void SGS_generator_destroy(SGSGenerator *o);
bool SGS_generator_run(SGSGenerator *o, int16_t *buf, size_t buf_len,
		size_t *gen_len);
