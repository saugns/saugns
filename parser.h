/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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
#include "plist.h"

/**
 * Parse operator data flags.
 */
enum {
	SGS_PSOD_OPERATOR_LATER_USED = 1<<0,
	SGS_PSOD_MULTIPLE_OPERATORS = 1<<1,
	SGS_PSOD_OPERATOR_NESTED = 1<<2,
	SGS_PSOD_LABEL_ALLOC = 1<<3,
	SGS_PSOD_TIME_DEFAULT = 1<<4,
	SGS_PSOD_SILENCE_ADDED = 1<<5,
};

/**
 * Node type for operator data.
 */
typedef struct SGS_ParseOperatorData {
	struct SGS_ParseEventData *event;
	SGS_PList on_next; /* all immediate forward refs for op(s) */
	struct SGS_ParseOperatorData *on_prev; /* preceding for same op(s) */
	struct SGS_ParseOperatorData *next_bound;
	uint32_t od_flags;
	const char *label;
	/* operator parameters */
	uint32_t operator_id; /* not used by parser; for program module */
	uint32_t operator_params;
	uint8_t attr;
	SGS_wave_t wave;
	uint32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGS_ProgramValit valitfreq, valitamp;
	/* node adjacents in operator linkage graph */
	SGS_PList fmods, pmods, amods;
} SGS_ParseOperatorData;

/**
 * Parse event data flags.
 */
enum {
	SGS_PSED_VOICE_LATER_USED = 1<<0,
	SGS_PSED_ADD_WAIT_DURATION = 1<<1,
};

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SGS_ParseEventData {
	struct SGS_ParseEventData *next;
	struct SGS_ParseEventData *groupfrom;
	struct SGS_ParseEventData *composite;
	uint32_t wait_ms;
	SGS_PList operators; /* operators included in event */
	uint32_t ed_flags;
	/* voice parameters */
	uint32_t voice_id; /* not used by parser; for program module */
	uint32_t voice_params;
	struct SGS_ParseEventData *voice_prev; /* preceding event for voice */
	uint8_t voice_attr;
	float panning;
	SGS_ProgramValit valitpanning;
	SGS_PList graph;
} SGS_ParseEventData;

/**
 * Flags set after parsing the setting of a script option by a script.
 */
enum {
	SGS_PSSO_AMPMULT = 1<<0,
	SGS_PSSO_A4_FREQ = 1<<1,
	SGS_PSSO_DEF_TIME = 1<<2,
	SGS_PSSO_DEF_FREQ = 1<<3,
	SGS_PSSO_DEF_RATIO = 1<<4,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SGS_ParseScriptOptions {
	uint32_t changed; // flags (SGS_PSSO_*) set upon change by script
	float ampmult;    // amplitude multiplier for non-modulator operators
	float A4_freq;    // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_ratio;
} SGS_ParseScriptOptions;

/**
 * Type returned after processing a file.
 */
typedef struct SGS_ParseResult {
	SGS_ParseEventData *events;
	const char *name; // currently simply set to the filename
	SGS_ParseScriptOptions sopt;
} SGS_ParseResult;

struct SGS_Parser;
typedef struct SGS_Parser SGS_Parser;

SGS_Parser *SGS_create_Parser(void);
void SGS_destroy_Parser(SGS_Parser *o);

SGS_ParseResult *SGS_Parser_process(SGS_Parser *o, const char *fname);
void SGS_Parser_get_results(SGS_Parser *o, SGS_PList *dst);
void SGS_Parser_clear(SGS_Parser *o);
