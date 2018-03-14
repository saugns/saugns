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
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "wave.h"

/*
 * Program types and definitions.
 */

/**
 * Voice parameter flags
 */
enum {
	SGS_VOP_GRAPH = 1<<0,
	SGS_VOP_OPLIST = 1<<1,
	SGS_VOP_PANNING = 1<<2,
	SGS_VOP_VALITPANNING = 1<<3,
	SGS_VOP_ATTR = 1<<4,
};

/**
 * Operator parameter flags
 */
enum {
	SGS_OPP_ADJCS = 1<<0,
	SGS_OPP_WAVE = 1<<1,
	SGS_OPP_TIME = 1<<2,
	SGS_OPP_SILENCE = 1<<3,
	SGS_OPP_FREQ = 1<<4,
	SGS_OPP_VALITFREQ = 1<<5,
	SGS_OPP_DYNFREQ = 1<<6,
	SGS_OPP_PHASE = 1<<7,
	SGS_OPP_AMP = 1<<8,
	SGS_OPP_VALITAMP = 1<<9,
	SGS_OPP_DYNAMP = 1<<10,
	SGS_OPP_ATTR = 1<<11,
};

/**
 * Voice ID constants
 */
enum {
	SGS_VO_NO_ID = UINT16_MAX, /* voice ID missing */
	SGS_VO_MAX_ID = UINT16_MAX - 1, /* error if exceeded */
};

/**
 * Operator ID constants
 */
enum {
	SGS_OP_NO_ID = UINT32_MAX, /* operator ID missing */
	SGS_OP_MAX_ID = UINT32_MAX - 1, /* error if exceeded */
};

/**
 * Timing special values
 */
enum {
	SGS_TIME_INF = UINT32_MAX, /* special handling for nested operators */
	SGS_TIME_DEFAULT = UINT32_MAX, /* internal default for valits */
};

/**
 * Voice atttributes
 */
enum {
	SGS_VOAT_VALITPANNING = 1<<0,
};

/**
 * Operator atttributes
 */
enum {
	SGS_OPAT_WAVEENV = 1<<0, // should be moved, set by interpreter
	SGS_OPAT_FREQRATIO = 1<<1,
	SGS_OPAT_DYNFREQRATIO = 1<<2,
	SGS_OPAT_VALITFREQ = 1<<3,
	SGS_OPAT_VALITFREQRATIO = 1<<4,
	SGS_OPAT_VALITAMP = 1<<5,
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

/**
 * Operator use types.
 */
enum {
	SGS_OP_CARR = 0,
	SGS_OP_FMOD,
	SGS_OP_PMOD,
	SGS_OP_AMOD,
	SGS_OP_USES,
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

typedef struct SGS_ProgramValit {
	uint32_t time_ms, pos_ms;
	float goal;
	uint8_t type;
} SGS_ProgramValit;

typedef struct SGS_ProgramVoData {
	const SGS_ProgramOpRef *op_list;
	uint32_t op_count;
	uint32_t params;
	uint8_t attr;
	float panning;
	SGS_ProgramValit valitpanning;
} SGS_ProgramVoData;

typedef struct SGS_ProgramOpData {
	const SGS_ProgramOpAdjcs *adjcs;
	uint32_t id;
	uint32_t params;
	uint8_t attr;
	SGS_wave_t wave;
	uint32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGS_ProgramValit valitfreq, valitamp;
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
	SGS_PROG_AMP_DIV_VOICES = 1<<0,
};

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SGS_Program {
	SGS_ProgramEvent *events;
	size_t ev_count;
	uint16_t flags;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
} SGS_Program;

struct SGS_ParseResult;
SGS_Program *SGS_create_Program(struct SGS_ParseResult *parse);
void SGS_destroy_Program(SGS_Program *o);

void SGS_Program_print_info(SGS_Program *o);
