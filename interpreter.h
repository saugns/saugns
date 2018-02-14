/* sgensys: Audio program interpreter module.
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

/*
 * Result data types. Represent audio to be rendered.
 */

struct SGSResultVoiceData {
	uint32_t voice_id;
	uint32_t input_block_id;
	int32_t panning_block_id; /* -1 if none */
	const int32_t *operator_list;
	uint32_t operator_count;
	uint8_t attr;
	float panning;
	SGSProgramValit valitpanning;
};
typedef const struct SGSResultVoiceData *SGSResultVoiceData_t;

struct SGSResultOperatorData {
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
};
typedef const struct SGSResultOperatorData *SGSResultOperatorData_t;

struct SGSResultEvent {
	int32_t wait_ms;
	uint32_t params;
	SGSResultVoiceData_t voice_data;
	SGSResultOperatorData_t operator_data;
};
typedef const struct SGSResultEvent *SGSResultEvent_t;

struct SGSResult {
	SGSResultEvent_t events;
	uint32_t event_count,
		block_count,
		voice_count,
		operator_count;
};
typedef const struct SGSResult *SGSResult_t;

/*
 * Interpreter for audio program. Produces results to render.
 */

struct SGSInterpreter;
typedef struct SGSInterpreter *SGSInterpreter_t;

SGSInterpreter_t SGS_create_interpreter();
void SGS_destroy_interpreter(SGSInterpreter_t o);

SGSResult_t SGS_interpreter_run(SGSInterpreter_t o,
                                struct SGSProgram *program);
void SGS_interpreter_get_results(SGSInterpreter_t o,
                                 SGSResult_t **results, size_t **count);

void SGS_interpreter_clear(SGSInterpreter_t o);
