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
 * Range parameter type.
 *
 * Holds pair of lines, allowing sweeps alongside modulation with value ranges.
 */
typedef struct sauRange {
	sauLine a, b;
} sauRange;

/**
 * Swept parameter IDs.
 */
enum {
	SAU_PSWEEP_PAN = 0,
	SAU_PSWEEP_AMP,
	SAU_PSWEEP_FREQ,
	SAU_PSWEEP_PMA,
};

/** Frequency parameter default value, when default not changed in a script. */
#define SAU_PDEF_FREQ 440.0

enum {
	SAU_POBJT_LIST = 0,
	SAU_POBJT_GEN,
	SAU_POBJT_TYPES,
};

/* Macro used to declare and define program generator types sets of items. */
#define SAU_PGEN__ITEMS(X) \
	X(amp,   'A') \
	X(noise, 'N') \
	X(wave,  'W') \
	X(raseg, 'R') \
	//
#define SAU_PGEN__X_ID(NAME, LABELC) SAU_PGEN_N_##NAME,

enum {
	SAU_PGEN__ITEMS(SAU_PGEN__X_ID)
	SAU_PGEN_TYPES,
};

/** True if the given program generator type is an oscillator type. */
#define sau_pgen_is_osc(type_id) ((type_id) >= SAU_PGEN_N_wave)

/** True if the given program generator type uses seed values. */
static inline bool sau_pgen_has_seed(unsigned type_id) {
	return type_id == SAU_PGEN_N_noise || type_id == SAU_PGEN_N_raseg;
}

/**
 * Generator parameter flags. For parameters without other tracking only.
 */
enum {
	SAU_PGENP_TIME = 1<<0,
	SAU_PGENP_MODE = 1<<1, // type-specific data
	SAU_PGENP_PHASE = 1<<2,
	SAU_PGENP_SEED = 1<<3,
	SAU_PGEN_PARAMS = (1<<4) - 1,
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
	uint8_t line; // line module type; is first, to match sauPGenMode main
	unsigned flags: 10;
	unsigned func:  6;
	unsigned level: 8;
	uint32_t alpha;
} sauRasOpt;

/** Random segments functions. */
enum {
	SAU_RAS_F_URAND = 0,
	SAU_RAS_F_GAUSS,
	SAU_RAS_F_BIN,
	SAU_RAS_F_TERN,
	SAU_RAS_F_FIXED,
	SAU_RAS_F_ADDREC,
	SAU_RAS_FUNCTIONS,
};

/** Stretch digit range (0-9) across 0-30 range for Ras level setting. */
static inline unsigned int sau_ras_level(unsigned int digit) {
	return digit <= 6 ? digit : (digit - 4)*(digit - 4) + 2;
}

/** Random segments option flags. */
enum {
	SAU_RAS_O_PERLIN        = 1U<<0,
	SAU_RAS_O_HALFSHAPE     = 1U<<1,
	SAU_RAS_O_ZIGZAG        = 1U<<2,
	SAU_RAS_O_SQUARE        = 1U<<3,
	SAU_RAS_O_VIOLET        = 1U<<4,
	SAU_RAS_O_UNUSED        = 1U<<5,
	SAU_RAS_O_FUNC_FLAGS    = (1U<<6)-1,
	SAU_RAS_O_LINE_SET      = 1U<<6,
	SAU_RAS_O_FUNC_SET      = 1U<<7,
	SAU_RAS_O_LEVEL_SET     = 1U<<8,
	SAU_RAS_O_ASUBVAL_SET   = 1U<<9,
};

/*
 * Voice ID constants.
 */
#define SAU_PVO_NO_ID  UINT16_MAX       /* voice ID missing */
#define SAU_PVO_MAX_ID (UINT16_MAX - 1) /* error if exceeded */

/*
 * Generator ID constants.
 */
#define SAU_PGEN_NO_ID  UINT32_MAX       /* generator ID missing */
#define SAU_PGEN_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

typedef struct sauProgramIDArr {
	uint32_t count;
	uint32_t ids[];
} sauProgramIDArr;

typedef struct sauProgramIDs {
	const sauProgramIDArr *a;
	uint8_t use;
} sauProgramIDs;

/* Macro used for generator modulation or use type sets of items. */
#define SAU_MOD__ITEMS(X) \
	X(  carr,  0, " CA ", NULL) \
SAU_MOD__4M(c_am,  X, "cAM",  "c") /* channel mix i.e. panning modulation */ \
SAU_MOD__4M(a_am,  X, " AM",  "a") \
SAU_MOD__4M(f_fm,  X, " FM",  "f") \
	X(  p_pm,  1, " PM ", "p") \
	X(  pf_pm, 1, "fPM ", "p.f") \
SAU_MOD__4M(pa_pm, X, "aPM",  "p.a") \
	//
#define SAU_MOD__4M(NAME, X, LABEL, SYNTAX) /* 4 valrange modulator types */ \
	X(NAME,     1, LABEL " ", SYNTAX) \
	X(NAME##1,  1, LABEL "1", SYNTAX "..") \
	X(NAME##2,  1, LABEL "2", SYNTAX "..") \
	X(NAME##_r, 1, LABEL "r", SYNTAX ".r") \
	//
#define SAU_MOD__X_ID(NAME, ...) SAU_MOD_N_##NAME,
#define SAU_MOD__X_GRAPH(NAME, IS_MOD, LABEL, ...) LABEL,
#define SAU_MOD__X_SYNTAX(NAME, IS_MOD, LABEL, SYNTAX) SYNTAX,

/**
 * Generator modulation or use types.
 */
enum {
	SAU_MOD__ITEMS(SAU_MOD__X_ID)
	SAU_MOD_NAMED,
	SAU_MOD_N_default = 0, // shares value with carrier
};

typedef struct sauProgramGenRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} sauProgramGenRef;

typedef struct sauProgramGenData {
	uint32_t id;
	uint32_t params;
	sauTime time;
	sauRange *amp, *pan;
	sauRange *freq;
	sauRange *pm_a;
	uint32_t phase;
	uint32_t seed;
	union sauPGenMode {
		uint8_t main; // holds wave, noise, etc. ID -- what's primary
		sauRasOpt ras;
	} mode;
	uint8_t use_type; // carrier or modulator use?
	uint8_t type; // type info, for now
	uint32_t mod_count;
	const sauProgramIDs *mods;
} sauProgramGenData;

typedef struct sauProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t carr_gen_id;
	uint32_t gen_count;
	uint32_t gen_data_count;
	const sauProgramGenRef *gen_list; // used for printout
	const sauProgramGenData *gen_data;
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
	uint32_t gen_count;
	uint8_t gen_nest_depth;
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
