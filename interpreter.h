/* sgensys sound program interpreter
 * Copyright (c) 2013-2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "ptrarr.h"

struct SGSEventVoiceData {
	uint voice_id;
	uint input_block_id;
	int panning_block_id; /* -1 if none */
	const int *operator_list;
	uint operator_count;
	uchar attr;
	float panning;
	SGSProgramValit valitpanning;
};
typedef const struct SGSEventVoiceData *SGSEventVoiceData_t;

struct SGSEventOperatorData {
	uint operator_id;
	uint output_block_id;
	int freq_block_id, /* -1 if none */
	    freq_mod_block_id,
	    phase_mod_block_id,
	    amp_block_id,
	    amp_mod_block_id;
	uchar attr, wave;
	int time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGSProgramValit valitfreq, valitamp;
};
typedef const struct SGSEventOperatorData *SGSEventOperatorData_t;

struct SGSResultEvent {
	int wait_ms;
	uint params;
	SGSEventVoiceData_t voice_data;
	SGSEventOperatorData_t operator_data;
};
typedef const struct SGSResultEvent *SGSResultEvent_t;

struct SGSResult {
	SGSResultEvent_t events;
	uint event_count,
	     block_count,
	     voice_count,
	     operator_count;
};
typedef const struct SGSResult *SGSResult_t;

struct SGSInterpreter;
typedef struct SGSInterpreter *SGSInterpreter_t;

SGSInterpreter_t SGS_create_interpreter();
void SGS_destroy_interpreter(SGSInterpreter_t o);

SGSResult_t SGS_interpreter_run(SGSInterpreter_t o,
                                struct SGSProgram *program);
void SGS_interpreter_get_results(SGSInterpreter_t o,
                                 struct SGSPtrArr *results);

void SGS_interpreter_clear(SGSInterpreter_t o);
