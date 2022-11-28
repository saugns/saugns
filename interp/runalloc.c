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

#include "runalloc.h"

enum {
	RECHECK_BUFS = 1<<0,
};

static uint32_t random_next(uint32_t *seed) {
	return *seed = mgs_xorshift32(*seed);
}

//static uint32_t spread_next(uint32_t *seed) {
//	return *seed += MGS_FIBH32;
//}

static mgsModList *create_ModList(const mgsProgramArrData *restrict arr_data,
		mgsMemPool *restrict mem) {
	mgsModList *o = mgs_mpalloc(mem, sizeof(mgsModList) +
			arr_data->count * sizeof(uint32_t));
	mgsProgramNode *n = arr_data->first_node;
	size_t i = 0;
	o->count = arr_data->count;
	while (n != NULL) {
		mgsProgramSoundData *sound_data = n->data;
		o->ids[i++] = n->base_id;
		n = sound_data->nested_next;
	}
	return o;
}

bool mgs_init_RunAlloc(mgsRunAlloc *restrict o,
		const mgsProgram *restrict prg, uint32_t srate,
		mgsMemPool *restrict mem) {
	*o = (mgsRunAlloc){0};
	o->prg = prg;
	o->srate = srate;
	o->mem = mem;
	size_t count = prg->base_counts[MGS_BASETYPE_SOUND];
	o->sound_list = mgs_mpalloc(mem, count * sizeof(mgsSoundNode*));
	if (!o->sound_list)
		return false;
	o->sndn_count = count;
	o->seed = MGS_XORSHIFT32_SEED;
	/*
	 * Add blank modlist as value 0.
	 */
	mgsModList *l = mgs_mpalloc(mem, sizeof(mgsModList));
	if (!l || !mgsPtrArr_add(&o->mod_lists, l))
		return false;
	return true;
}

void mgs_fini_RunAlloc(mgsRunAlloc *restrict o) {
	mgsEventArr_clear(&o->ev_arr);
	mgsVoiceArr_clear(&o->voice_arr);
	mgsPtrArr_clear(&o->mod_lists);
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
static mgsNoinline bool mgsRunAlloc_make_modlist(mgsRunAlloc *restrict o,
		const mgsProgramArrData *restrict arr_data,
		uint32_t *restrict id) {
	if (!arr_data || !arr_data->count) {
		*id = 0;
		return true;
	}
	mgsModList *l = create_ModList(arr_data, o->mem);
	if (!l || !(mgsPtrArr_add(&o->mod_lists, l)))
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
static bool mgsRunAlloc_make_event(mgsRunAlloc *restrict o,
		mgsProgramNode *restrict n) {
	mgsEventNode *ev = mgsEventArr_add(&o->ev_arr, NULL);
	if (!ev)
		return false;
	o->cur_ev = ev;
	o->cur_ev_id = o->ev_arr.count - 1;
	n->conv_id = o->cur_ev_id;
	ev->pos = 0 - o->next_ev_delay;
	o->next_ev_delay = 0;
	if (n->ref_prev != NULL) {
		const mgsProgramNode *ref = n->ref_prev;
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
static bool mgsRunAlloc_make_voice(mgsRunAlloc *restrict o,
		mgsSoundNode *restrict sndn,
		const mgsProgramNode *restrict n) {
	uint32_t voice_id;
	mgsProgramSoundData *sound_data = n->data;
	if (n->base_id == sound_data->root->base_id) {
		voice_id = o->voice_arr.count;
		mgsVoiceNode *voice = mgsVoiceArr_add(&o->voice_arr, NULL);
		if (!voice)
			return false;
		voice->root = sndn;
		voice->delay = lrintf(n->delay * o->srate);
		o->flags |= RECHECK_BUFS;
	} else {
		const mgsProgramNode *root_n = sound_data->root;
		mgsSoundNode *root_sndn = o->sound_list[root_n->base_id];
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
static bool mgsRunAlloc_init_sound(mgsRunAlloc *restrict o,
		mgsSoundNode *restrict sndn,
		const mgsProgramNode *restrict n) {
	mgsProgramSoundData *sound_data = n->data;
	mgsProgramSoundData *prev_sound_data = NULL;
	if (!n->ref_prev) {
		o->sound_list[n->base_id] = sndn;
		if (!mgsRunAlloc_make_voice(o, sndn, n))
			return false;
	} else {
		prev_sound_data = n->ref_prev->data;
	}
	sndn->time = lrintf(sound_data->time.v * o->srate);
	sndn->amp = sound_data->amp;
	sndn->dynamp = sound_data->dynamp;
	sndn->pan = sound_data->pan;
	if (NEED_MODLIST(sound_data, prev_sound_data, amod)) {
		if (!mgsRunAlloc_make_modlist(o, sound_data->amod,
				&sndn->amods_id))
			return false;
	}
	sndn->params = sound_data->params;
	sndn->type = n->type;
	mgsEventNode *ev = o->cur_ev;
	ev->sndn = sndn;
	ev->base_type = MGS_BASETYPE_SOUND;
	return true;
}

/*
 * Allocate and initialize line node.
 *
 * \return true, or false on allocation failure
 */
static bool mgsRunAlloc_make_line(mgsRunAlloc *restrict o,
		const mgsProgramNode *restrict n) {
	mgsEventNode *ev = o->cur_ev;
	mgsLineNode *lon;
	mgsProgramLineData *lod = n->data;
	mgsProgramLineData *prev_lod = NULL;
	if (!(ev->status & MGS_EV_UPDATE)) {
		lon = mgs_mpalloc(o->mem, sizeof(mgsLineNode));
	} else {
		mgsEventNode *ref_ev = &o->ev_arr.a[ev->ref_i];
		lon = mgs_mpmemdup(o->mem,
				ref_ev->sndn, sizeof(mgsLineNode));
		prev_lod = n->ref_prev->data;
	}
	if (!lon || !mgsRunAlloc_init_sound(o, &lon->sound, n))
		return false;
	lon->line = lod->line;
	if (!prev_lod)
		mgsLine_setup(&lon->line, o->srate);
	//mgsLine_copy(&lon->line, &lod->line, o->srate);
	return true;
}

/*
 * Allocate and initialize noise node.
 *
 * \return true, or false on allocation failure
 */
static bool mgsRunAlloc_make_noise(mgsRunAlloc *restrict o,
		const mgsProgramNode *restrict n) {
	mgsEventNode *ev = o->cur_ev;
	mgsNoiseNode *non;
	//mgsProgramNoiseData *nod = n->data;
	//mgsProgramNoiseData *prev_nod = NULL;
	if (!(ev->status & MGS_EV_UPDATE)) {
		non = mgs_mpalloc(o->mem, sizeof(mgsNoiseNode));
	} else {
		mgsEventNode *ref_ev = &o->ev_arr.a[ev->ref_i];
		non = mgs_mpmemdup(o->mem,
				ref_ev->sndn, sizeof(mgsNoiseNode));
		//prev_nod = n->ref_prev->data;
	}
	if (!non || !mgsRunAlloc_init_sound(o, &non->sound, n))
		return false;
	mgs_init_NGen(&non->ngen, random_next(&o->seed));
	return true;
}

/*
 * Allocate and initialize wave node.
 *
 * \return true, or false on allocation failure
 */
static bool mgsRunAlloc_make_wave(mgsRunAlloc *restrict o,
		const mgsProgramNode *restrict n) {
	mgsEventNode *ev = o->cur_ev;
	mgsWaveNode *won;
	mgsProgramWaveData *wod = n->data;
	mgsProgramWaveData *prev_wod = NULL;
	if (!(ev->status & MGS_EV_UPDATE)) {
		won = mgs_mpalloc(o->mem, sizeof(mgsWaveNode));
	} else {
		mgsEventNode *ref_ev = &o->ev_arr.a[ev->ref_i];
		won = mgs_mpmemdup(o->mem,
				ref_ev->sndn, sizeof(mgsWaveNode));
		prev_wod = n->ref_prev->data;
	}
	if (!won || !mgsRunAlloc_init_sound(o, &won->sound, n))
		return false;
	mgs_init_Osc(&won->osc, o->srate);
	won->osc.wave = wod->wave;
	mgsOsc_set_phase(&won->osc, wod->phase);
	won->attr = wod->attr;
	won->freq = wod->freq;
	won->dynfreq = wod->dynfreq;
	if (NEED_MODLIST(wod, prev_wod, fmod)) {
		if (!mgsRunAlloc_make_modlist(o, wod->fmod, &won->fmods_id))
			return false;
	}
	if (NEED_MODLIST(wod, prev_wod, pmod)) {
		if (!mgsRunAlloc_make_modlist(o, wod->pmod, &won->pmods_id))
			return false;
	}
	return true;
}

/*
 * Allocate and initialize type-dependent node.
 *
 * \return index node, or NULL on allocation failure or unsupported type
 */
static bool mgsRunAlloc_make_sound(mgsRunAlloc *restrict o,
		mgsProgramNode *restrict n) {
	if (!mgsRunAlloc_make_event(o, n))
		return false;
	switch (n->type) {
	case MGS_TYPE_LINE:
		if (!mgsRunAlloc_make_line(o, n))
			return false;
		break;
	case MGS_TYPE_NOISE:
		if (!mgsRunAlloc_make_noise(o, n))
			return false;
		break;
	case MGS_TYPE_WAVE:
		if (!mgsRunAlloc_make_wave(o, n))
			return false;
		break;
	default:
		mgs_warning("runalloc",
"sound data type %hhd unsupported, event %d left blank",
				n->type, o->cur_ev_id);
		break;
	}
	return true;
}

static bool mgsRunAlloc_recheck_bufs(mgsRunAlloc *restrict o);

/**
 * Make nodes for an input node list.
 *
 * \return true, or false on allocation failure
 */
bool mgsRunAlloc_for_nodelist(mgsRunAlloc *restrict o,
		mgsProgramNode *restrict first_n) {
	mgsProgramNode *n = first_n;
	while (n != NULL) {
		uint32_t delay = lrintf(n->delay * o->srate);
		o->next_ev_delay += delay;
		switch (n->base_type) {
		case MGS_BASETYPE_SOUND:
			if ((o->next_ev_delay > 0) &&
					!mgsRunAlloc_recheck_bufs(o))
				return false;
			if (!mgsRunAlloc_make_sound(o, n))
				return false;
			break;
		}
		n = n->next;
	}
	if (!mgsRunAlloc_recheck_bufs(o))
		return false;
	return true;
}

static size_t calc_bufs_sub(mgsRunAlloc *restrict o,
		size_t count_from, uint32_t mods_id);

/*
 * Traversal mirroring the function for running an mgsLineNode.
 */
static size_t calc_bufs_line(mgsRunAlloc *restrict o,
		size_t count_from, mgsNoiseNode *restrict n) {
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
 * Traversal mirroring the function for running an mgsNoiseNode.
 */
static size_t calc_bufs_noise(mgsRunAlloc *restrict o,
		size_t count_from, mgsNoiseNode *restrict n) {
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
 * Traversal mirroring the function for running an mgsWaveNode.
 */
static size_t calc_bufs_wave(mgsRunAlloc *restrict o,
		size_t count_from, mgsWaveNode *restrict n) {
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

static size_t calc_bufs_sub(mgsRunAlloc *restrict o,
		size_t count_from, uint32_t mods_id) {
	mgsModList *mod_list = mgsPtrArr_GET(&o->mod_lists, mods_id);
	size_t max_count = count_from;
	for (size_t i = 0; i < mod_list->count; ++i) {
		mgsSoundNode *n = o->sound_list[mod_list->ids[i]];
		size_t sub_count = count_from;
		switch (n->type) {
		case MGS_TYPE_LINE:
			sub_count = calc_bufs_line(o,
					count_from, (mgsNoiseNode*) n);
			break;
		case MGS_TYPE_NOISE:
			sub_count = calc_bufs_noise(o,
					count_from, (mgsNoiseNode*) n);
			break;
		case MGS_TYPE_WAVE:
			sub_count = calc_bufs_wave(o,
					count_from, (mgsWaveNode*) n);
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
static bool mgsRunAlloc_recheck_bufs(mgsRunAlloc *restrict o) {
	if (!(o->flags & RECHECK_BUFS))
		return true;
	for (size_t i = 0; i < o->voice_arr.count; ++i) {
		mgsVoiceNode *voice = &o->voice_arr.a[i];
		mgsSoundNode *sndn = voice->root;
		size_t count = 0;
		switch (sndn->type) {
		case MGS_TYPE_LINE:
			count = calc_bufs_line(o, 0, (mgsNoiseNode*) sndn);
			break;
		case MGS_TYPE_NOISE:
			count = calc_bufs_noise(o, 0, (mgsNoiseNode*) sndn);
			break;
		case MGS_TYPE_WAVE:
			count = calc_bufs_wave(o, 0, (mgsWaveNode*) sndn);
			break;
		}
		if (o->max_bufs < count)
			o->max_bufs = count;
	}
	o->flags &= ~RECHECK_BUFS;
	return true;
}
