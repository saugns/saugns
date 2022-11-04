/* mgensys: Audio program data.
 * Copyright (c) 2011, 2020-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

typedef struct MGS_ProgramNode MGS_ProgramNode;
typedef struct MGS_ProgramSoundData MGS_ProgramSoundData;
typedef struct MGS_ProgramLineData MGS_ProgramLineData;
typedef struct MGS_ProgramNoiseData MGS_ProgramNoiseData;
typedef struct MGS_ProgramWaveData MGS_ProgramWaveData;
typedef struct MGS_ProgramScopeData MGS_ProgramScopeData;
typedef struct MGS_ProgramDurData MGS_ProgramDurData;
typedef struct MGS_ProgramArrData MGS_ProgramArrData;

/* Time parameter flags. */
enum {
	MGS_TIME_SET = 1<<0,
};

typedef struct MGS_TimePar {
	float v;
	uint32_t flags;
} MGS_TimePar;

struct MGS_ProgramSoundData {
	MGS_TimePar time;
	MGS_ProgramNode *root;
	uint32_t params;
	float amp, dynamp, pan;
	MGS_ProgramArrData *amod;
	MGS_ProgramNode *nested_next;
};

struct MGS_ProgramLineData {
	MGS_ProgramSoundData sound;
	MGS_Line line;
};

struct MGS_ProgramNoiseData {
	MGS_ProgramSoundData sound;
	uint8_t noise;
};

struct MGS_ProgramWaveData {
	MGS_ProgramSoundData sound;
	uint8_t attr, wave;
	float freq, dynfreq;
	uint32_t phase;
	MGS_ProgramArrData *pmod, *fmod;
};

struct MGS_ProgramScopeData {
	MGS_ProgramNode *first_node;
	MGS_ProgramNode *last_node;
};

struct MGS_ProgramDurData {
	MGS_ProgramScopeData scope;
	MGS_ProgramNode *next;
};

struct MGS_ProgramArrData {
	MGS_ProgramScopeData scope;
	uint32_t count;
	uint8_t mod_type;
};

struct MGS_ProgramNode {
	MGS_ProgramNode *next;
	MGS_ProgramNode *ref_prev;
	float delay;
	uint8_t base_type;
	uint8_t type;
	uint32_t base_id; // per-base-type id, not increased for references
	uint32_t conv_id; // for use by later processing
	void *data;
};

static inline bool MGS_ProgramNode_is_type(const MGS_ProgramNode *restrict n,
		uint8_t type) {
	if (n->type == type || n->base_type == type)
		return true;
	/*switch (type) {
	}*/
	return false;
}

static inline void *MGS_ProgramNode_get_data(const MGS_ProgramNode *restrict n,
		uint8_t type) {
	return MGS_ProgramNode_is_type(n, type) ? n->data : NULL;
}

struct MGS_MemPool;
struct MGS_SymTab;

typedef struct MGS_LangOpt {
} MGS_LangOpt;

bool MGS_init_LangOpt(MGS_LangOpt *restrict o,
		struct MGS_SymTab *restrict symtab);

struct MGS_Program {
	MGS_ProgramNode *node_list;
	uint32_t node_count;
	uint32_t root_count;
	uint32_t base_counts[MGS_BASETYPES];
	struct MGS_MemPool *mem;
	struct MGS_SymTab *symt;
	const char *name;
	MGS_LangOpt lopt;
};
