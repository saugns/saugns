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

#include "generator.h"

static MGS_ModList *create_ModList(const MGS_ProgramArrData *restrict arr_data,
		MGS_MemPool *restrict mem) {
	MGS_ModList *o = MGS_MemPool_alloc(mem, sizeof(MGS_ModList) +
			arr_data->count * sizeof(uint32_t));
	MGS_ProgramNode *n = arr_data->scope.first_node;
	size_t i = 0;
	o->count = arr_data->count;
	while (n != NULL) {
		MGS_ProgramSoundData *sound_data = n->data;
		o->ids[i++] = n->base_id;
		n = sound_data->nested_next;
	}
	return o;
}

bool MGS_init_RunAlloc(MGS_RunAlloc *restrict o,
		const MGS_Program *restrict prg, uint32_t srate,
		MGS_MemPool *restrict mem) {
	*o = (MGS_RunAlloc){};
	o->prg = prg;
	o->srate = srate;
	o->mem = mem;
	/*
	 * Add blank modlist as value 0.
	 */
	MGS_ModList *l = MGS_MemPool_alloc(mem, sizeof(MGS_ModList));
	if (!l || !MGS_PtrArr_add(&o->mod_lists, l))
		return false;
	return true;
}

void MGS_fini_RunAlloc(MGS_RunAlloc *restrict o) {
	MGS_PtrArr_clear(&o->sound_list);
	MGS_PtrArr_clear(&o->mod_lists);
}

/**
 * Get modulator list ID for \p arr_data, adding the modulator
 * list to the collection.
 *
 * The ID 0 is reserved for blank modulator lists (\p arr_data
 * NULL or with a zero count). Other lists are allocated and a
 * new ID returned for each.
 *
 * \return true, or false on allocation failure
 */
mgsNoinline bool MGS_RunAlloc_get_modlist(MGS_RunAlloc *restrict o,
		const MGS_ProgramArrData *restrict arr_data,
		uint32_t *restrict id) {
	if (!arr_data || !arr_data->count) {
		*id = 0;
		return true;
	}
	MGS_ModList *l = create_ModList(arr_data, o->mem);
	if (!l || !(MGS_PtrArr_add(&o->mod_lists, l)))
		return false;
	*id = o->mod_lists.count - 1;
	return true;
}

/* does not handle allocation of the sound node itself */
static bool MGS_RunAlloc_for_sound(MGS_RunAlloc *restrict o,
		MGS_SoundNode *restrict sndn,
		const MGS_ProgramNode *restrict n) {
	if (!MGS_PtrArr_add(&o->sound_list, sndn))
		return false;
	MGS_ProgramSoundData *sound_data = n->data;
	sndn->time = lrintf(sound_data->time.v * o->srate);
	sndn->amp = sound_data->amp;
	sndn->dynamp = sound_data->dynamp;
	sndn->pan = sound_data->pan;
	if (sound_data->amod != NULL) {
		if (!MGS_RunAlloc_get_modlist(o, sound_data->amod,
				&sndn->amods_id))
			return false;
	}
	sndn->root_base_i = sound_data->root->base_id;
	sndn->type = n->type;
	return true;
}

MGS_OpNode *MGS_RunAlloc_for_op(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n) {
	MGS_OpNode *opn = MGS_MemPool_alloc(o->mem, sizeof(MGS_OpNode));
	if (!opn)
		return NULL;
	if (!MGS_RunAlloc_for_sound(o, &opn->sound, n))
		return NULL;
	MGS_ProgramOpData *op_data = n->data;
	MGS_init_Osc(&opn->osc, o->srate);
	opn->osc.lut = MGS_Osc_LUT(op_data->wave);
	opn->osc.phase = MGS_Osc_PHASE(op_data->phase);
	opn->attr = op_data->attr;
	opn->freq = op_data->freq;
	opn->dynfreq = op_data->dynfreq;
	if (op_data->fmod != NULL) {
		if (!MGS_RunAlloc_get_modlist(o, op_data->fmod,
				&opn->fmods_id))
			return NULL;
	}
	if (op_data->pmod != NULL) {
		if (!MGS_RunAlloc_get_modlist(o, op_data->pmod,
				&opn->pmods_id))
			return NULL;
	}
	return opn;
}

#if 0
bool MGS_RunAlloc_for_node(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n) {
	switch (n->type) {
	case MGS_TYPE_OP:
		if (!MGS_RunAlloc_for_op(o, n))
			return false;
		break;
	}
	return true;
}
#endif
