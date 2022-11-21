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

typedef struct mgsProgramNode mgsProgramNode;
typedef struct mgsProgramSoundData mgsProgramSoundData;
typedef struct mgsProgramLineData mgsProgramLineData;
typedef struct mgsProgramNoiseData mgsProgramNoiseData;
typedef struct mgsProgramWaveData mgsProgramWaveData;
typedef struct mgsProgramScopeData mgsProgramScopeData;
typedef struct mgsProgramDurData mgsProgramDurData;
typedef struct mgsProgramArrData mgsProgramArrData;

/* Time parameter flags. */
enum {
	MGS_TIME_SET = 1<<0,
};

typedef struct mgsTimePar {
	float v;
	uint32_t flags;
} mgsTimePar;

struct mgsProgramSoundData {
	mgsTimePar time;
	mgsProgramNode *root;
	uint32_t params;
	float amp, dynamp, pan;
	mgsProgramArrData *amod;
	mgsProgramNode *nested_next;
};

struct mgsProgramLineData {
	mgsProgramSoundData sound;
	mgsLine line;
};

struct mgsProgramNoiseData {
	mgsProgramSoundData sound;
	uint8_t noise;
};

struct mgsProgramWaveData {
	mgsProgramSoundData sound;
	uint8_t attr, wave;
	float freq, dynfreq;
	uint32_t phase;
	mgsProgramArrData *pmod, *fmod;
};

struct mgsProgramScopeData {
	mgsProgramNode *first_node;
	mgsProgramNode *last_node;
};

struct mgsProgramDurData {
	mgsProgramScopeData scope;
	mgsProgramNode *next;
};

struct mgsProgramArrData {
	mgsProgramScopeData scope;
	uint32_t count;
	uint8_t mod_type;
};

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

static inline bool mgsProgramNode_is_type(const mgsProgramNode *restrict n,
		uint8_t type) {
	if (n->type == type || n->base_type == type)
		return true;
	/*switch (type) {
	}*/
	return false;
}

static inline void *mgsProgramNode_get_data(const mgsProgramNode *restrict n,
		uint8_t type) {
	return mgsProgramNode_is_type(n, type) ? n->data : NULL;
}

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
