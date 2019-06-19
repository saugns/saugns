/* saugns: Audio program data and functions.
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
	SAU_TIMEP_SET      = 1<<0, // use the \a v_ms value or implicit value
	SAU_TIMEP_DEFAULT  = 1<<1, // the \a v_ms value set was default value
	SAU_TIMEP_IMPLICIT = 1<<2, // use an implicit value from other source
};

/**
 * Time parameter type.
 *
 * Holds data for a generic time parameter.
 */
typedef struct SAU_Time {
	uint32_t v_ms;
	uint8_t flags;
} SAU_Time;

/**
 * Voice parameter flags.
 */
enum {
	SAU_PVOP_GRAPH = 1<<0,
	SAU_PVO_PARAMS = (1<<1) - 1,
};

/**
 * Ramp use IDs.
 */
enum {
	SAU_PRAMP_PAN = 0,
	SAU_PRAMP_AMP,
	SAU_PRAMP_AMP2,
	SAU_PRAMP_FREQ,
	SAU_PRAMP_FREQ2,
};

/**
 * Operator parameter flags. For parameters without other tracking only.
 */
enum {
	SAU_POPP_WAVE = 1<<0,
	SAU_POPP_TIME = 1<<1,
	SAU_POPP_PHASE = 1<<2,
	SAU_POP_PARAMS = (1<<3) - 1,
};

/*
 * Voice ID constants.
 */
#define SAU_PVO_NO_ID  UINT16_MAX       /* voice ID missing */
#define SAU_PVO_MAX_ID (UINT16_MAX - 1) /* error if exceeded */

/*
 * Operator ID constants.
 */
#define SAU_POP_NO_ID  UINT32_MAX       /* operator ID missing */
#define SAU_POP_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

/**
 * Operator use types.
 */
enum {
	SAU_POP_CARR = 0,
	SAU_POP_AMOD,
	SAU_POP_FMOD,
	SAU_POP_PMOD,
	SAU_POP_USES,
};

typedef struct SAU_ProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} SAU_ProgramOpRef;

typedef struct SAU_ProgramOpList {
	uint32_t count;
	uint32_t ids[];
} SAU_ProgramOpList;

typedef struct SAU_ProgramVoData {
	const SAU_ProgramOpRef *graph;
	uint32_t op_count;
	uint32_t params;
} SAU_ProgramVoData;

typedef struct SAU_ProgramOpData {
	uint32_t params;
	uint8_t wave;
	uint8_t use_type;
	SAU_Time time;
	SAU_Ramp *freq, *freq2;
	SAU_Ramp *amp, *amp2;
	SAU_Ramp *pan;
	float phase;
	/* assigned after parsing */
	uint32_t id;
	const SAU_ProgramOpList *fmods, *pmods, *amods;
} SAU_ProgramOpData;

typedef struct SAU_ProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const SAU_ProgramVoData *vo_data;
	const SAU_ProgramOpData **op_data;
} SAU_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SAU_PMODE_AMP_DIV_VOICES = 1<<0,
};

struct SAU_Script;

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SAU_Program {
	SAU_ProgramEvent **events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
} SAU_Program;

struct SAU_Script;
bool SAU_build_Program(struct SAU_Script *restrict sd);

void SAU_Program_print_info(const SAU_Program *restrict o);
