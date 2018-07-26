/* sgensys: Audio generation program definition/creation module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
#include "wave.h"

/*
 * Program types and definitions.
 */

/**
 * Parameter flags
 */
enum {
	/* voice parameters */
	SGS_P_GRAPH = 1<<0,
	SGS_P_PANNING = 1<<1,
	SGS_P_VALITPANNING = 1<<2,
	SGS_P_VOATTR = 1<<3,
	/* operator parameters */
	SGS_P_ADJCS = 1<<4,
	SGS_P_WAVE = 1<<5,
	SGS_P_TIME = 1<<6,
	SGS_P_SILENCE = 1<<7,
	SGS_P_FREQ = 1<<8,
	SGS_P_VALITFREQ = 1<<9,
	SGS_P_DYNFREQ = 1<<10,
	SGS_P_PHASE = 1<<11,
	SGS_P_AMP = 1<<12,
	SGS_P_VALITAMP = 1<<13,
	SGS_P_DYNAMP = 1<<14,
	SGS_P_OPATTR = 1<<15
};

#define SGS_P_VOICE(flags) \
	((flags) & ( \
		SGS_P_GRAPH | \
		SGS_P_PANNING | \
		SGS_P_VALITPANNING | \
		SGS_P_VOATTR))

#define SGS_P_OPERATOR(flags) \
	((flags) & ( \
		SGS_P_ADJCS | \
		SGS_P_WAVE | \
		SGS_P_TIME | \
		SGS_P_SILENCE | \
		SGS_P_FREQ | \
		SGS_P_VALITFREQ | \
		SGS_P_DYNFREQ | \
		SGS_P_PHASE | \
		SGS_P_AMP | \
		SGS_P_VALITAMP | \
		SGS_P_DYNAMP | \
		SGS_P_OPATTR))

/**
 * Operator ID constants
 */
enum {
	SGS_OP_NO_ID = UINT32_MAX, /* operator ID missing */
	SGS_OP_MAX_ID = UINT32_MAX - 1, /* error if exceeded */
};

/**
 * Voice ID constants
 */
enum {
	SGS_VO_NO_ID = UINT16_MAX, /* voice ID missing */
	SGS_VO_MAX_ID = UINT16_MAX - 1, /* error if exceeded */
};

/**
 * Timing special values
 */
enum {
	SGS_TIME_INF = UINT32_MAX, /* special handling for nested operators */
	SGS_TIME_DEFAULT = UINT32_MAX, /* internal default for valits */
};

/**
 * Operator atttributes
 */
enum {
	SGS_ATTR_WAVEENV = 1<<0, // should be moved, set by interpreter
	SGS_ATTR_FREQRATIO = 1<<1,
	SGS_ATTR_DYNFREQRATIO = 1<<2,
	SGS_ATTR_VALITFREQ = 1<<3,
	SGS_ATTR_VALITFREQRATIO = 1<<4,
	SGS_ATTR_VALITAMP = 1<<5,
	SGS_ATTR_VALITPANNING = 1<<6
};

/**
 * Value iteration types
 */
enum {
	SGS_VALIT_NONE = 0, /* when none given */
	SGS_VALIT_LIN,
	SGS_VALIT_EXP,
	SGS_VALIT_LOG
};

typedef struct SGS_ProgramGraph {
	uint32_t opc;
	uint32_t ops[1]; /* sized to opc */
} SGS_ProgramGraph;

typedef struct SGS_ProgramGraphAdjcs {
	uint32_t fmodc;
	uint32_t pmodc;
	uint32_t amodc;
	uint32_t level;  /* index for buffer used to store result to use if
	                    node revisited when traversing the graph. */
	uint32_t adjcs[1]; /* sized to total number */
} SGS_ProgramGraphAdjcs;

typedef struct SGS_ProgramValit {
	uint32_t time_ms, pos_ms;
	float goal;
	uint8_t type;
} SGS_ProgramValit;

typedef struct SGS_ProgramVoiceData {
	const SGS_ProgramGraph *graph;
	uint8_t attr;
	float panning;
	SGS_ProgramValit valitpanning;
} SGS_ProgramVoiceData;

typedef struct SGS_ProgramOperatorData {
	const SGS_ProgramGraphAdjcs *adjcs;
	uint32_t operator_id;
	uint8_t attr;
	SGS_wave_t wave;
	uint32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGS_ProgramValit valitfreq, valitamp;
} SGS_ProgramOperatorData;

typedef struct SGS_ProgramEvent {
	uint32_t wait_ms;
	uint32_t params;
	uint32_t voice_id; /* needed for both voice and operator data */
	const SGS_ProgramVoiceData *voice;
	const SGS_ProgramOperatorData *operator;
} SGS_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SGS_PROG_AMP_DIV_VOICES = 1<<0,
};

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SGS_Program {
	const SGS_ProgramEvent *events;
	size_t event_count;
	uint32_t operator_count;
	uint16_t voice_count;
	uint16_t flags;
	const char *name;
	size_t odata_count,
	       vdata_count;
} SGS_Program;

struct SGS_ParseResult;
SGS_Program *SGS_create_Program(struct SGS_ParseResult *parse);
void SGS_destroy_Program(SGS_Program *o);

void SGS_Program_print_info(SGS_Program *o);
