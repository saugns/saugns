/* SAU library: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2024 Joel K. Pettersson
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
	SAU_PSWEEP_PMA,
};

enum {
	SAU_POBJT_LIST = 0,
	SAU_POBJT_OP,
	SAU_POBJT_TYPES,
};

/* Macro used to declare and define program op types sets of items. */
#define SAU_POPT__ITEMS(X) \
	X(noise, 'N') \
	X(wave,  'W') \
	X(raseg, 'R') \
	//
#define SAU_POPT__X_ID(NAME, LABELC) SAU_POPT_N_##NAME,

enum {
	SAU_POPT__ITEMS(SAU_POPT__X_ID)
	SAU_POPT_TYPES,
};

/** True if the given program op type is an oscillator type. */
#define sau_pop_is_osc(type_id) ((type_id) >= SAU_POPT_N_wave)

/**
 * Operator parameter flags. For parameters without other tracking only.
 */
enum {
	SAU_POPP_TIME = 1<<0,
	SAU_POPP_PHASE = 1<<1,
	SAU_POPP_MODE = 1<<2, // type-specific data
	SAU_POP_PARAMS = (1<<3) - 1,
};

/* Macro used to declare and define noise type sets of items. */
#define SAU_NOISE__ITEMS(X) \
	X(wh) \
	X(gw) \
	X(bw) \
	X(tw) \
	X(re) \
	X(vi) \
	X(bv) \
	//
#define SAU_NOISE__X_ID(NAME) SAU_NOISE_N_##NAME,
#define SAU_NOISE__X_NAME(NAME) #NAME,

/**
 * Noise types.
 */
enum {
	SAU_NOISE__ITEMS(SAU_NOISE__X_ID)
	SAU_NOISE_NAMED
};

/** Names of noise types, with an extra NULL pointer at the end. */
extern const char *const sauNoise_names[SAU_NOISE_NAMED + 1];

/** Random segments option data. */
typedef struct sauRasOpt {
	uint8_t line; // line module type; is first, to match sauPOPMode main
	uint8_t func;
	uint8_t level;
	uint8_t flags;
} sauRasOpt;

/** Random segments functions. */
enum {
	SAU_RAS_F_RAND = 0,
	SAU_RAS_F_GAUSS,
	SAU_RAS_F_BIN,
	SAU_RAS_F_TERN,
	SAU_RAS_F_FIXED,
	SAU_RAS_FUNCTIONS,
};

/** Stretch digit range (0-9) across 0-30 range for Ras level setting. */
static inline unsigned int sau_ras_level(unsigned int digit) {
	return digit <= 6 ? digit : (digit - 4)*(digit - 4) + 2;
}

/** Random segments option flags. */
enum {
	SAU_RAS_O_HALFSHAPE = 1U<<0,
	SAU_RAS_O_SQUARE = 1U<<1,
	SAU_RAS_O_VIOLET = 1U<<2,
	SAU_RAS_O_ZIGZAG = 1U<<3,
	/* = 1U<<4, */
	SAU_RAS_O_FUNC_FLAGS = (1U<<5)-1,
	SAU_RAS_O_LINE_SET = 1U<<5,
	SAU_RAS_O_FUNC_SET = 1U<<6,
	SAU_RAS_O_LEVEL_SET = 1U<<7,
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

/* Macro used for operator use type sets of items. */
#define SAU_POP__ITEMS(X) \
	X(carr,  0, " CA", NULL) \
	X(camod, 1, "cAM", "c") /* channel mix i.e. panning modulation */ \
	X(amod,  1, " AM", "a") \
	X(ramod, 1, "rAM", "a.r") \
	X(fmod,  1, " FM", "f") \
	X(rfmod, 1, "rFM", "f.r") \
	X(pmod,  1, " PM", "p") \
	X(apmod, 1, "aPM", "p.a") \
	X(fpmod, 1, "fPM", "p.f") \
	//
#define SAU_POP__X_ID(NAME, ...) SAU_POP_N_##NAME,
#define SAU_POP__X_GRAPH(NAME, IS_MOD, LABEL, ...) LABEL,

/**
 * Operator use types.
 */
enum {
	SAU_POP__ITEMS(SAU_POP__X_ID)
	SAU_POP_NAMED,
	SAU_POP_N_default = 0, // shares value with carrier
};

typedef struct sauProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} sauProgramOpRef;

typedef struct sauProgramOpData {
	uint32_t id;
	uint32_t params;
	sauTime time;
	sauLine *pan;
	sauLine *amp, *amp2;
	sauLine *freq, *freq2;
	sauLine *pm_a;
	uint32_t phase;
	uint32_t seed; // TODO: divide containing node type
	uint8_t use_type; // carrier or modulator use?
	uint8_t type; // type info, for now
	union sauPOPMode {
		uint8_t main; // holds wave, noise, etc. ID -- what's primary
		sauRasOpt ras;
	} mode;
#define SAU_POP__X_IDARR_PTR(NAME, IS_MOD, ...) \
	SAU_IF(IS_MOD, const sauProgramIDArr *NAME##s;, )
	SAU_POP__ITEMS(SAU_POP__X_IDARR_PTR)
} sauProgramOpData;

typedef struct sauProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t carr_op_id;
	uint32_t op_count;
	uint32_t op_data_count;
	const sauProgramOpRef *op_list; // used for printout
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
	float ampmult;
	const char *name;
	struct sauMempool *mp; // holds memory for the specific program
	struct sauScript *parse; // parser output used to build program
} sauProgram;

struct sauScript;
struct sauScriptArg;
sauProgram* sau_build_Program(const struct sauScriptArg *restrict arg) sauMalloclike;
void sau_discard_Program(sauProgram *restrict o);

void sauProgram_print_info(const sauProgram *restrict o);
