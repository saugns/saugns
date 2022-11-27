/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

struct SGSOperatorNode;

/*
 * Parsing nodes.
 */

enum {
	/* parse flags */
	ON_OPERATOR_LATER_USED = 1<<0,
	ON_MULTIPLE_OPERATORS = 1<<1,
	ON_OPERATOR_NESTED = 1<<2,
	ON_TIME_DEFAULT = 1<<3,
	ON_SILENCE_ADDED = 1<<4,
	ON_HAS_COMPSTEP = 1<<5,
};

/**
 * Container node for linked list, used for nesting.
 */
typedef struct SGSListNode {
	struct SGSOperatorNode *first_on;
} SGSListNode;

typedef struct SGSOperatorNode {
	struct SGSEventNode *event;
	struct SGSOperatorNode *next; /* next in list, scope, grouping... */
	struct SGSOperatorNode *on_prev; /* preceding for same operator(s) */
	uint32_t on_flags;
	/* operator parameters */
	uint32_t operator_id; /* not set by parser; for later use (program.c) */
	uint32_t operator_params;
	uint8_t attr;
	uint8_t wave;
	int32_t time_ms, silence_ms;
	float freq, dynfreq, amp, dynamp;
	int32_t phase;
	SGSProgramValit valitfreq, valitamp;
	/* node adjacents in operator linkage graph */
	SGSListNode *amods, *fmods, *pmods;
} SGSOperatorNode;

enum {
	/* parse flags */
	EN_VOICE_LATER_USED = 1<<0,
	EN_ADD_WAIT_DURATION = 1<<1,
};

struct SGSEventBranch;

typedef struct SGSEventNode {
	struct SGSEventNode *next;
	struct SGSEventNode *group_backref;
	struct SGSEventBranch *forks;
	int32_t wait_ms;
	SGSListNode operators; /* operators included in event */
	uint32_t en_flags;
	/* voice parameters */
	uint32_t voice_id; /* not set by parser; for later use (program.c) */
	uint32_t voice_params;
	struct SGSEventNode *voice_prev; /* preceding event for same voice */
	uint8_t voice_attr;
	float panning;
	SGSProgramValit valitpanning;
	SGSListNode graph;
} SGSEventNode;

/**
 * Script data option flags.
 *
 * Set after parsing the setting of script options in a script.
 */
enum {
	SGS_SOPT_AMPMULT = 1<<0,
	SGS_SOPT_A4_FREQ = 1<<1,
	SGS_SOPT_DEF_TIME = 1<<2,
	SGS_SOPT_DEF_FREQ = 1<<3,
	SGS_SOPT_DEF_RATIO = 1<<4,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SGSScriptOptions {
	uint32_t set;  // flags (SGS_SOPT_*) set upon change by script
	float ampmult; // amplitude multiplier for non-modulator operators
	float A4_freq; // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	int32_t def_time_ms;
	float def_freq,
	      def_ratio;
} SGSScriptOptions;

typedef struct SGSParserResult {
	SGSEventNode *events;
	const char *name; // currently simply set to the filename
	SGSScriptOptions sopt;
	struct SGSMempool *mp;
} SGSParserResult;

SGSParserResult *SGSParser_parse(const char *filename);
void SGSParser_destroy_result(SGSParserResult *pr);
