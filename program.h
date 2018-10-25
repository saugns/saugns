/* saugns: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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
	SAU_PVOP_OPLIST = 1<<0,
	SAU_PVOP_PAN = 1<<1,
};

/**
 * Operator parameter flags
 */
enum {
	SAU_POPP_ADJCS = 1<<0,
	SAU_POPP_WAVE = 1<<1,
	SAU_POPP_TIME = 1<<2,
	SAU_POPP_SILENCE = 1<<3,
	SAU_POPP_FREQ = 1<<4,
	SAU_POPP_DYNFREQ = 1<<5,
	SAU_POPP_PHASE = 1<<6,
	SAU_POPP_AMP = 1<<7,
	SAU_POPP_DYNAMP = 1<<8,
};

/**
 * Voice ID constants
 */
enum {
	SAU_PVO_NO_ID = UINT16_MAX, /* voice ID missing */
	SAU_PVO_MAX_ID = UINT16_MAX - 1, /* error if exceeded */
};

/**
 * Operator ID constants
 */
enum {
	SAU_POP_NO_ID = UINT32_MAX, /* operator ID missing */
	SAU_POP_MAX_ID = UINT32_MAX - 1, /* error if exceeded */
};

/**
 * Timing special values
 */
enum {
	SAU_TIME_INF = UINT32_MAX, /* special handling for nested operators */
	SAU_TIME_DEFAULT = UINT32_MAX, /* default for slopes while parsing */
};

/**
 * Operator use types.
 */
enum {
	SAU_POP_CARR = 0,
	SAU_POP_FMOD,
	SAU_POP_PMOD,
	SAU_POP_AMOD,
	SAU_POP_USES,
};

typedef struct SAU_ProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} SAU_ProgramOpRef;

typedef struct SAU_ProgramOpGraph {
	uint32_t opc;
	uint32_t ops[1]; /* sized to opc */
} SAU_ProgramOpGraph;

typedef struct SAU_ProgramOpAdjcs {
	uint32_t fmodc;
	uint32_t pmodc;
	uint32_t amodc;
	uint32_t adjcs[1]; /* sized to total number */
} SAU_ProgramOpAdjcs;

typedef struct SAU_ProgramVoData {
	const SAU_ProgramOpRef *op_list;
	uint32_t op_count;
	uint32_t params;
	SAU_TimedParam pan;
} SAU_ProgramVoData;

typedef struct SAU_ProgramOpData {
	const SAU_ProgramOpAdjcs *adjcs;
	uint32_t id;
	uint32_t params;
	uint8_t wave;
	uint32_t time_ms, silence_ms;
	float dynfreq, phase, dynamp;
	SAU_TimedParam freq, amp;
} SAU_ProgramOpData;

typedef struct SAU_ProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const SAU_ProgramVoData *vo_data;
	const SAU_ProgramOpData *op_data;
} SAU_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SAU_PMODE_AMP_DIV_VOICES = 1<<0,
};

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SAU_Program {
	SAU_ProgramEvent *events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
} SAU_Program;

struct SAU_Script;
SAU_Program* SAU_build_Program(struct SAU_Script *restrict sd);
void SAU_discard_Program(SAU_Program *restrict o);

void SAU_Program_print_info(SAU_Program *restrict o);
