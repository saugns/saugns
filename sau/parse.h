/* SAU library: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2025 Joel K. Pettersson
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
 * Envelope modes a.k.a. functions.
 */
enum {
	SAU_ENV_FN_OFF = 0,
	SAU_ENV_FN_CLAMP,
	SAU_ENV_FN_LOOP,
	SAU_ENV_FN_TRUNC,
	SAU_ENV_FUNCTIONS
};

/**
 * Envelope time parameters. Used as indices for time and line arrays.
 */
enum {
	SAU_ENV_TIME_A = 0,
	SAU_ENV_TIME_D,
	SAU_ENV_TIME_R,
	SAU_ENV_TIMES /* stages except the sustain stage */
};

/** Envelope time parameter flag; see envelope time enums for \p i range. */
#define SAU_ENVP_TIME(i) (1U<<(i))

/** Envelope other parameter flags. */
enum {
	SAU_ENVP_S         = 1U<<0,
	SAU_ENVP_MODE      = 1U<<1,
	SAU_ENVP_R_STRETCH = 1U<<2,
};

/**
 * Envelope parameter type.
 */
typedef struct sauEnvPar {
	uint32_t time_ms[SAU_ENV_TIMES];
	uint8_t line_p1[SAU_ENV_TIMES], line_all_p1; // set +1 the value
	float s_val;
	uint8_t flags, time_flags;
	uint8_t mode;
} sauEnvPar;

/**
 * Range parameter type.
 *
 * Holds lines for sweep and value range modulation pair, and envelope pairing.
 * Also holds envelope parameter data, for use with a separate triggered timer.
 */
typedef struct sauRange {
	sauLinePar a, b, e;
	sauEnvPar env;
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

/**
 * Phase distortion parameter set type.
 *
 * Holds main range, phase offset range, frequency multiplier, and flags.
 */
typedef struct sauPDSet {
	sauRange v, f, p;
} sauPDSet;

/**
 * Phase distortion parameter set IDs.
 */
enum {
	SAU_PPD_C = 0,
	SAU_PPD_D,
	SAU_PPD_H,
	SAU_PPD_X,
	SAU_PPD_Y,
	SAU_PPD_TYPES,
};

/** Is PD cycle zoom a.k.a. pulsar synthesis, '.f' multiplying frequency? */
#define sau_pd_f_is_fmul(id) ((id) <= SAU_PPD_D)

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

/** True if the given program generator type is the given type. */
#define sau_pgen_is(type_id, N_ID) ((type_id) == SAU_PGEN_N_##N_ID)

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
 * Object ID constants.
 */
#define SAU_POBJ_NO_ID  UINT32_MAX       /* object ID missing */
#define SAU_POBJ_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

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
	X(  carr,   0, " CA ", NULL) \
SAU_MOD__VR(c_am,   X, "cAM",  "c") /* channel mix i.e. panning modulation */ \
SAU_MOD__VR(a_am,   X, " AM",  "a") \
SAU_MOD__VR(f_fm,   X, " FM",  "f") \
	X(  p_pm,   1, " PM ", "p") \
	X(  pf_pm,  1, "fPM ", "p.f") \
SAU_MOD__VR(pa_pm,  X, "aPM",  "p.a") \
SAU_MOD__PD(pd_c,   X, "cPD",  "p.c") \
SAU_MOD__PD(pd_d,   X, "dPD",  "p.d") \
SAU_MOD__PD(pd_h,   X, "hPD",  "p.h") \
SAU_MOD__PD(pd_x,   X, "xPD",  "p.x") \
SAU_MOD__PD(pd_y,   X, "yPD",  "p.y") \
	//
#define SAU_MOD__VR(NAME, X, LABEL, SYNTAX) /* 5 valrange modulator types */ \
	X(NAME,     1, LABEL " ", SYNTAX) \
	X(NAME##2,  1, LABEL "2", SYNTAX "..") \
	X(NAME##_r, 1, LABEL "r", SYNTAX ".r") \
	X(NAME##_e, 1, LABEL "e", SYNTAX ".e") \
	X(NAME##_a, 1, LABEL "a", SYNTAX ".a") \
	//
#define SAU_MOD__PD(NAME, X, LABEL, SYNTAX) /* 5*3 PD valrange modulators */ \
SAU_MOD__VR(NAME,   X, LABEL,     SYNTAX) \
SAU_MOD__VR(NAME##f,X, LABEL "f", SYNTAX ".f") \
SAU_MOD__VR(NAME##p,X, LABEL "p", SYNTAX ".p") \
	//
#define SAU_MOD__X_ID(NAME, ...) SAU_MOD_N_##NAME,
#define SAU_MOD__X_GRAPH(NAME, IS_MOD, LABEL, ...) LABEL,
#define SAU_MOD__X_SYNTAX(NAME, IS_MOD, LABEL, SYNTAX) SYNTAX,

/* Number of modulators in sequence for a value range with envelope and all. */
#define SAU_MODS_VALR 5
#define SAU_MOD_VALR   0
#define SAU_MOD_VALR2  1
#define SAU_MOD_VALR_r 2
#define SAU_MOD_VALR_e 3
#define SAU_MOD_VALR_a 4

/**
 * Generator modulation or use types.
 */
enum {
	SAU_MOD__ITEMS(SAU_MOD__X_ID)
	SAU_MOD_NAMED,
	SAU_MOD_N_default = 0, // shares value with carrier
};

typedef struct sauPrintGenRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} sauPrintGenRef;

/**
 * Reference to script data object; the reference is common to all subtypes.
 * The \a obj_id is a type-specific ID.
 */
typedef struct sauParseObjRef {
	uint32_t obj_id; // shared by all references to an object
	uint8_t obj_type; // included for quick access
	uint8_t gen_type; // included for quick access
	bool is_new       : 1; // first data for object
	bool is_cloned    : 1;
	bool is_nested    : 1;
	bool is_labeled   : 1; // this exact node is pointed to by label
	bool has_next_ref : 1; // status usable within scope of a durgroup
	void *next; // next in set of objects
	struct sauParseObjRef *prev_ref; // a preceding ref for same object
} sauParseObjRef;

/**
 * Container node for linked list, used for nesting.
 */
typedef struct sauParseListData {
	sauParseObjRef ref;
	void *first_item;
	uint8_t use_type;
	bool append;
} sauParseListData;

/**
 * Node type for generator data.
 */
typedef struct sauParseGenData {
	sauParseObjRef ref;
	struct sauParseEvData *event;
	/* generator parameters */
	uint32_t copy_from_id; // for initializing a cloned generator
	uint32_t params;
	sauTime time;
	sauRange *amp, *pan;
	sauRange *freq;
	sauRange *pm_a;
	sauPDSet *pd;
	uint32_t phase;
	uint32_t seed;
	union sauPGenMode {
		uint8_t main; // holds wave, noise, etc. ID -- what's primary
		sauRasOpt ras;
		sauWaveOpt woo;
	} mode;
	sauParseListData *mods; // node adjacents updates
	/* ID arrays as used by audio generator code */
	const sauProgramIDs *mods_idarr;
	uint32_t mods_count; // number of ID arrays
} sauParseGenData;

/**
 * Script data event flags.
 */
enum {
	SAU_PEV_VOICE_SET_DUR    = 1U<<0,
	SAU_PEV_IMPLICIT_TIME    = 1U<<1,
	SAU_PEV_WAIT_PREV_DUR    = 1U<<2, // compound step timing
	SAU_PEV_FROM_GAPSHIFT    = 1U<<3, // gapshift follow-on event
	SAU_PEV_LOCK_DUR_SCOPE   = 1U<<4, // nested data can't lengthen dur
};

struct sauParseEvBranch;

/**
 * Node type for event data. Events are placed in time per script contents,
 * in a nested way during parsing and flattened after for later processing.
 *
 * The flow of time and nesting in a script end up two different dimensions
 * of data. Attached objects introduce (sub)trees of script contents, after
 * which they may also refer back to just parts of them in follow-on nodes.
 * (E.g. a tree of carriers and modulators in one event, and then an update
 * node for a modulator in the next event. An update could add a sub-tree.)
 */
typedef struct sauParseEvData {
	struct sauParseEvData *next;
	struct sauParseEvBranch *forks;
	void *main_obj;
	uint32_t wait_ms;
	uint32_t dur_ms; // for level at which main object is included
	uint8_t ev_flags;
	uint16_t vo_id;
	uint32_t carr_obj_id;
	const sauParseGenData **gen_data; // flat per-event list
	uint32_t gen_data_count;
	/* for -p printout format (voice graph blocks) */
	uint32_t gen_count;
	const sauPrintGenRef *gen_list;
} sauParseEvData;

/** String and number pair for predefined values passed as arguments. */
typedef struct sauScriptPredef {
	const char *key;
	uint32_t len;
	double val;
} sauScriptPredef;

/** Specifies a script to parse (and possibly process further). */
typedef struct sauScriptArg {
	const char *str;
	bool is_path : 1;
	bool no_time : 1;
	sauScriptPredef *predef;
	size_t predef_count;
} sauScriptArg;

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct sauParseSetOptions {
	float ampmult; // global amplitude multiplier for whole script
	float A4_freq; // A4 tuning for frequency as note
	/* generator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_ampmult,
	      def_freq,
	      def_relfreq,
	      def_chanmix;
	int8_t note_key;
	uint8_t key_octave;
	uint8_t key_system;
	sauRasOpt def_ras;
	sauWaveOpt def_woo;
} sauParseSetOptions;

/**
 * Type returned after processing a file. The data is divided into
 * two mempools, one specific to the parse and one shared with any
 * later program data (sauProgram), if built from the same parse.
 */
typedef struct sauParse {
	sauParseEvData *events;
	sauParseSetOptions sopt;
	const char *name; // currently simply set to the filename
	struct sauSymtab *st;
	size_t ev_count;
	bool is_ampmult_set : 1;
	bool is_amp_autoscaled : 1;
	uint8_t gen_nest_depth;
	uint16_t vo_count;
	uint32_t gen_count;
	uint32_t duration_ms;
	struct sauMempool *mp; // holds memory for the specific program
} sauParse;

sauParse* sau_build_Parse(const sauScriptArg *restrict arg) sauMalloclike;
void sau_discard_Parse(sauParse *restrict o);

void sauParse_print_info(const sauParse *restrict o);
