/* SAU library: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "line.h"
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
typedef struct sauTime {
	uint32_t v_ms;
	uint8_t flags;
} sauTime;

#define sauTime_VALUE(v_ms, implicit) (sauTime){ \
	(v_ms), SAU_TIMEP_SET | \
		((implicit) ? (SAU_TIMEP_DEFAULT | SAU_TIMEP_IMPLICIT) : 0) \
}

#define sauTime_DEFAULT(v_ms, implicit) (sauTime){ \
	(v_ms), SAU_TIMEP_DEFAULT | ((implicit) ? SAU_TIMEP_IMPLICIT : 0) \
}

/**
 * Swept parameter IDs.
 */
enum {
	SAU_PSWEEP_PAN = 0,
	SAU_PSWEEP_AMP,
	SAU_PSWEEP_AMP2,
	SAU_PSWEEP_FREQ,
	SAU_PSWEEP_FREQ2,
};

/**
 * Operator parameter flags. For parameters without other tracking only.
 */
enum {
	SAU_POPP_TIME = 1<<0,
	SAU_POPP_PHASE = 1<<1,
	SAU_POPP_WAVE = 1<<2,
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

typedef struct sauProgramIDArr {
	uint32_t count;
	uint32_t ids[];
} sauProgramIDArr;

/**
 * Operator use types.
 */
enum {
	SAU_POP_CARR = 0,
	SAU_POP_AMOD,
	SAU_POP_RAMOD,
	SAU_POP_FMOD,
	SAU_POP_RFMOD,
	SAU_POP_PMOD,
	SAU_POP_FPMOD,
	SAU_POP_USES,
};

typedef struct sauProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} sauProgramOpRef;

typedef struct sauProgramVoData {
	const sauProgramOpRef *op_list;
	uint32_t op_count;
} sauProgramVoData;

typedef struct sauProgramOpData {
	uint32_t id;
	uint32_t params;
	sauTime time;
	sauLine *pan;
	sauLine *amp, *amp2;
	sauLine *freq, *freq2;
	uint32_t phase;
	uint8_t wave;
	const sauProgramIDArr *amods, *ramods;
	const sauProgramIDArr *fmods, *rfmods;
	const sauProgramIDArr *pmods, *fpmods;
} sauProgramOpData;

typedef struct sauProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const sauProgramVoData *vo_data;
	const sauProgramOpData *op_data;
} sauProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SAU_PMODE_AMP_DIV_VOICES = 1<<0,
};

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct sauProgram {
	const sauProgramEvent *events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint8_t op_nest_depth;
	uint32_t duration_ms;
	const char *name;
	struct sauMempool *mp; // holds memory for the specific program
	struct sauScript *parse; // parser output used to build program
} sauProgram;

struct sauScript;
sauProgram* sau_build_Program(struct sauScript *restrict parse,
		bool keep_parse) sauMalloclike;
void sau_discard_Program(sauProgram *restrict o);

void sauProgram_print_info(const sauProgram *restrict o);
