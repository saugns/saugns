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
#include "osc.h"
#include "env.h"
#include "../program.h"
#include "../mempool.h"
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
	uint32_t root_base_i;
	uint8_t type;
} MGS_SoundNode;

typedef struct MGS_OpNode {
	MGS_SoundNode sound;
	MGS_Osc osc;
	uint8_t attr;
	float freq, dynfreq;
	uint32_t fmods_id;
	uint32_t pmods_id;
} MGS_OpNode;

typedef struct MGS_RunAlloc {
	MGS_PtrArr sound_list;
	MGS_PtrArr mod_lists;
	const MGS_Program *prg;
	uint32_t srate;
	MGS_MemPool *mem;
} MGS_RunAlloc;

bool MGS_init_RunAlloc(MGS_RunAlloc *restrict o,
		const MGS_Program *restrict prg, uint32_t srate,
		MGS_MemPool *restrict mem);
void MGS_fini_RunAlloc(MGS_RunAlloc *restrict o);

bool MGS_RunAlloc_get_modlist(MGS_RunAlloc *restrict o,
		const MGS_ProgramArrData *restrict arr_data,
		uint32_t *restrict id);

MGS_OpNode *MGS_RunAlloc_for_op(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n);

void *MGS_RunAlloc_for_node(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n);
