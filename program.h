/* sgensys: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
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
#include "ramp.h"
#include "wave.h"

/*
 * Program types and definitions.
 */

/**
 * Time parameter flags.
 */
enum {
	SGS_TIMEP_SET      = 1<<0, // use the \a v_ms value or implicit value
	SGS_TIMEP_DEFAULT  = 1<<1, // the \a v_ms value set was default value
	SGS_TIMEP_IMPLICIT = 1<<2, // use an implicit value from other source
};

/**
 * Time parameter type.
 *
 * Holds data for a generic time parameter.
 */
typedef struct SGS_Time {
	uint32_t v_ms;
	uint8_t flags;
} SGS_Time;

/**
 * Voice parameter flags.
 */
enum {
	SGS_PVOP_GRAPH = 1<<0,
	SGS_PVOP_PAN = 1<<1,
	SGS_PVO_PARAMS = (1<<2) - 1,
};

/**
 * Operator parameter flags.
 */
enum {
	/* SGS_POPP_PAN = 1<<0, */
	SGS_POPP_WAVE = 1<<1,
	SGS_POPP_TIME = 1<<2,
	SGS_POPP_FREQ = 1<<3,
	SGS_POPP_DYNFREQ = 1<<4,
	SGS_POPP_PHASE = 1<<5,
	SGS_POPP_AMP = 1<<6,
	SGS_POPP_DYNAMP = 1<<7,
	SGS_POP_PARAMS = (1<<8) - 1,
};

/*
 * Voice ID constants.
 */
#define SGS_PVO_NO_ID  UINT16_MAX       /* voice ID missing */
#define SGS_PVO_MAX_ID (UINT16_MAX - 1) /* error if exceeded */

/*
 * Operator ID constants.
 */
#define SGS_POP_NO_ID  UINT32_MAX       /* operator ID missing */
#define SGS_POP_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

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

typedef struct SGS_ProgramOpList {
	uint32_t count;
	uint32_t ids[];
} SGS_ProgramOpList;

typedef struct SGS_ProgramVoData {
	const SGS_ProgramOpRef *graph;
	uint32_t op_count;
	uint32_t params;
	SGS_Ramp pan;
} SGS_ProgramVoData;

typedef struct SGS_ProgramOpData {
	const SGS_ProgramOpList *fmods, *pmods, *amods;
	uint32_t id;
	uint32_t params;
	SGS_Time time;
	uint8_t wave;
	float dynfreq, phase, dynamp;
	SGS_Ramp freq, amp;
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
	SGS_ProgramEvent **events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
	struct SGS_MemPool *mem; // holds memory for the specific program
} SGS_Program;

struct SGS_Script;
SGS_Program* SGS_build_Program(struct SGS_Script *restrict sd) sgsMalloclike;
void SGS_discard_Program(SGS_Program *restrict o);

void SGS_Program_print_info(const SGS_Program *restrict o);
