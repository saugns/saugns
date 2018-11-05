/* sgensys: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
#include "program/param.h"
#include "program/wave.h"

/*
 * Program types and definitions.
 */

/**
 * Voice parameter flags
 */
enum {
	SGS_PVOP_OPLIST = 1<<0,
	SGS_PVOP_PAN = 1<<1,
};

/**
 * Operator parameter flags
 */
enum {
	SGS_POPP_ADJCS = 1<<0,
	SGS_POPP_WAVE = 1<<1,
	SGS_POPP_TIME = 1<<2,
	SGS_POPP_SILENCE = 1<<3,
	SGS_POPP_FREQ = 1<<4,
	SGS_POPP_DYNFREQ = 1<<5,
	SGS_POPP_PHASE = 1<<6,
	SGS_POPP_AMP = 1<<7,
	SGS_POPP_DYNAMP = 1<<8,
};

/**
 * Voice ID constants
 */
enum {
	SGS_PVO_NO_ID = UINT16_MAX, /* voice ID missing */
	SGS_PVO_MAX_ID = UINT16_MAX - 1, /* error if exceeded */
};

/**
 * Operator ID constants
 */
enum {
	SGS_POP_NO_ID = UINT32_MAX, /* operator ID missing */
	SGS_POP_MAX_ID = UINT32_MAX - 1, /* error if exceeded */
};

/**
 * Timing special values
 */
enum {
	SGS_TIME_INF = UINT32_MAX, /* special handling for nested operators */
	SGS_TIME_DEFAULT = UINT32_MAX, /* default for slopes while parsing */
};

/**
 * Operator use types.
 */
enum {
	SGS_POP_CARR = 0,
	SGS_POP_FMOD,
	SGS_POP_PMOD,
	SGS_POP_AMOD,
	SGS_POP_USES,
};

typedef struct SGS_ProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} SGS_ProgramOpRef;

typedef struct SGS_ProgramOpGraph {
	uint32_t opc;
	uint32_t ops[1]; /* sized to opc */
} SGS_ProgramOpGraph;

typedef struct SGS_ProgramOpAdjcs {
	uint32_t fmodc;
	uint32_t pmodc;
	uint32_t amodc;
	uint32_t adjcs[1]; /* sized to total number */
} SGS_ProgramOpAdjcs;

typedef struct SGS_ProgramVoData {
	const SGS_ProgramOpRef *op_list;
	uint32_t op_count;
	uint32_t params;
	SGS_TimedParam pan;
} SGS_ProgramVoData;

typedef struct SGS_ProgramOpData {
	const SGS_ProgramOpAdjcs *adjcs;
	uint32_t id;
	uint32_t params;
	uint8_t wave;
	uint32_t time_ms, silence_ms;
	float dynfreq, phase, dynamp;
	SGS_TimedParam freq, amp;
} SGS_ProgramOpData;

typedef struct SGS_ProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const SGS_ProgramVoData *vo_data;
	const SGS_ProgramOpData *op_data;
} SGS_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SGS_PMODE_AMP_DIV_VOICES = 1<<0,
};

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SGS_Program {
	SGS_ProgramEvent *events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
} SGS_Program;

struct SGS_Script;
SGS_Program* SGS_build_Program(struct SGS_Script *restrict sd);
void SGS_discard_Program(SGS_Program *restrict o);

void SGS_Program_print_info(SGS_Program *restrict o);
