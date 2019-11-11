/* saugns: Audio result definitions.
 * Copyright (c) 2013-2014, 2018-2019 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"

/*
 * Result data types. Represent audio to be rendered.
 */

struct SAU_ResultVoiceData {
	uint32_t op_id;
	uint32_t input_block_id;
	int32_t panning_block_id; /* -1 if none */
	const int32_t *op_list;
	uint32_t op_count;
	uint8_t attr;
	float panning;
//	SAU_ProgramValit valitpanning;
};
typedef struct SAU_ResultVoiceData SAU_ResultVoiceData;

struct SAU_ResultOperatorData {
	uint32_t id;
	uint32_t output_block_id;
	int32_t freq_block_id, /* -1 if none */
		freq_mod_block_id,
		phase_mod_block_id,
		amp_block_id,
		amp_mod_block_id;
	uint8_t attr, wave;
	int32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
//	SAU_ProgramValit valitfreq, valitamp;
};
typedef struct SAU_ResultOperatorData SAU_ResultOperatorData;

struct SAU_ResultEvent {
	int32_t wait_ms;
	const SAU_ResultVoiceData *vo_data;
	const SAU_ResultOperatorData *op_data;
};
typedef struct SAU_ResultEvent SAU_ResultEvent;

struct SAU_Result {
	SAU_ResultEvent *events;
	size_t ev_count;
	uint32_t block_count;
	uint32_t op_count;
	uint16_t vo_count;
	uint16_t mode;
	const char *name;
	SAU_ResultOperatorData *odata_nodes;
	SAU_ResultVoiceData *vdata_nodes;
};
typedef struct SAU_Result SAU_Result;
