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


typedef struct SGSEventVoiceData {
	uint voice_id;
	uint input_block_id;
	int panning_block_id; /* -1 if none */
	const int *operator_list;
	uint operator_c;
	uchar attr;
	float panning;
	SGSProgramValit valitpanning;
} SGSEventVoiceData;

typedef struct SGSEventOperatorData {
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
} SGSEventOperatorData;

typedef struct SGSEvent {
	int wait_ms;
	uint params;
	const SGSEventVoiceData *vo_data;
	const SGSEventOperatorData *op_data;
} SGSEvent;

typedef struct SGSSoundData {
	const SGSEvent *events;
	uint event_count,
	     block_count,
	     voice_count,
	     operator_count;
} SGSSoundData;

struct SGSInterpreter;

struct SGSInterpreter *SGS_create_interpreter();
void SGS_destroy_interpreter(struct SGSInterpreter *o);

SGSSoundData *SGS_interpreter_run(struct SGSInterpreter *o,
                                  SGSProgram *program);
void SGS_interpreter_clear(struct SGSInterpreter *o);
