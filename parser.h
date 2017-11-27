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
 * <https://www.gnu.org/licenses/>.
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
typedef struct SGSParseOperatorData {
	struct SGSParseEventData *event;
	SGSPList on_next; /* all immediate forward refs for op(s) */
	struct SGSParseOperatorData *on_prev; /* preceding for same op(s) */
	struct SGSParseOperatorData *next_bound;
	uint32_t od_flags;
	const char *label;
	/* operator parameters */
	uint32_t operator_id; /* not used by parser; for program module */
	uint32_t operator_params;
	uint8_t attr;
	SGS_wave_t wave;
	int32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGSProgramValit valitfreq, valitamp;
	/* node adjacents in operator linkage graph */
	SGSPList fmods, pmods, amods;
} SGSParseOperatorData;

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
typedef struct SGSParseEventData {
	struct SGSParseEventData *next;
	struct SGSParseEventData *groupfrom;
	struct SGSParseEventData *composite;
	int32_t wait_ms;
	SGSPList operators; /* operators included in event */
	uint32_t ed_flags;
	/* voice parameters */
	uint32_t voice_id; /* not used by parser; for program module */
	uint32_t voice_params;
	struct SGSParseEventData *voice_prev; /* preceding event for voice */
	uint8_t voice_attr;
	float panning;
	SGSProgramValit valitpanning;
	SGSPList graph;
} SGSParseEventData;

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
typedef struct SGSParseScriptOptions {
	uint32_t changed; // flags (SGS_PSSO_*) set upon change by script
	float ampmult;    // amplitude multiplier for non-modulator operators
	float A4_freq;    // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	int32_t def_time_ms;
	float def_freq,
	      def_ratio;
} SGSParseScriptOptions;

/**
 * Type returned after processing a file.
 */
typedef struct SGSParseResult {
	SGSParseEventData *events;
	const char *name; // currently simply set to the filename
	SGSParseScriptOptions sopt;
} SGSParseResult;

struct SGSParser;
typedef struct SGSParser SGSParser;

SGSParser *SGS_create_parser(void);
void SGS_destroy_parser(SGSParser *o);

SGSParseResult *SGS_parser_process(SGSParser *o, const char *fname);
void SGS_parser_get_results(SGSParser *o, SGSPList *dst);
void SGS_parser_clear(SGSParser *o);
