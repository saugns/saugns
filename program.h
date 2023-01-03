/* mgensys: Audio program data.
 * Copyright (c) 2011, 2020-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#include "Object.h"
#include "line.h"
#include "noise.h"
#include "wave.h"

/* Node type tags. Used for representations elsewhere too. */
enum {
	MGS_BASETYPE_NONE = 0,
	MGS_BASETYPE_SOUND,
	MGS_BASETYPE_ENV,
	MGS_BASETYPE_SCOPE,
	MGS_BASETYPES,
	MGS_TYPE_NONE = 0,
	MGS_TYPE_LINE = MGS_BASETYPES,
	MGS_TYPE_NOISE,
	MGS_TYPE_WAVE,
	MGS_TYPE_RASEG,
	MGS_TYPE_ENV,
	MGS_TYPE_DUR,
	MGS_TYPE_ARR,
	MGS_TYPES,
};

/* Sound node modulators, all types. */
enum {
	MGS_MOD_NONE = 0,
	MGS_MOD_AM,
	MGS_MOD_FM,
	MGS_MOD_PM,
	MGS_MOD_TYPES
};

/* Wave attributes. */
enum {
	MGS_ATTR_FREQRATIO = 1<<0,
	MGS_ATTR_DYNFREQRATIO = 1<<1
};

/* Sound node parameters. */
enum {
	MGS_PARAM_MASK = (1<<24) - 1,
	/* (Level 1) Common object parameters. */
	MGS_SOUNDP_TIME = 1<<0,
	MGS_SOUNDP_AMP = 1<<1,
	MGS_SOUNDP_DYNAMP = 1<<2,
	MGS_SOUNDP_PAN = 1<<3,
	/* (Level 2) Line object parameters. */
	MGS_LINEP_LINE = 1<<8,
	//MGS_LINEP_VALUE = 1<<9,
	//MGS_LINEP_GOAL = 1<<10,
	/* (Level 2) Noise object parameters. */
	MGS_NOISEP_NOISE = 1<<8,
	/* (Level 2) Oscillating generator object parameters. */
	MGS_OSCGENP_ATTR = 1<<8,
	MGS_OSCGENP_FREQ = 1<<9,
	MGS_OSCGENP_DYNFREQ = 1<<10,
	MGS_OSCGENP_PHASE = 1<<11,
	/* (Level 3) Wave object parameters. */
	MGS_WAVEP_WAVE = 1<<16,
	/* (Level 3) Repeated line segments object parameters. */
	MGS_RASEGP_SEG = 1<<16,
	MGS_RASEGP_MODE = 1<<17,
};

struct mgsParser;

/* Time parameter flags. */
enum {
	MGS_TIME_SET = 1<<0,
};

typedef struct mgsTimePar {
	float v;
	uint32_t flags;
} mgsTimePar;

#define mgsProgramData_C_ mgsObject_C_ \
	void *next, *ref_prev; \
	float delay; \
	uint8_t base_type, type; /* type tags used elsewhere too */ \
	uint32_t base_id; /* per-base-type id, not increased for references */ \
	uint32_t conv_id; /* for use by later processing */ \
/**/
#define mgsProgramData_V_ mgsObject_V_ \
	bool (*end_prev_node)(struct mgsParser *pr); \
	/* void (*time_node)(mgsProgramData *n); */ \
/**/
MGSclassdef(mgsProgramData)

#define mgsProgramSoundData_C_ mgsProgramData_C_ \
	mgsTimePar time; \
	struct mgsProgramSoundData *root; \
	uint32_t params; \
	float amp, dynamp, pan; \
	struct mgsProgramArrData *amod; \
	void *nested_next; \
/**/
#define mgsProgramSoundData_V_ mgsProgramData_V_ \
/**/
MGSclassdef(mgsProgramSoundData)

#define mgsProgramLineData_C_ mgsProgramSoundData_C_ \
	mgsLine line; \
/**/
#define mgsProgramLineData_V_ mgsProgramSoundData_V_ \
/**/
MGSclassdef(mgsProgramLineData)

#define mgsProgramNoiseData_C_ mgsProgramSoundData_C_ \
	uint8_t noise; \
/**/
#define mgsProgramNoiseData_V_ mgsProgramSoundData_V_ \
/**/
MGSclassdef(mgsProgramNoiseData)

#define mgsProgramOscgenData_C_ mgsProgramSoundData_C_ \
	float freq, dynfreq; \
	uint32_t phase; \
	struct mgsProgramArrData *pmod, *fmod; \
	uint8_t attr; \
/**/
#define mgsProgramOscgenData_V_ mgsProgramSoundData_V_ \
/**/
MGSclassdef(mgsProgramOscgenData)

#define mgsProgramWaveData_C_ mgsProgramOscgenData_C_ \
	uint8_t wave; \
/**/
#define mgsProgramWaveData_V_ mgsProgramOscgenData_V_ \
/**/
MGSclassdef(mgsProgramWaveData)

enum {
	MGS_RASEG_MODE_RAND = 0,
	MGS_RASEG_MODE_SMOOTH,
	MGS_RASEG_MODE_FIXED,
};

#define mgsProgramRasegData_C_ mgsProgramOscgenData_C_ \
	uint8_t seg, mode; \
/**/
#define mgsProgramRasegData_V_ mgsProgramOscgenData_V_ \
/**/
MGSclassdef(mgsProgramRasegData)

#define mgsProgramScopeData_C_ mgsProgramData_C_ \
	void *first_node, *last_node; \
/**/
#define mgsProgramScopeData_V_ mgsProgramData_V_ \
/**/
MGSclassdef(mgsProgramScopeData)

#define mgsProgramDurData_C_ mgsProgramScopeData_C_ \
	struct mgsProgramDurData *next_dur; \
/**/
#define mgsProgramDurData_V_ mgsProgramScopeData_V_ \
/**/
MGSclassdef(mgsProgramDurData)

#define mgsProgramArrData_C_ mgsProgramScopeData_C_ \
	uint32_t count; \
	uint8_t mod_type; \
/**/
#define mgsProgramArrData_V_ mgsProgramScopeData_V_ \
/**/
MGSclassdef(mgsProgramArrData)

struct mgsMemPool;
struct mgsSymTab;

typedef struct mgsLangOpt {
} mgsLangOpt;

bool mgs_init_LangOpt(mgsLangOpt *restrict o,
		struct mgsSymTab *restrict symtab);

struct mgsProgram {
	mgsProgramData *node_list;
	uint32_t node_count;
	uint32_t root_count;
	uint32_t base_counts[MGS_BASETYPES];
	struct mgsMemPool *mem;
	struct mgsSymTab *symt;
	const char *name;
	mgsLangOpt lopt;
};
