/* mgensys: Audio generator data allocator.
 * Copyright (c) 2020 Joel K. Pettersson
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
#include "../mgensys.h"
#include "ngen.h"
#include "osc.h"
#include "env.h"
#include "../program.h"
#include "../mempool.h"
#include "../arrtype.h"
#include "../ptrarr.h"

typedef struct MGS_ModList {
	uint32_t count;
	uint32_t ids[];
} MGS_ModList;

typedef struct MGS_SoundNode {
	uint32_t time;
	float amp, dynamp;
	float pan;
	uint32_t amods_id;
	uint32_t params; // for use as update
	uint32_t voice_id;
	uint8_t type;
} MGS_SoundNode;

typedef struct MGS_NoiseNode {
	MGS_SoundNode sound;
	MGS_NGen ngen;
} MGS_NoiseNode;

typedef struct MGS_WaveNode {
	MGS_SoundNode sound;
	MGS_Osc osc;
	uint8_t attr;
	float freq, dynfreq;
	uint32_t fmods_id;
	uint32_t pmods_id;
} MGS_WaveNode;

typedef struct MGS_VoiceNode {
	MGS_SoundNode *root;
	uint32_t delay;
} MGS_VoiceNode;

mgsArrType(MGS_VoiceArr, MGS_VoiceNode, );

enum {
	MGS_EV_PREPARED = 1<<0,
	MGS_EV_UPDATE = 1<<1,
	MGS_EV_ACTIVE = 1<<2
};

typedef struct MGS_EventNode {
	MGS_SoundNode *sndn; // update node
	int32_t pos; // negative for delay, i.e. wait time
	uint8_t status;
	uint8_t base_type;
	uint32_t ref_i;
} MGS_EventNode;

mgsArrType(MGS_EventArr, MGS_EventNode, );

typedef struct MGS_RunAlloc {
	MGS_EventArr ev_arr;
	MGS_VoiceArr voice_arr;
	MGS_PtrArr mod_lists;
	MGS_SoundNode **sound_list;
	size_t sndn_count;
	size_t max_bufs;
	const MGS_Program *prg;
	uint32_t srate;
	MGS_MemPool *mem;
	MGS_EventNode *cur_ev;
	uint32_t cur_ev_id;
	uint32_t next_ev_delay;
	uint32_t flags;
} MGS_RunAlloc;

bool MGS_init_RunAlloc(MGS_RunAlloc *restrict o,
		const MGS_Program *restrict prg, uint32_t srate,
		MGS_MemPool *restrict mem);
void MGS_fini_RunAlloc(MGS_RunAlloc *restrict o);

bool MGS_RunAlloc_for_nodelist(MGS_RunAlloc *restrict o,
		MGS_ProgramNode *restrict first_n);
