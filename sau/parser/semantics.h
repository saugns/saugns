/* SAU library: Extra semantics handling code for parser.
 * Copyright (c) 2011-2012, 2017-2025 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h> // for sauParse_print_info()

/*
 * Semantics code running with parsing, prior to the audio rendering
 * interpretation. As a first layer of interpreting, this focuses on
 * "What does it need, what exactly does it use?". Thus, later stage
 * interpreting can handle it simply, focusing on signal processing.
 *
 * In part, this is timing logic (pre-calculating when possible). In
 * part it is 'measuring' and allocating IDs, establishing exact use
 * of resources in advance. And each generator is mapped to a voice.
 */

typedef struct sauParseEvBranch {
	sauParseEvData *events;
	struct sauParseEvBranch *prev;
} sauParseEvBranch;

static const sauProgramIDArr blank_idarr = {0}; // used as value for "none"

static sauProgramIDArr *
create_ProgramIDArr(sauMempool *restrict mp, uint32_t count) {
	if (!count)
		return (sauProgramIDArr*) &blank_idarr; // treated as read-only
	size_t size = count * sizeof(uint32_t);
	sauProgramIDArr *idarr = NULL;
	if (!(idarr = sau_mpalloc(mp, sizeof(sauProgramIDArr) + size)))
		return NULL;
	idarr->count = count;
	return idarr;
}

static const sauProgramIDArr *
clone_ProgramIDArr(sauMempool *restrict mp,
		const uint32_t *restrict ids, uint32_t count) {
	if (!count)
		return &blank_idarr;
	size_t size = count * sizeof(uint32_t);
	sauProgramIDArr *idarr = NULL;
	if (!(idarr = sau_mpalloc(mp, sizeof(sauProgramIDArr) + size)))
		return NULL;
	idarr->count = count;
	memcpy(idarr->ids, ids, size);
	return idarr;
}

static const sauProgramIDArr *
concat_ProgramIDArr(sauMempool *restrict mp,
		const sauProgramIDArr *arr0, const sauProgramIDArr *arr1) {
	if (!arr0 || arr0->count == 0)
		return arr1;
	if (!arr1 || arr1->count == 0)
		return arr0;
	size_t size0 = sizeof(uint32_t) * arr0->count;
	size_t size1 = sizeof(uint32_t) * arr1->count;
	sauProgramIDArr *idarr = sau_mpalloc(mp,
			sizeof(sauProgramIDArr) + size0 + size1);
	if (!idarr)
		return NULL;
	idarr->count = arr0->count + arr1->count;
	memcpy(idarr->ids, arr0->ids, size0);
	memcpy(&idarr->ids[arr0->count], arr1->ids, size1);
	return idarr;
}

sauArrType(ObjInfoArr, sauParseObjInfo, _)

/*
 * Per-voice state during data allocation.
 */
typedef struct sauVoAllocState {
	uint32_t obj_id;
	uint32_t time_ms;
	bool has_new_graph  : 1; // traverse to make updated graph in event
	bool has_gen_renum  : 1; // traverse to update object info in array
	bool has_gen_expiry : 1; // traverse to update generator expiry state
} sauVoAllocState;

sauArrType(sauVoAlloc, sauVoAllocState, _)

/*
 * Per-generator state used during program data allocation.
 */
typedef struct sauGenAllocState {
	uint32_t obj_id;
	uint32_t time_ms;
	struct sauParseGenData *last_gd;
	bool is_visited   : 1; // for voice traversal
	bool is_timed     : 1; // use \a time_ms to change \a is_expired?
	bool is_expired   : 1; // available for reuse
	bool has_next_ref : 1; // copied from generator node on update
} sauGenAllocState;

sauArrType(sauGenAlloc, sauGenAllocState, _)

sauArrType(IDsArr, sauProgramIDs, )
sauArrType(GenRefArr, sauProgramGenRef, )

sauArrType(GenDataArr, sauParseGenData*, _)
sauArrType(IDBuf, uint32_t, )

typedef struct ParseSem {
	sauVoAlloc va;
	GenRefArr vo_graph;
	uint32_t gen_nest_level, gen_nest_max;
	uint32_t tot_dur_ms;
	ObjInfoArr obj_arr;
	IDBuf obj_dead; // track never-used-again objects for ID reuse
	sauGenAlloc ga;
	sauParseGenData *ga_main_clone; // point to cloned gen during cloning
	IDBuf idbuf;
	size_t ev_count;
	GenDataArr ev_gen_data; // flat list of pointers
	IDsArr ev_ids; // tmp buffer for modulator lists
	sauMempool *mp;
} ParseSem;

static sauParseObjInfo *
sem_objinfo_add(ParseSem *restrict o, sauParseObjRef *restrict ref,
		uint8_t obj_type, uint8_t gen_type) {
	uint32_t id;
	sauParseObjInfo *info;
	if (o->obj_dead.count > 0) {
		id = *IDBuf_pop(&o->obj_dead);
		info = &o->obj_arr.a[id];
		*info = (sauParseObjInfo){0};
	} else {
		id = o->obj_arr.count;
		info = _ObjInfoArr_add(&o->obj_arr);
		if (!info)
			return NULL;
	}
	ref->obj_id = id;
	info->obj_type = ref->obj_type = obj_type;
	info->gen_type = ref->gen_type = gen_type;
	info->last_vo_id = ref->vo_id = SAU_PVO_NO_ID;
	info->last_gen_id = SAU_PGEN_NO_ID;
	info->dst_obj_id = SAU_POBJ_NO_ID;
	return info;
}

static sauParseObjInfo *
sem_objinfo_add_gen(ParseSem *restrict o, sauParseGenData *restrict gen,
		uint32_t owner_obj_id, uint8_t obj_type, uint8_t gen_type) {
	sauParseObjInfo *info;
	if (!(info = sem_objinfo_add(o, &gen->ref, obj_type, gen_type)))
		return NULL;
	gen->is_new = true;
	if (owner_obj_id != SAU_POBJ_NO_ID) {
		gen->is_nested = true;
		sauParseObjInfo *owner_info = &o->obj_arr.a[owner_obj_id];
		// modulators count as reachable from carrier
		info->is_labeled = owner_info->is_labeled;
		info->root_gen_obj = owner_info->root_gen_obj;
	} else {
		info->root_gen_obj = (info - o->obj_arr.a); // this object
	}
	for (int i = 1; i < SAU_MOD_NAMED; ++i)
		info->mods_idarr[i-1] = &blank_idarr;
	return info;
}

/*
 * Set the array of ID arrays for a generator node,
 * allocating the sparse variation from the non-sparse one.
 *
 * \return true, or false on allocation failure
 */
static bool sem_set_gen_idsarr(ParseSem *restrict o,
		sauParseGenData *restrict gen,
		const sauProgramIDArr *new_mods[SAU_MOD_NAMED-1]) {
	o->ev_ids.count = 0; // reuse allocation
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		sauProgramIDs *ids;
		if (!new_mods[i]) continue;
		if (!(ids = IDsArr_add(&o->ev_ids)))
			return false;
		ids->a = new_mods[i];
		ids->use = i + 1;
	}
	gen->mod_count = o->ev_ids.count;
	return IDsArr_mpmemdup(&o->ev_ids,
			(sauProgramIDs**) &gen->mods_idarr, o->mp);
}

static bool
sem_vograph_traverse(ParseSem *restrict o, sauVoAllocState *restrict vas,
		sauParseEvData *restrict ev);

typedef bool (*semVoGraph_cb)(ParseSem *o, sauGenAllocState *gas,
		sauProgramGenRef *gen_ref);

static bool
sem_vograph_handle_gen_node(ParseSem *restrict o, semVoGraph_cb node_cb,
		bool gens_expired, sauVoAllocState *restrict vas,
		uint32_t obj_id, sauProgramGenRef *restrict gen_ref);

/*
 * Traverse generator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static inline bool
sem_vograph_handle_gen_list(ParseSem *restrict o, semVoGraph_cb node_cb,
		bool gens_expired, sauVoAllocState *restrict vas,
		const sauProgramIDArr *restrict gen_list, uint8_t mod_use) {
	sauProgramGenRef gen_ref = {0, mod_use, o->gen_nest_level};
	for (uint32_t i = 0; i < gen_list->count; ++i) {
		uint32_t obj_id = gen_list->ids[i];
		if (!sem_vograph_handle_gen_node(o, node_cb,
					gens_expired, vas, obj_id, &gen_ref))
			return false;
	}
	return true;
}

#define sem_sum_dur_ms(o, add_ms) ((o)->tot_dur_ms += (add_ms))

/*
 * Time-updating part of voice allocation update. This can be ran
 * in the absence of the rest, if a voice ID is not used for a node.
 */
static void
sem_voalloc_timing(ParseSem *restrict o, sauParseEvData *restrict e) {
	sem_sum_dur_ms(o, e->wait_ms);
	/*
	 * Count down remaining durations before voice reuse.
	 */
	if (e->wait_ms > 0) for (uint32_t id = 0; id < o->va.count; ++id) {
		if (o->va.a[id].time_ms <= e->wait_ms)
			o->va.a[id].time_ms = 0;
		else
			o->va.a[id].time_ms -= e->wait_ms;
	}
	/*
	 * Also update generators for generator reuse; when a
	 * call to sem_genalloc_update() is done, this result
	 * is used there. (Countdown is per-event after all.)
	 *
	 * Avoid reusing IDs during split steps. These may be
	 * for generators adding modulators and/or any mod in
	 * a mod list. Object ID reuse can't be done then, as
	 * next refs exist, which need old objects to remain.
	 *
	 * To reuse things properly for zero durations, where
	 * generator times set and also the event wait times,
	 * both, are zero, the check for if a generator has a
	 * next ref is *also* needed.
	 */
	for (uint32_t id = 0; id < o->ga.count; ++id) {
		sauGenAllocState *gas = &o->ga.a[id];
		if (gas->time_ms <= e->wait_ms)
			gas->time_ms = 0;
		else
			gas->time_ms -= e->wait_ms;
		if (gas->has_next_ref) continue;
		if (!(gas->time_ms == 0 && gas->is_timed)) continue;
		gas->is_expired = true;
		/*
		 * Set up graph traversal to mark linked nodes expired.
		 * This testing must be done before reuse of the voice.
		 */
		sauParseObjInfo *info = &o->obj_arr.a[gas->obj_id];
		info = &o->obj_arr.a[info->root_gen_obj];
		if (info->last_vo_id != SAU_PVO_NO_ID) {
			sauVoAllocState *vas = &o->va.a[info->last_vo_id];
			vas->has_gen_expiry = true;
		}
	}
	e->vo_id = SAU_PVO_NO_ID; // not assigned yet
}

/*
 * ID-updating part of voice allocation update. Return (re)allocated
 * state for voice. Must be ran after sem_voalloc_timing().
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 *
 * \return current array element, or NULL on allocation failure
 */
static sauVoAllocState *
sem_voalloc_update(ParseSem *restrict o, sauParseEvData *restrict e) {
	uint32_t vo_id, obj_id;
	bool has_new_graph = false;
	/*
	 * Use voice without change if possible.
	 */
	sauParseGenData *obj = e->main_obj;
	sauParseObjInfo *info = &o->obj_arr.a[obj->ref.obj_id];
	info = &o->obj_arr.a[(obj_id = info->root_gen_obj)];
	sauVoAllocState *vas;
	if (obj->prev_ref && info->last_vo_id != SAU_PVO_NO_ID) {
		vo_id = info->last_vo_id;
		vas = &o->va.a[vo_id];
		goto PRESERVED;
	}
	if (!obj->is_nested && (obj->params & SAU_PGENP_TIME) != 0)
		has_new_graph = true; // need to assign one
	/*
	 * Reuse first lowest free voice (duration expired), if any.
	 */
	for (uint32_t id = 0; id < o->va.count; ++id) {
		vas = &o->va.a[id];
		if (vas->time_ms == 0) {
			if (vas->has_gen_expiry) // must traverse before reuse
				sem_vograph_traverse(o, vas, e);
			sauParseObjInfo *old_info = &o->obj_arr.a[vas->obj_id];
			old_info->last_vo_id = SAU_PVO_NO_ID; // renumber on use
			*vas = (sauVoAllocState){0};
			vo_id = id;
			goto RECYCLED;
		}
	}
	vo_id = o->va.count;
	if (!(vas = _sauVoAlloc_add(&o->va)))
		return NULL;
RECYCLED:
	info->last_vo_id = vo_id;
	vas->obj_id = obj_id;
PRESERVED:
	e->carr_obj_id = vas->obj_id;
	if ((e->ev_flags & SAU_PEV_VOICE_SET_DUR) != 0)
		vas->time_ms = e->dur_ms;
	e->vo_id = obj->ref.vo_id = vo_id;
	vas->has_new_graph = has_new_graph;
	vas->has_gen_renum = false; // always clear first
	return vas;
}

static bool
sem_vograph_cb_clonegen(ParseSem *restrict o, sauGenAllocState *restrict gas,
		sauProgramGenRef *restrict gen_ref);

/*
 * Update generator data for event and return object info.
 *
 * Use the current generator if any, otherwise reusing an expired
 * generator if possible, or allocating a new if not. Generators
 * may sometimes be moved to new IDs after old ones are recycled.
 *
 * \return sauParseObjInfo, or NULL on allocation failure
 */
static sauParseObjInfo *
sem_genalloc_update(ParseSem *restrict o, sauParseGenData *restrict g) {
	uint32_t obj_id, gen_id;
	sauParseGenData *swap_with_old = NULL;
	/*
	 * Use generator without change if possible.
	 */
	sauParseObjInfo *info = &o->obj_arr.a[(obj_id = g->ref.obj_id)];
	sauGenAllocState *gas;
	if (g->prev_ref && !g->is_cloned) {
		if (info->last_gen_id != SAU_PGEN_NO_ID) {
			gen_id = info->last_gen_id;
			gas = &o->ga.a[gen_id];
			goto PRESERVED;
		} else {
			// last time, it was clobbered by greedy algorithm
			swap_with_old = info->swap_from_gd;
			// TODO: test usefulness of this code when scripts
			// can use some feature to move gens between lists
			uint32_t vo_id = g->event->vo_id;
			if (vo_id != SAU_PVO_NO_ID) {
				sauVoAllocState *vas = &o->va.a[vo_id];
				vas->has_gen_renum = true;
			}
		}
	}
	/*
	 * Reuse first lowest free generator, if any. Note that when
	 * recursing in deep cloning traversal, reuse is restricted.
	 *
	 * On reuse, set info for reused object pointing to new user
	 * data so the latter can later be made to instruct the copy
	 * (preserving) of the old generator to a new ID, if needed.
	 *
	 * Also reuse object IDs when possible -- less aggressively.
	 * Unlike generator IDs, they can't be safely changed once a
	 * node using the ID exists. Therefore mark IDs for later...
	 */
	if (!swap_with_old) for (uint32_t id = 0; id < o->ga.count; ++id) {
		gas = &o->ga.a[id];
		if (gas->is_expired && !gas->is_visited) {
			sauParseObjInfo *old_info = &o->obj_arr.a[gas->obj_id];
			old_info->last_gen_id = SAU_PGEN_NO_ID; // to renumber
			if (old_info->is_labeled) {
				// In case reuse is hasty, allow gen ID move.
				old_info->swap_from_gd = g;
			} else {
				// Reuse object ID slot, not now but for next
				// object. No room for mistakes in this case.
				if (!IDBuf_push(&o->obj_dead, &gas->obj_id))
					return NULL;
			}
			*gas = (sauGenAllocState){0};
			gen_id = id;
			goto RECYCLED;
		}
	}
	gen_id = o->ga.count;
	if (!(gas = _sauGenAlloc_add(&o->ga)))
		return NULL;
RECYCLED:
	info->last_gen_id = gen_id;
	if (swap_with_old) swap_with_old->swap_to_id = gen_id;
	gas->obj_id = obj_id;
PRESERVED:
	info->last_gd = g;
	if (g->params & SAU_PGENP_TIME) {
		gas->time_ms = g->time.v_ms;
		gas->is_timed = !(g->time.flags & SAU_TIMEP_IMPLICIT);
		if (!gas->is_timed || gas->time_ms > 0)
			gas->is_expired = false;
	}
	gas->has_next_ref = g->has_next_ref;
	g->id = info->last_gen_id;
	g->swap_to_id = SAU_PGEN_NO_ID;
	g->copy_from_id = SAU_PGEN_NO_ID;
	/*
	 * Deep cloning of a modulator tree begins here. Traverse and use
	 * a callback to make the extra generators. A few details are needed
	 * to avoid trouble on recursion.
	 */
	if (g->is_cloned && !o->ga_main_clone) {
		sauVoAllocState *vas = &o->va.a[g->event->vo_id];
		uint32_t src_obj_id = g->prev_ref->ref.obj_id;
		o->ga_main_clone = g; // for use by the callback function
		if (!sem_vograph_handle_gen_node(o, sem_vograph_cb_clonegen,
				false, vas, src_obj_id, NULL))
			return NULL;
		o->ga_main_clone = NULL;
		vas->has_new_graph = true;
		info = &o->obj_arr.a[obj_id]; // array may have resized!
	}
	return info;
}

/*
 * Make a modified copy of a modulator ID array, replacing IDs to
 * point to generator clones. Used as part of a deep cloning process.
 *
 * \return true, or false on allocation failure
 */
static sauProgramIDArr *
sem_make_clone_idarr(ParseSem *restrict o,
		const sauProgramIDArr *restrict src) {
	sauProgramIDArr *dst = create_ProgramIDArr(o->mp, src->count);
	if (!dst)
		return NULL;
	for (uint32_t j = 0; j < src->count; ++j) {
		sauParseObjInfo *src_info = &o->obj_arr.a[src->ids[j]];
		dst->ids[j] = src_info->dst_obj_id;
	}
	return dst;
}

/*
 * Callback used by sem_genalloc_update() to clone linked modulators
 * when handling a "first" cloned generator.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_cb_clonegen(ParseSem *restrict o, sauGenAllocState *restrict gas,
		sauProgramGenRef *restrict gen_ref) {
	sauParseGenData *dst_gen;
	sauParseObjInfo *dst_info, *info = &o->obj_arr.a[gas->obj_id];
	bool is_main = !gen_ref;
	if (is_main) { // detect first gen clone, from parser begin_gen()
		dst_gen = o->ga_main_clone;
		dst_info = &o->obj_arr.a[dst_gen->ref.obj_id];
	} else {
		sauParseGenData *dst_pgen = info->last_gd;
		uint32_t type = info->gen_type;
		sauParseGenData **dst_gen_a = _GenDataArr_add(&o->ev_gen_data);
		dst_gen = sau_mpalloc(o->mp, sizeof(*dst_gen));
		if (!dst_gen_a || !dst_gen)
			return false;
		*dst_gen_a = dst_gen;
		dst_gen->prev_ref = dst_pgen;
		dst_gen->event = o->ga_main_clone->event;
		dst_gen->is_cloned = true;
		if (!(dst_info = sem_objinfo_add_gen(o, dst_gen,
				    o->ga_main_clone->ref.obj_id,
				    SAU_POBJT_GEN, type)) ||
		    !sem_genalloc_update(o, dst_gen))
			return false;
		info = &o->obj_arr.a[gas->obj_id]; // array may have resized!
	}
	info->dst_obj_id = dst_gen->ref.obj_id;
	dst_gen->copy_from_id = info->last_gen_id;
	/*
	 * Clone/update modulator ID lists as well, to link to the new objects.
	 */
	const sauProgramIDArr *new_mods[SAU_MOD_NAMED - 1] = {0};
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		const sauProgramIDArr *src = info->mods_idarr[i];
		if (!src->count) continue;
		if (!(new_mods[i] = sem_make_clone_idarr(o, src)))
			return false;
		dst_info->mods_idarr[i] = new_mods[i];
	}
	if (is_main)
		return true; // final modulators set after processing params
	return sem_set_gen_idsarr(o, dst_gen, new_mods);
}

/*
 * Make a recycled generator be swapped up (preserved at a new ID)
 * without using an update for a new generator node.
 *
 * \return true, or false on allocation failure
 */
static sauParseObjInfo *
sem_genalloc_swapup(ParseSem *restrict o, uint32_t obj_id) {
	sauParseObjInfo *info = &o->obj_arr.a[obj_id];
	sauParseGenData *swap_with_old = info->swap_from_gd;
	uint32_t gen_id = o->ga.count;
	sauGenAllocState *gas;
	if (!(gas = _sauGenAlloc_add(&o->ga)))
		return NULL;
	info->last_gen_id = swap_with_old->swap_to_id = gen_id;
	gas->obj_id = obj_id;
	return info;
}

/*
 * Add last duration (greatest remaining duration for a voice) to counter.
 *
 * \return duration in ms
 */
static uint32_t
sem_end_dur_ms(ParseSem *restrict o) {
	uint32_t remaining_ms = 0;
	for (size_t i = 0; i < o->va.count; ++i) {
		sauVoAllocState *vas = &o->va.a[i];
		if (vas->time_ms > remaining_ms)
			remaining_ms = vas->time_ms;
	}
	return sem_sum_dur_ms(o, remaining_ms);
}

static const sauProgramIDArr *
sem_handle_list(ParseSem *restrict o,
		const sauParseListData *restrict list_in);

/*
 * Handle generator data node (and recurse for its lists in turn),
 * listing it among those in the current event.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_handle_gendata(ParseSem *restrict o, sauParseGenData *restrict gen) {
	sauParseGenData **gen_a = _GenDataArr_add(&o->ev_gen_data);
	if (!gen_a) goto MEM_ERR;
	*gen_a = gen;
	sauParseObjInfo *info = sem_genalloc_update(o, gen);
	if (!info) goto MEM_ERR;
	const sauProgramIDArr *new_mods[SAU_MOD_NAMED - 1] = {0}; // new here
	for (sauParseListData *in_list = gen->mods;
			in_list != NULL; in_list = in_list->ref.next) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = sem_handle_list(o, in_list)))
			goto MEM_ERR;
		/*
		 * Addresses in resized arrays got here, after maybe changing.
		 */
		uint32_t vo_id = gen->event->vo_id;
		sauVoAllocState *vas = vo_id != SAU_PVO_NO_ID ?
			&o->va.a[vo_id] :
			NULL;
		const sauProgramIDArr **mods = info->mods_idarr;
		if (in_list->append) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = concat_ProgramIDArr(o->mp,
					mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == mods[type]) continue; // omit no-op
			// recycle IDs for generators made unreachable
			if (vas && !sem_vograph_handle_gen_list(o, NULL,
						true, vas, mods[type], type+1))
				goto MEM_ERR;
		}
		new_mods[type] = mods[type] = arr;
		if (vas) vas->has_new_graph = true;
	}
	return sem_set_gen_idsarr(o, gen,
			gen->is_cloned ? info->mods_idarr : new_mods);
MEM_ERR:
	return false;
}

/*
 * Loop and handle list and its contents, creating ID array for it.
 * The IDBuf is used like a stack in this function on recursion.
 *
 * \return result, or NULL on allocation failure
 */
static const sauProgramIDArr *
sem_handle_list(ParseSem *restrict o,
		const sauParseListData *restrict list_in) {
	const sauProgramIDArr *idarr = NULL;
	size_t offset = o->idbuf.count;
	if (list_in) for (sauParseObjRef *ref = list_in->first_item;
			ref; ref = ref->next) {
		if (ref->obj_type == SAU_POBJT_LIST) {
			if (!sem_handle_list(o, (void*)ref)) goto RETURN;
			continue;
		} else if (ref->obj_type != SAU_POBJT_GEN) continue;
		sauParseGenData *gen = (void*)ref;
		size_t list_max_count = o->idbuf.asize / sizeof(uint32_t);
		if (o->idbuf.count == list_max_count) {
			if (!IDBuf_upsize(&o->idbuf, list_max_count + 1024))
				goto RETURN;
		}
		if (!sem_handle_gendata(o, gen)) goto RETURN;
		o->idbuf.a[o->idbuf.count++] = gen->ref.obj_id;

	}
	idarr = clone_ProgramIDArr(o->mp,
			&o->idbuf.a[offset], o->idbuf.count - offset);
RETURN:
	o->idbuf.count = offset; // reuse allocation (zero when fully out)
	return idarr;
}

/*
 * Build -p printout graph list of generators.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_cb_genref(ParseSem *restrict o, sauGenAllocState *restrict gas,
		sauProgramGenRef *restrict gen_ref) {
	(void)gas;
	return GenRefArr_push(&o->vo_graph, gen_ref);
}

/*
 * Traverse parts of voice generator graph reached from generator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_handle_gen_node(ParseSem *restrict o, semVoGraph_cb node_cb,
		bool gens_expired, sauVoAllocState *restrict vas,
		uint32_t obj_id, sauProgramGenRef *restrict gen_ref) {
	sauParseObjInfo *info = &o->obj_arr.a[obj_id];
	if (info->last_gen_id == SAU_PGEN_NO_ID)
		sem_genalloc_swapup(o, obj_id); // realloc ID
	uint32_t gen_id = info->last_gen_id;
	sauGenAllocState *gas = &o->ga.a[gen_id];
	if (gas->is_visited) {
		sau_warning("voicegraph",
"skipping generator %u (object %d);\n\tcircular references unsupported",
			gen_id, obj_id);
		return true;
	}
	if (o->gen_nest_level > o->gen_nest_max)
		o->gen_nest_max = o->gen_nest_level;
	gas->is_visited = true;
	if (gens_expired)
		gas->is_expired = true;
	else if (vas->has_gen_expiry && gas->is_expired)
		gens_expired = true;
	++o->gen_nest_level;
	for (int i = 1; i < SAU_MOD_NAMED; ++i) {
		if (!sem_vograph_handle_gen_list(o, node_cb, gens_expired, vas,
					info->mods_idarr[i-1], i))
			return false;
		// array may have been resized by action of callback function!
		info = &o->obj_arr.a[obj_id];
	}
	--o->gen_nest_level;
	gas = &o->ga.a[gen_id]; // array may have resized/reallocated in loop!
	if (!gens_expired && node_cb) {
		if (gen_ref) gen_ref->id = gen_id;
		if (!node_cb(o, gas, gen_ref)) // may resize arrays...
			return false;
		gas = &o->ga.a[gen_id]; // array may have resized!
	}
	gas->is_visited = false;
	return true;
}

/*
 * Traverse generator graph for voice using data built
 * during allocation. Builds lists for -p printouts,
 * and fills and maintains some additional data.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_traverse(ParseSem *restrict o, sauVoAllocState *restrict vas,
		sauParseEvData *restrict ev) {
	sauProgramGenRef gen_ref = {0, SAU_MOD_N_carr, 0};
	if (!sem_vograph_handle_gen_node(o, sem_vograph_cb_genref,
				false, vas, vas->obj_id, &gen_ref))
		return false;
	if (vas->has_new_graph &&
	    !GenRefArr_mpmemdup(&o->vo_graph,
				(sauProgramGenRef**) &ev->gen_list, o->mp))
		return false;
	vas->has_new_graph = vas->has_gen_renum = vas->has_gen_expiry = false;
	ev->gen_count = o->vo_graph.count;
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

static inline void
time_line(sauLinePar *restrict line, uint32_t default_time_ms) {
	if (line->flags & SAU_LINEP_TIME_IF_NEW) { // update fallback value
		line->time_ms = default_time_ms;
		line->flags |= SAU_LINEP_TIME;
	}
}

static void
time_range(sauRange *restrict r, uint32_t default_time_ms) {
	if (!r)
		return;
	time_line(&r->a, default_time_ms);
	time_line(&r->b, default_time_ms);
	time_line(&r->e, default_time_ms);
}

static inline void
time_pdset(sauPDSet *restrict p, uint32_t default_time_ms) {
	if (!p)
		return;
	for (uint32_t i = 0; i < SAU_PPD_TYPES; ++i) {
		time_range(&p[i].v, default_time_ms);
		time_range(&p[i].f, default_time_ms);
		time_range(&p[i].p, default_time_ms);
	}
}

static void
time_gen_lines(sauParseGenData *restrict gen) {
	uint32_t dur_ms = gen->time.v_ms;
	time_range(gen->pan, dur_ms);
	time_range(gen->amp, dur_ms);
	time_range(gen->freq, dur_ms);
	time_range(gen->pm_a, dur_ms);
	time_pdset(gen->pd, dur_ms);
}

static uint32_t
time_gen(sauParseGenData *restrict gen) {
	uint32_t dur_ms = gen->time.v_ms;
	if (!(gen->params & SAU_PGENP_TIME))
		gen->event->ev_flags &= ~SAU_PEV_VOICE_SET_DUR;
	if (!(gen->time.flags & SAU_TIMEP_SET)) {
		if (gen->time.flags & SAU_TIMEP_DEFAULT)
			gen->time.flags |= SAU_TIMEP_SET; /* use, may adjust */
		else
			gen->time.flags |= SAU_TIMEP_DEFAULT;
	} else if (!gen->is_nested) {
		gen->event->ev_flags |= SAU_PEV_LOCK_DUR_SCOPE;
	}
	for (sauParseListData *list = gen->mods;
			list != NULL; list = list->ref.next) {
		for (sauParseObjRef *obj = list->first_item;
				obj; obj = obj->next) {
			if (obj->obj_type != SAU_POBJT_GEN) continue;
			sauParseGenData *sub_gen = (sauParseGenData*)obj;
			uint32_t sub_dur_ms = time_gen(sub_gen);
			if (dur_ms < sub_dur_ms
			    && (gen->time.flags & SAU_TIMEP_DEFAULT) != 0)
				dur_ms = sub_dur_ms;
		}
	}
	gen->time.v_ms = dur_ms;
	time_gen_lines(gen);
	return dur_ms;
}

static uint32_t
time_event(sauParseEvData *restrict e) {
	uint32_t dur_ms = 0;
	if (e->main_obj) {
		sauParseObjRef *obj = e->main_obj;
		if (obj->obj_type == SAU_POBJT_GEN) {
			sauParseGenData *gen = (sauParseGenData*)obj;
			dur_ms = time_gen(gen);
		}
	}
	/*
	 * Timing for sub-events - done before event list flattened.
	 */
	sauParseEvBranch *fork = e->forks;
	while (fork != NULL) {
		uint32_t nest_dur_ms = 0, wait_sum_ms = 0;
		sauParseEvData *ne = fork->events, *ne_prev = e;
		sauParseGenData *ne_gen = ne->main_obj,
				 *ne_gen_prev = ne_gen->prev_ref,
				 *e_gen = ne_gen_prev;
		uint32_t first_time_ms = e_gen->time.v_ms;
		uint32_t def_time_ms = e_gen->time.v_ms;
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SAU_PEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_PEV_VOICE_SET_DUR;
		for (;;) {
			wait_sum_ms += ne->wait_ms;
			if (!(ne_gen->time.flags & SAU_TIMEP_SET)) {
				ne_gen->time.v_ms = def_time_ms;
				if (ne->ev_flags & SAU_PEV_FROM_GAPSHIFT)
					ne_gen->time.flags |= SAU_TIMEP_SET;
			}
			time_event(ne);
			def_time_ms = ne_gen->time.v_ms;
			if (ne->ev_flags & SAU_PEV_FROM_GAPSHIFT) {
				if (ne_gen_prev->time.flags & SAU_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SAU_PEV_FROM_GAPSHIFT)) /* gap */
					ne_gen_prev->time = sauTime_VALUE(0, 0);
			}
			if (ne->ev_flags & SAU_PEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_gen_prev->time.v_ms;
				ne_gen_prev->time.flags &= ~SAU_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_gen_prev->time.flags &= ~SAU_TIMEP_DEFAULT; // fix val
			ne_gen->time.flags |= SAU_TIMEP_SET;
			ne_gen->params |= SAU_PGENP_TIME;
			ne_gen_prev = ne_gen;
			ne_prev = ne;
			ne = ne->next;
			if (!ne) break;
			ne_gen = ne->main_obj;
		}
		/*
		 * Exclude nested generators when setting a longer duration,
		 * if time has already been explicitly set for any carriers
		 * (otherwise the duration can be misreported as too long).
		 *
		 * TODO: Replace with design that gives nodes at each level
		 * their own event. Merge event and data nodes (always make
		 * new events for everything), or sublist into event nodes?
		 */
		if (!(e->ev_flags & SAU_PEV_LOCK_DUR_SCOPE)
		    || !e_gen->is_nested) {
			if (dur_ms < first_time_ms)
				dur_ms = first_time_ms;
//			if (dur_ms < nest_dur_ms)
//				dur_ms = nest_dur_ms;
		}
		fork = fork->prev;
	}
	e->dur_ms = dur_ms; /* unfinished estimate used to adjust timing */
	return dur_ms;
}

/*
 * Final time update for generator, to set flexible default time duration.
 */
static void
time_gen_tailing(sauParseGenData *restrict gen, sauParseEvData *restrict e,
		uint32_t cur_longest, uint32_t wait_sum) {
	if ((gen->time.flags & (SAU_TIMEP_SET|SAU_TIMEP_DEFAULT))
	    != SAU_TIMEP_SET) {
		gen->time.v_ms = cur_longest + wait_sum;
		gen->time.flags |= SAU_TIMEP_SET;
		if (e->dur_ms < gen->time.v_ms)
			e->dur_ms = gen->time.v_ms;
		time_gen_lines(gen);
	}
}

/*
 * Handle all voice and generator data for a parse event node.
 *
 * This is the "main" per-event semantics handling function.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_handle_event(ParseSem *restrict o, sauParseEvData *restrict e,
		uint32_t cur_longest, uint32_t wait_sum) {
	sem_voalloc_timing(o, e);
	++o->ev_count;
	sauParseObjRef *ref = e->main_obj;
	switch (ref->obj_type) {
	case SAU_POBJT_LIST:
		if (!sem_handle_list(o, (void*)ref)) goto MEM_ERR;
		return true;
	case SAU_POBJT_GEN:
		break;
	}
	sauParseGenData *gen = (void*)ref;
	time_gen_tailing(gen, e, cur_longest, wait_sum); // only for outermost
	sauVoAllocState *vas = sem_voalloc_update(o, e);
	if (!sem_handle_gendata(o, gen)) goto MEM_ERR;
	if (vas->has_new_graph || vas->has_gen_renum || vas->has_gen_expiry) {
		if (!sem_vograph_traverse(o, vas, e)) goto MEM_ERR;
	}
	if (o->ev_gen_data.count > 0) {
		if (!_GenDataArr_mpmemdup(&o->ev_gen_data,
					(sauParseGenData***) &e->gen_data,
					o->mp)) goto MEM_ERR;
		e->gen_data_count = o->ev_gen_data.count;
		o->ev_gen_data.count = 0; // reuse allocation
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Deals with events that are "sub-events" (attached to a main event as
 * nested sequence rather than part of the main linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void
flatten_events(sauParseEvData *restrict e) {
	sauParseEvBranch *fork = e->forks;
	sauParseEvData *ne = fork->events;
	sauParseEvData *fe = e->next, *fe_prev = e;
	while (ne != NULL) {
		if (!fe) {
			/*
			 * No more events in the flat sequence,
			 * so append all sub-events.
			 */
			fe_prev->next = fe = ne;
			break;
		}
		/*
		 * Insert next sub-event before or after
		 * the next events of the flat sequence.
		 */
		sauParseEvData *ne_next = ne->next;
		if (fe->wait_ms >= ne->wait_ms) {
			fe->wait_ms -= ne->wait_ms;
			fe_prev->next = ne;
			ne->next = fe;
		} else {
			ne->wait_ms -= fe->wait_ms;
			/*
			 * If several events should pass in the flat sequence
			 * before the next sub-event is inserted, skip ahead.
			 */
			while (fe->next && fe->next->wait_ms <= ne->wait_ms) {
				fe_prev = fe;
				fe = fe->next;
				ne->wait_ms -= fe->wait_ms;
			}
			sauParseEvData *fe_next = fe->next;
			fe->next = ne;
			ne->next = fe_next;
			fe = fe_next;
			if (fe)
				fe->wait_ms -= ne->wait_ms;
		}
		fe_prev = ne;
		ne = ne_next;
	}
	e->forks = fork->prev;
}

/*
 * Main semantics sem_* function, used per-durgroup. Combines timing logic
 * with other semantics (examination and allocation before audio rendering
 * interpretation) after parsing.
 *
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" generator level, so the algorithm only deals with
 * this for the events involved.
 */
static sauParseEvData *
sem_per_durgroup(ParseSem *restrict o, sauParseEvData *restrict e_from,
		uint32_t *restrict wait_after) {
	sauParseEvData *e, *e_subtract_after = e_from;
	uint32_t cur_longest = 0, wait_sum = 0, group_carry = 0;
	bool subtract = false;
	for (e = e_from; e; ) {
		if (!(e->ev_flags & SAU_PEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_PEV_VOICE_SET_DUR;
		time_event(e);
		if ((e->ev_flags & SAU_PEV_VOICE_SET_DUR) != 0 &&
		    cur_longest < e->dur_ms) {
			cur_longest = e->dur_ms;
			group_carry = cur_longest;
			e_subtract_after = e;
		}
		if (!e->next) break;
		e = e->next;
		if (cur_longest > e->wait_ms)
			cur_longest -= e->wait_ms;
		else
			cur_longest = 0;
		wait_sum += e->wait_ms;
	}
	/*
	 * Flatten event forks in loop following the timing adjustments
	 * depending on composite step event structure, complete times.
	 *
	 * Also run voice allocation, any other final bookkeeping here.
	 * This is the last event loop per durgroup, place it all here.
	 */
	for (e = e_from; e; ) {
		while (e->forks != NULL) flatten_events(e);
		sem_handle_event(o, e, cur_longest, wait_sum);
		if (!e->next) break;
		if (e == e_subtract_after) subtract = true;
		e = e->next;
		wait_sum -= e->wait_ms;
		if (subtract) {
			if (group_carry >= e->wait_ms)
				group_carry -= e->wait_ms;
			else
				group_carry = 0;
		}
	}
	if (wait_after) *wait_after += group_carry;
	return e;
}

/*
 * Check whether program can be returned for use.
 *
 * \return true, unless invalid data detected
 */
static bool
sem_check_validity(ParseSem *restrict o,
		sauParse *restrict parse) {
	bool error = false;
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			parse->name, SAU_PVO_MAX_ID);
		error = true;
	}
	if (o->obj_arr.count > SAU_POBJ_MAX_ID) {
		fprintf(stderr,
"%s: error: number of objects used cannot exceed %u\n",
			parse->name, SAU_POBJ_MAX_ID);
		error = true;
	}
	return !error;
}

static bool
init_ParseSem(ParseSem *restrict o, sauMempool *restrict mp) {
	o->mp = mp;
	return true;
}

/*
 * Fill in final data, and clean up.
 */
static sauParse *
fini_ParseSem(ParseSem *restrict o, sauParse *restrict parse) {
	bool ok;
	if ((ok = sem_check_validity(o, parse))) {
		parse->object_count = o->obj_arr.count;
		parse->ev_count = o->ev_count;
		if (isnan(parse->sopt.ampmult)) {
			/*
			 * Enable amplitude scaling (division) by voice count,
			 * handled by audio generator.
			 */
			parse->is_amp_autoscaled = true;
		} else {
			parse->is_ampmult_set = true;
		}
		parse->vo_count = o->va.count;
		parse->gen_count = o->ga.count;
		parse->gen_nest_depth = o->gen_nest_max;
		parse->duration_ms = o->tot_dur_ms;
		parse->mp = o->mp;
	}
	_sauVoAlloc_clear(&o->va);
	GenRefArr_clear(&o->vo_graph);
	_ObjInfoArr_clear(&o->obj_arr);
	IDBuf_clear(&o->obj_dead);
	_sauGenAlloc_clear(&o->ga);
	IDBuf_clear(&o->idbuf);
	_GenDataArr_clear(&o->ev_gen_data);
	IDsArr_clear(&o->ev_ids);
	return ok ? parse : NULL;
}

static void
print_linked(const char *restrict header,
		const sauProgramIDArr *restrict idarr, uint32_t *restrict map) {
	if (!idarr || !idarr->count)
		return;
	sau_printf("\n\t    %s[%u", header, map[idarr->ids[0]]);
	for (uint32_t i = 0; ++i < idarr->count; )
		sau_printf(", %u", map[idarr->ids[i]]);
	sau_printf("]");
}

static void
print_genlist(const sauProgramGenRef *restrict list,
		uint32_t count) {
	static const char *const uses[SAU_MOD_NAMED] = {
		SAU_MOD__ITEMS(SAU_MOD__X_GRAPH)
	};
	if (!list)
		return;
	FILE *out = sau_print_stream();
	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", out);
	for (;;) {
		const uint32_t indent = list[i].level * 3;
		if (indent > max_indent) max_indent = indent;
		fprintf(out, "%6u:  ", list[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', out);
		fputs(uses[list[i].use], out);
		if (++i == count) break;
		fputs("\n\t     ", out);
	}
	for (uint32_t j = max_indent; j > 0; --j)
		putc(' ', out);
	putc(']', out);
}

static sauNoinline void
print_range(const sauRange *restrict r, char c) {
	if (!r)
		return;
	const sauLinePar *line = &r->a; // currently prints only the first line
	if ((line->flags & SAU_LINEP_STATE) != 0) {
		if ((line->flags & SAU_LINEP_GOAL) != 0)
			sau_printf("\t%c=%-6.2f->%-6.2f", c, line->v0, line->vt);
		else
			sau_printf("\t%c=%-6.2f\t", c, line->v0);
	} else {
		if ((line->flags & SAU_LINEP_GOAL) != 0)
			sau_printf("\t%c->%-6.2f\t", c, line->vt);
		else
			sau_printf("\t%c", c);
	}
}

#define SAU_PGEN__X_CASE(NAME, LABELC) \
	case SAU_PGEN_N_##NAME: type = LABELC; break;

static void
print_genline(const sauParseGenData *restrict gd) {
	char type = '?';
	switch (gd->ref.gen_type) {
	SAU_PGEN__ITEMS(SAU_PGEN__X_CASE)
	}
	if (gd->swap_to_id != SAU_PGEN_NO_ID) {
		sau_printf("\n     mv op %-2u to op %-2u",
				gd->id, gd->swap_to_id);
	}
	if (gd->copy_from_id != SAU_PGEN_NO_ID) {
		sau_printf("\n    dup op %-2u to:", gd->copy_from_id);
	}
	const char *head = gd->is_new ? "\n    new\t" : "\n\t";
	sau_printf("%sop %-2u %c", head, gd->id, type);
	if (gd->params & SAU_PGENP_TIME) {
		if (gd->time.flags & SAU_TIMEP_IMPLICIT)
			sau_printf(" t=IMPL  ");
		else
			sau_printf(" t=%-6u", gd->time.v_ms);
	}
	print_range(gd->freq, 'f');
	print_range(gd->amp, 'a');
}

/**
 * Print information about program contents. Useful for debugging.
 */
void
sauParse_print_info(const sauParse *restrict o) {
	static const char *const mods_syntax[SAU_MOD_NAMED] = {
		SAU_MOD__ITEMS(SAU_MOD__X_SYNTAX)
	};
	sau_printf("Program: \"%s\"\n"
		"\tDuration:\t%u ms\n"
		"\tEvents:  \t%zu\n"
		"\tVoices:  \t%hu\n"
		"\tGenerators:\t%u\tObjects: %d\n",
		o->name,
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->gen_count, o->object_count);
	size_t ev_id = 0;
	uint32_t *obj_to_gen = calloc(o->object_count, sizeof(uint32_t));
	if (!obj_to_gen)
		return; // can't print
	for (const sauParseEvData *ev = o->events; ev; ev = ev->next) {
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (ev->gen_list != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_genlist(ev->gen_list, ev->gen_count);
		}
		for (size_t i = 0; i < ev->gen_data_count; ++i) {
			const sauParseGenData *gd = ev->gen_data[i];
			obj_to_gen[gd->ref.obj_id] = gd->id; // update lookup
		}
		for (size_t i = 0; i < ev->gen_data_count; ++i) {
			const sauParseGenData *gd = ev->gen_data[i];
			print_genline(gd);
			for (uint32_t i = 0; i < gd->mod_count; ++i) {
				const sauProgramIDs *ids = &gd->mods_idarr[i];
				print_linked(mods_syntax[ids->use],
						ids->a, obj_to_gen);
			}
		}
		sau_printf("\n");
		++ev_id;
	}
	free(obj_to_gen);
}
