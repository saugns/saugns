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

#include "runalloc.h"

enum {
	RECHECK_BUFS = 1<<0,
};

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
	size_t count = prg->base_counts[MGS_BASETYPE_SOUND];
	o->sound_list = MGS_MemPool_alloc(mem, count * sizeof(MGS_SoundNode*));
	if (!o->sound_list)
		return false;
	o->sndn_count = count;
	/*
	 * Add blank modlist as value 0.
	 */
	MGS_ModList *l = MGS_MemPool_alloc(mem, sizeof(MGS_ModList));
	if (!l || !MGS_PtrArr_add(&o->mod_lists, l))
		return false;
	return true;
}

void MGS_fini_RunAlloc(MGS_RunAlloc *restrict o) {
	MGS_EventArr_clear(&o->ev_arr);
	MGS_VoiceArr_clear(&o->voice_arr);
	MGS_PtrArr_clear(&o->mod_lists);
}

/*
 * Get modulator list ID for \p arr_data, adding the modulator
 * list to the collection.
 *
 * The ID 0 is reserved for blank modulator lists (\p arr_data
 * NULL or with a zero count). Other lists are allocated and a
 * new ID returned for each.
 *
 * \return true, or false on allocation failure
 */
static mgsNoinline bool MGS_RunAlloc_make_modlist(MGS_RunAlloc *restrict o,
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
	o->flags |= RECHECK_BUFS;
	return true;
}

/*
 * Check if a new modulator list needs to be assigned.
 */
#define NEED_MODLIST(data, prev_data, arr_memb) \
	((data)->arr_memb != NULL && \
	 (!(prev_data) || (prev_data)->arr_memb != (data)->arr_memb))

/*
 * Allocate and initialize an event,
 * leaving type-specific data blank.
 *
 * \return true, or false on allocation failure
 */
static bool MGS_RunAlloc_make_event(MGS_RunAlloc *restrict o,
		MGS_ProgramNode *restrict n) {
	MGS_EventNode *ev = MGS_EventArr_add(&o->ev_arr, NULL);
	if (!ev)
		return false;
	o->cur_ev = ev;
	o->cur_ev_id = o->ev_arr.count - 1;
	n->conv_id = o->cur_ev_id;
	ev->pos = 0 - o->next_ev_delay;
	o->next_ev_delay = 0;
	if (n->ref_prev != NULL) {
		const MGS_ProgramNode *ref = n->ref_prev;
		ev->status |= MGS_EV_UPDATE;
		ev->ref_i = ref->conv_id;
	}
	/* base_type remains MGS_BASETYPE_NONE until valid data assigned */
	return true;
}

/*
 * Set voice ID for sound node, allocating voice if needed.
 * Currently allocates one voice per root sound node; to improve.
 *
 * \return true, or false on allocation failure
 */
static bool MGS_RunAlloc_make_voice(MGS_RunAlloc *restrict o,
		MGS_SoundNode *restrict sndn,
		const MGS_ProgramNode *restrict n) {
	uint32_t voice_id;
	MGS_ProgramSoundData *sound_data = n->data;
	if (n->base_id == sound_data->root->base_id) {
		voice_id = o->voice_arr.count;
		MGS_VoiceNode *voice = MGS_VoiceArr_add(&o->voice_arr, NULL);
		if (!voice)
			return false;
		voice->root = sndn;
		voice->delay = lrintf(n->delay * o->srate);
		o->flags |= RECHECK_BUFS;
	} else {
		const MGS_ProgramNode *root_n = sound_data->root;
		MGS_SoundNode *root_sndn = o->sound_list[root_n->base_id];
		voice_id = root_sndn->voice_id;
	}
	sndn->voice_id = voice_id;
	return true;
}

/*
 * Fill in sound node data, performing voice allocation and
 * adding the node to the list if new (not an update node).
 *
 * To be called to initialize common data for sound nodes.
 *
 * \return true, or false on allocation failure
 */
static bool MGS_RunAlloc_init_sound(MGS_RunAlloc *restrict o,
		MGS_SoundNode *restrict sndn,
		const MGS_ProgramNode *restrict n) {
	MGS_ProgramSoundData *sound_data = n->data;
	MGS_ProgramSoundData *prev_sound_data = NULL;
	if (!n->ref_prev) {
		o->sound_list[n->base_id] = sndn;
		if (!MGS_RunAlloc_make_voice(o, sndn, n))
			return false;
	} else {
		prev_sound_data = n->ref_prev->data;
	}
	sndn->time = lrintf(sound_data->time.v * o->srate);
	sndn->amp = sound_data->amp;
	sndn->dynamp = sound_data->dynamp;
	sndn->pan = sound_data->pan;
	if (NEED_MODLIST(sound_data, prev_sound_data, amod)) {
		if (!MGS_RunAlloc_make_modlist(o, sound_data->amod,
				&sndn->amods_id))
			return false;
	}
	sndn->params = sound_data->params;
	sndn->type = n->type;
	MGS_EventNode *ev = o->cur_ev;
	ev->sndn = sndn;
	ev->base_type = MGS_BASETYPE_SOUND;
	return true;
}

/*
 * Allocate and initialize noise node.
 *
 * \return true, or false on allocation failure
 */
static bool MGS_RunAlloc_make_noise(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n) {
	MGS_EventNode *ev = o->cur_ev;
	MGS_NoiseNode *non;
	//MGS_ProgramWaveData *nod = n->data;
	//MGS_ProgramWaveData *prev_nod = NULL;
	if (!(ev->status & MGS_EV_UPDATE)) {
		non = MGS_MemPool_alloc(o->mem, sizeof(MGS_NoiseNode));
	} else {
		MGS_EventNode *ref_ev = &o->ev_arr.a[ev->ref_i];
		non = MGS_MemPool_memdup(o->mem,
				ref_ev->sndn, sizeof(MGS_NoiseNode));
		//prev_nod = n->ref_prev->data;
	}
	if (!non || !MGS_RunAlloc_init_sound(o, &non->sound, n))
		return false;
	MGS_init_NGen(&non->ngen, o->srate);
	return true;
}

/*
 * Allocate and initialize wave node.
 *
 * \return true, or false on allocation failure
 */
static bool MGS_RunAlloc_make_wave(MGS_RunAlloc *restrict o,
		const MGS_ProgramNode *restrict n) {
	MGS_EventNode *ev = o->cur_ev;
	MGS_WaveNode *won;
	MGS_ProgramWaveData *wod = n->data;
	MGS_ProgramWaveData *prev_wod = NULL;
	if (!(ev->status & MGS_EV_UPDATE)) {
		won = MGS_MemPool_alloc(o->mem, sizeof(MGS_WaveNode));
	} else {
		MGS_EventNode *ref_ev = &o->ev_arr.a[ev->ref_i];
		won = MGS_MemPool_memdup(o->mem,
				ref_ev->sndn, sizeof(MGS_WaveNode));
		prev_wod = n->ref_prev->data;
	}
	if (!won || !MGS_RunAlloc_init_sound(o, &won->sound, n))
		return false;
	MGS_init_Osc(&won->osc, o->srate);
	won->osc.wave = wod->wave;
	MGS_Osc_set_phase(&won->osc, wod->phase);
	won->attr = wod->attr;
	won->freq = wod->freq;
	won->dynfreq = wod->dynfreq;
	if (NEED_MODLIST(wod, prev_wod, fmod)) {
		if (!MGS_RunAlloc_make_modlist(o, wod->fmod, &won->fmods_id))
			return false;
	}
	if (NEED_MODLIST(wod, prev_wod, pmod)) {
		if (!MGS_RunAlloc_make_modlist(o, wod->pmod, &won->pmods_id))
			return false;
	}
	return true;
}

/*
 * Allocate and initialize type-dependent node.
 *
 * \return index node, or NULL on allocation failure or unsupported type
 */
static bool MGS_RunAlloc_make_sound(MGS_RunAlloc *restrict o,
		MGS_ProgramNode *restrict n) {
	if (!MGS_RunAlloc_make_event(o, n))
		return false;
	switch (n->type) {
	case MGS_TYPE_NOISE:
		if (!MGS_RunAlloc_make_noise(o, n))
			return false;
		break;
	case MGS_TYPE_WAVE:
		if (!MGS_RunAlloc_make_wave(o, n))
			return false;
		break;
	default:
		MGS_warning("runalloc",
"sound data type %hhd unsupported, event %d left blank",
				n->type, o->cur_ev_id);
		break;
	}
	return true;
}

static bool MGS_RunAlloc_recheck_bufs(MGS_RunAlloc *restrict o);

/**
 * Make nodes for an input node list.
 *
 * \return true, or false on allocation failure
 */
bool MGS_RunAlloc_for_nodelist(MGS_RunAlloc *restrict o,
		MGS_ProgramNode *restrict first_n) {
	MGS_ProgramNode *n = first_n;
	while (n != NULL) {
		uint32_t delay = lrintf(n->delay * o->srate);
		o->next_ev_delay += delay;
		switch (n->base_type) {
		case MGS_BASETYPE_SOUND:
			if ((o->next_ev_delay > 0) &&
					!MGS_RunAlloc_recheck_bufs(o))
				return false;
			if (!MGS_RunAlloc_make_sound(o, n))
				return false;
			break;
		}
		n = n->next;
	}
	if (!MGS_RunAlloc_recheck_bufs(o))
		return false;
	return true;
}

static size_t calc_bufs_sub(MGS_RunAlloc *restrict o,
		size_t count_from, uint32_t mods_id);

/*
 * Traversal mirroring the function for running an MGS_NoiseNode.
 */
static size_t calc_bufs_noise(MGS_RunAlloc *restrict o,
		size_t count_from, MGS_NoiseNode *restrict n) {
	size_t count = count_from, max_count = count_from;
	++count;
	if (n->sound.amods_id > 0) {
		size_t sub_count = calc_bufs_sub(o, count, n->sound.amods_id);
		if (max_count < sub_count) max_count = sub_count;
		++count;
	} else {
		++count;
	}
	++count;
	if (max_count < count) max_count = count;
	return max_count;
}

/*
 * Traversal mirroring the function for running an MGS_WaveNode.
 */
static size_t calc_bufs_wave(MGS_RunAlloc *restrict o,
		size_t count_from, MGS_WaveNode *restrict n) {
	size_t count = count_from, max_count = count_from;
	++count;
	++count;
	++count;
	if (n->fmods_id > 0) {
		size_t sub_count = calc_bufs_sub(o, count, n->fmods_id);
		if (max_count < sub_count) max_count = sub_count;
	}
	if (n->pmods_id > 0) {
		size_t sub_count = calc_bufs_sub(o, count, n->pmods_id);
		if (max_count < sub_count) max_count = sub_count;
	}
	if (n->sound.amods_id > 0) {
		size_t sub_count = calc_bufs_sub(o, count, n->sound.amods_id);
		if (max_count < sub_count) max_count = sub_count;
		++count;
	} else {
		++count;
	}
	++count;
	if (max_count < count) max_count = count;
	return max_count;
}

static size_t calc_bufs_sub(MGS_RunAlloc *restrict o,
		size_t count_from, uint32_t mods_id) {
	MGS_ModList *mod_list = MGS_PtrArr_GET(&o->mod_lists, mods_id);
	size_t max_count = count_from;
	for (size_t i = 0; i < mod_list->count; ++i) {
		MGS_SoundNode *n = o->sound_list[mod_list->ids[i]];
		size_t sub_count = count_from;
		switch (n->type) {
		case MGS_TYPE_NOISE:
			sub_count = calc_bufs_noise(o,
					count_from, (MGS_NoiseNode*) n);
			break;
		case MGS_TYPE_WAVE:
			sub_count = calc_bufs_wave(o,
					count_from, (MGS_WaveNode*) n);
			break;
		}
		if (max_count < sub_count)
			max_count = sub_count;
	}
	return max_count;
}

/*
 * Update buffer counts for new node traversals.
 */
static bool MGS_RunAlloc_recheck_bufs(MGS_RunAlloc *restrict o) {
	if (!(o->flags & RECHECK_BUFS))
		return true;
	for (size_t i = 0; i < o->voice_arr.count; ++i) {
		MGS_VoiceNode *voice = &o->voice_arr.a[i];
		MGS_SoundNode *sndn = voice->root;
		size_t count = 0;
		switch (sndn->type) {
		case MGS_TYPE_NOISE:
			count = calc_bufs_noise(o, 0, (MGS_NoiseNode*) sndn);
			break;
		case MGS_TYPE_WAVE:
			count = calc_bufs_wave(o, 0, (MGS_WaveNode*) sndn);
			break;
		}
		if (o->max_bufs < count)
			o->max_bufs = count;
	}
	o->flags &= ~RECHECK_BUFS;
	return true;
}
