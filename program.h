/* mgensys: Audio program data.
 * Copyright (c) 2011, 2020-2022 Joel K. Pettersson
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

/* Node types. */
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
	MGS_PARAM_MASK = (1<<16) - 1,
	/* Common object parameters. */
	MGS_SOUNDP_TIME = 1<<0,
	MGS_SOUNDP_AMP = 1<<1,
	MGS_SOUNDP_DYNAMP = 1<<2,
	MGS_SOUNDP_PAN = 1<<3,
	/* Line object parameters. */
	MGS_LINEP_LINE = 1<<8,
	//MGS_LINEP_VALUE = 1<<9,
	//MGS_LINEP_GOAL = 1<<10,
	/* Noise object parameters. */
	MGS_NOISEP_NOISE = 1<<8,
	/* Wave object parameters. */
	MGS_WAVEP_WAVE = 1<<8,
	MGS_WAVEP_ATTR = 1<<9,
	MGS_WAVEP_FREQ = 1<<10,
	MGS_WAVEP_DYNFREQ = 1<<11,
	MGS_WAVEP_PHASE = 1<<12,
};

struct mgsParser;
struct mgsProgramArrData;
typedef struct mgsProgramNode mgsProgramNode;

/* Time parameter flags. */
enum {
	MGS_TIME_SET = 1<<0,
};

typedef struct mgsTimePar {
	float v;
	uint32_t flags;
} mgsTimePar;

#define mgsProgramData_C_ mgsObject_C_ \
/**/
#define mgsProgramData_V_ mgsObject_V_ \
	bool (*end_prev_node)(struct mgsParser *pr); \
	/* void (*time_node)(mgsProgramNode *n); */ \
/**/
MGSclassdef(mgsProgramData)

#define mgsProgramSoundData_C_ mgsProgramData_C_ \
	mgsTimePar time; \
	mgsProgramNode *root; \
	uint32_t params; \
	float amp, dynamp, pan; \
	struct mgsProgramArrData *amod; \
	mgsProgramNode *nested_next; \
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

#define mgsProgramWaveData_C_ mgsProgramSoundData_C_ \
	uint8_t attr, wave; \
	float freq, dynfreq; \
	uint32_t phase; \
	struct mgsProgramArrData *pmod, *fmod; \
/**/
#define mgsProgramWaveData_V_ mgsProgramSoundData_V_ \
/**/
MGSclassdef(mgsProgramWaveData)

#define mgsProgramScopeData_C_ mgsProgramData_C_ \
	mgsProgramNode *first_node; \
	mgsProgramNode *last_node; \
/**/
#define mgsProgramScopeData_V_ mgsProgramData_V_ \
/**/
MGSclassdef(mgsProgramScopeData)

#define mgsProgramDurData_C_ mgsProgramScopeData_C_ \
	mgsProgramNode *next_dur; \
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

struct mgsProgramNode {
	mgsProgramNode *next;
	mgsProgramNode *ref_prev;
	float delay;
	uint8_t base_type;
	uint8_t type;
	uint32_t base_id; // per-base-type id, not increased for references
	uint32_t conv_id; // for use by later processing
	void *data;
};

#define mgsProgramNode_get_data(n, Class) \
	(mgs_of_class((n)->data, Class) ? (n)->data : NULL)

struct mgsMemPool;
struct mgsSymTab;

typedef struct mgsLangOpt {
} mgsLangOpt;

bool mgs_init_LangOpt(mgsLangOpt *restrict o,
		struct mgsSymTab *restrict symtab);

struct mgsProgram {
	mgsProgramNode *node_list;
	uint32_t node_count;
	uint32_t root_count;
	uint32_t base_counts[MGS_BASETYPES];
	struct mgsMemPool *mem;
	struct mgsSymTab *symt;
	const char *name;
	mgsLangOpt lopt;
};
