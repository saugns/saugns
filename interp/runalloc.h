/* mgensys: Audio generator data allocator.
 * Copyright (c) 2020-2022 Joel K. Pettersson
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
#include "../mgensys.h"
#include "ngen.h"
#include "osc.h"
#include "env.h"
#include "../program.h"
#include "../mempool.h"
#include "../arrtype.h"
#include "../ptrarr.h"

typedef struct mgsModList {
	uint32_t count;
	uint32_t ids[];
} mgsModList;

typedef struct mgsSoundNode {
	uint32_t time;
	float amp, dynamp;
	float pan;
	uint32_t amods_id;
	uint32_t params; // for use as update
	uint32_t voice_id;
	uint8_t type;
} mgsSoundNode;

typedef struct mgsLineNode {
	mgsSoundNode sound;
	mgsLine line;
} mgsLineNode;

typedef struct mgsNoiseNode {
	mgsSoundNode sound;
	mgsNGen ngen;
} mgsNoiseNode;

typedef struct mgsWaveNode {
	mgsSoundNode sound;
	mgsOsc osc;
	uint8_t attr;
	float freq, dynfreq;
	uint32_t fmods_id;
	uint32_t pmods_id;
} mgsWaveNode;

typedef struct mgsVoiceNode {
	mgsSoundNode *root;
	uint32_t delay;
} mgsVoiceNode;

mgsArrType(mgsVoiceArr, mgsVoiceNode, )

enum {
	MGS_EV_PREPARED = 1<<0,
	MGS_EV_UPDATE = 1<<1,
	MGS_EV_ACTIVE = 1<<2
};

typedef struct mgsEventNode {
	mgsSoundNode *sndn; // update node
	int32_t pos; // negative for delay, i.e. wait time
	uint8_t status;
	uint8_t base_type;
	uint32_t ref_i;
} mgsEventNode;

mgsArrType(mgsEventArr, mgsEventNode, )

typedef struct mgsRunAlloc {
	mgsEventArr ev_arr;
	mgsVoiceArr voice_arr;
	mgsPtrArr mod_lists;
	mgsSoundNode **sound_list;
	size_t sndn_count;
	size_t max_bufs;
	const mgsProgram *prg;
	uint32_t srate;
	mgsMemPool *mem;
	mgsEventNode *cur_ev;
	uint32_t cur_ev_id;
	uint32_t next_ev_delay;
	uint32_t flags;
	uint32_t seed;
} mgsRunAlloc;

bool mgs_init_RunAlloc(mgsRunAlloc *restrict o,
		const mgsProgram *restrict prg, uint32_t srate,
		mgsMemPool *restrict mem);
void mgs_fini_RunAlloc(mgsRunAlloc *restrict o);

bool mgsRunAlloc_for_nodelist(mgsRunAlloc *restrict o,
		mgsProgramData *restrict first_n);
