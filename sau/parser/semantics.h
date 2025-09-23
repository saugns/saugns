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

static const sauProgramIDArr blank_idarr = {0};

static const sauProgramIDArr *
create_ProgramIDArr(sauMempool *restrict mp,
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

static sauParseObjInfo *
ObjInfoArr_add(ObjInfoArr *restrict o, sauParseObjRef *restrict ref,
		uint8_t obj_type, uint8_t gen_type) {
	uint32_t count = o->count;
	sauParseObjInfo *info = _ObjInfoArr_add(o);
	if (!info)
		return NULL;
	ref->obj_id = count;
	info->obj_type = ref->obj_type = obj_type;
	info->gen_type = ref->gen_type = gen_type;
	info->last_vo_id = ref->vo_id = SAU_PVO_NO_ID;
	info->last_gen_id = SAU_PGEN_NO_ID;
	for (int i = 1; i < SAU_MOD_NAMED; ++i)
		info->mods_idarr[i-1] = &blank_idarr;
	return info;
}

/*
 * Per-voice state during data allocation.
 */
typedef struct sauVoAllocState {
	uint32_t obj_id;
	uint32_t duration_ms;
	uint32_t carr_obj_id;
	bool has_new_graph : 1; // traverse to make updated graph in event
	bool has_gen_renum : 1; // traverse to update object info in array
} sauVoAllocState;

sauArrType(sauVoAlloc, sauVoAllocState, _)

/*
 * Per-generator state used during program data allocation.
 */
typedef struct sauGenAllocState {
	uint32_t obj_id;
	uint32_t kept_ms;
	bool is_visited : 1;  // for voice traversal
	bool is_timed : 1;    // use \a kept_ms to decide reuse?
	bool is_reserved : 1; // don't reuse if set (used for optim only)
} sauGenAllocState;

sauArrType(sauGenAlloc, sauGenAllocState, _)

sauArrType(IDsArr, sauProgramIDs, )
sauArrType(GenRefArr, sauProgramGenRef, )

sauArrType(GenDataArr, sauParseGenData*, _)
sauArrType(IDBuf, uint32_t, )

typedef struct ParseSem {
	sauGenAlloc ga;
	IDBuf idbuf;
	size_t ev_count;
	GenDataArr ev_gen_data; // flat list of pointers
	IDsArr ev_ids;
	sauMempool *mp;
	sauVoAlloc va;
	GenRefArr vo_graph;
	uint32_t gen_nest_level, gen_nest_max;
	ObjInfoArr obj_arr;
	uint32_t tot_dur_ms;
} ParseSem;

/*
 * Update voices for event and return state for voice.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 *
 * \return current array element, or NULL on allocation failure
 */
static sauVoAllocState *
sem_voalloc_update(ParseSem *restrict o, sauParseEvData *restrict e) {
	sauVoAlloc *va = &o->va;
	sauGenAlloc *ga = &o->ga;
	uint32_t vo_id, obj_id;
	bool has_new_graph = false;
	/*
	 * Count down remaining durations before voice reuse.
	 *
	 * Also update generators for generator reuse; when a
	 * call to sem_genalloc_update() is done, this result
	 * is used there. (Countdown is per-event after all.)
	 */
	if (e->wait_ms > 0) {
		for (uint32_t id = 0; id < va->count; ++id) {
			if (va->a[id].duration_ms < e->wait_ms)
				va->a[id].duration_ms = 0;
			else
				va->a[id].duration_ms -= e->wait_ms;
		}
		for (uint32_t id = 0; id < ga->count; ++id) {
			if (ga->a[id].kept_ms < e->wait_ms)
				ga->a[id].kept_ms = 0;
			else
				ga->a[id].kept_ms -= e->wait_ms;
		}
	}
	/*
	 * Use voice without change if possible.
	 */
	sauParseGenData *obj = e->main_obj;
	sauParseObjInfo *info = &o->obj_arr.a[(obj_id = obj->ref.obj_id)];
	sauVoAllocState *vas;
	if (obj->prev_ref) {
		info = &o->obj_arr.a[(obj_id = info->root_gen_obj)];
		if (info->last_vo_id != SAU_PVO_NO_ID) {
			vo_id = info->last_vo_id;
			vas = &va->a[vo_id];
			goto PRESERVED;
		}
	}
	has_new_graph = true; // need to assign one
	/*
	 * Reuse first lowest free voice (duration expired), if any.
	 */
	for (uint32_t id = 0; id < va->count; ++id) {
		vas = &va->a[id];
		if (vas->duration_ms == 0) {
			sauParseObjInfo *old_info = &o->obj_arr.a[vas->obj_id];
			old_info->last_vo_id = SAU_PVO_NO_ID; // renumber on use
			*vas = (sauVoAllocState){0};
			vo_id = id;
			goto RECYCLED;
		}
	}
	vo_id = va->count;
	if (!(vas = _sauVoAlloc_add(va)))
		return NULL;
RECYCLED:
	info->last_vo_id = vo_id;
	vas->obj_id = obj_id;
	vas->carr_obj_id = SAU_POBJ_NO_ID;
PRESERVED:
	if ((e->ev_flags & SAU_PEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = e->dur_ms;
	e->vo_id = obj->ref.vo_id = vo_id;
	vas->has_new_graph = has_new_graph;
	vas->has_gen_renum = false; // always clear first
	return vas;
}

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
	sauGenAlloc *ga = &o->ga;
	uint32_t obj_id, gen_id;
	sauParseGenData *swap_with_old = NULL;
	/*
	 * Use generator without change if possible.
	 */
	sauParseObjInfo *info = &o->obj_arr.a[(obj_id = g->ref.obj_id)];
	sauGenAllocState *gas;
	if (g->prev_ref) {
		if (info->last_gen_id != SAU_PGEN_NO_ID) {
			gen_id = info->last_gen_id;
			gas = &ga->a[gen_id];
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
	 * Reuse first lowest free generator, if any.
	 *
	 * On reuse, set info for reused object pointing to new user
	 * data so the latter can later be made to instruct the copy
	 * (preserving) of the old generator to a new ID, if needed.
	 */
	if (!swap_with_old) for (uint32_t id = 0; id < ga->count; ++id) {
		gas = &ga->a[id];
		if (!gas->is_reserved && gas->is_timed && gas->kept_ms == 0) {
			sauParseObjInfo *old_info = &o->obj_arr.a[gas->obj_id];
			old_info->last_gen_id = SAU_PGEN_NO_ID; // to renumber
			old_info->swap_from_gd = g; // in case it's hasty to do
			*gas = (sauGenAllocState){0};
			gen_id = id;
			goto RECYCLED;
		}
	}
	gen_id = ga->count;
	if (!(gas = _sauGenAlloc_add(ga)))
		return NULL;
RECYCLED:
	info->last_gen_id = gen_id;
	if (swap_with_old) swap_with_old->copy_to_id = gen_id;
	gas->obj_id = obj_id;
PRESERVED:
	info->last_gd = g;
	if (g->params & SAU_PGENP_TIME) {
		gas->kept_ms = g->time.v_ms;
		gas->is_timed = !(g->time.flags & SAU_TIMEP_IMPLICIT);
	}
	gas->is_reserved = g->has_next_ref; // make fewer hasty moves (optim)
	g->id = info->last_gen_id;
	g->copy_to_id = SAU_PGEN_NO_ID;
	return info;
}

#define sem_sum_dur_ms(o, add_ms) ((o)->tot_dur_ms += (add_ms))

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
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
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
		}
		new_mods[type] = mods[type] = arr;
		if (vas) vas->has_new_graph = true;
	}
	o->ev_ids.count = 0; // reuse allocation
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		sauProgramIDs *ids;
		if (!new_mods[i]) continue;
		if (!(ids = IDsArr_add(&o->ev_ids))) goto MEM_ERR;
		ids->a = new_mods[i];
		ids->use = i + 1;
	}
	gen->mods_count = o->ev_ids.count;
	IDsArr_mpmemdup(&o->ev_ids, (sauProgramIDs**) &gen->mods_idarr, o->mp);
	return true;
MEM_ERR:
	return false;
}

/*
 * Used to add an extra generator node to an event to handle follow-up changes.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_create_gendata(ParseSem *restrict o,
		sauParseEvData *restrict ev, sauParseObjInfo *restrict info) {
	sauParseGenData *pgen = info->last_gd;
	sauParseGenData **gen_a = _GenDataArr_add(&o->ev_gen_data);
	sauParseGenData *gen = sau_mpalloc(o->mp, sizeof(sauParseGenData));
	if (!gen_a || !gen)
		return false;
	*gen_a = gen;
	gen->ref = pgen->ref;
	gen->prev_ref = pgen;
	gen->event = ev;
	info = sem_genalloc_update(o, gen);
	return info;
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
	idarr = create_ProgramIDArr(o->mp,
			&o->idbuf.a[offset], o->idbuf.count - offset);
RETURN:
	o->idbuf.count = offset; // reuse allocation (zero when fully out)
	return idarr;
}

static bool
sem_vograph_handle_gen_node(ParseSem *restrict o, bool renumber_gens,
		sauParseEvData *restrict ev,
		uint32_t obj_id, sauProgramGenRef *restrict gen_ref);

/*
 * Traverse generator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_handle_gen_list(ParseSem *restrict o, bool renumber_gens,
		sauParseEvData *restrict ev,
		const sauProgramIDArr *restrict gen_list, uint8_t mod_use) {
	if (!gen_list)
		return true;
	sauProgramGenRef gen_ref = {0, mod_use, o->gen_nest_level};
	for (uint32_t i = 0; i < gen_list->count; ++i) {
		uint32_t obj_id = gen_list->ids[i];
		if (!sem_vograph_handle_gen_node(o, renumber_gens, ev,
					obj_id, &gen_ref))
			return false;
	}
	return true;
}

/*
 * Traverse parts of voice generator graph reached from generator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_handle_gen_node(ParseSem *restrict o, bool renumber_gens,
		sauParseEvData *restrict ev,
		uint32_t obj_id, sauProgramGenRef *restrict gen_ref) {
	sauParseObjInfo *info = &o->obj_arr.a[obj_id];
	if (info->last_gen_id == SAU_PGEN_NO_ID)
		sem_create_gendata(o, ev, info); // realloc ID with dummy node
	uint32_t gen_id = gen_ref->id = info->last_gen_id;
	sauGenAllocState *gas = &o->ga.a[gen_id];
	if (gas->is_visited) {
		sau_warning("voicegraph",
"skipping generator %u; circular references unsupported",
			gen_ref->id);
		return true;
	}
	if (o->gen_nest_level > o->gen_nest_max) {
		o->gen_nest_max = o->gen_nest_level;
	}
	++o->gen_nest_level;
	gas->is_visited = true;
	for (int i = 1; i < SAU_MOD_NAMED; ++i) {
		if (!sem_vograph_handle_gen_list(o, renumber_gens, ev,
					info->mods_idarr[i-1], i))
			return false;
	}
	gas = &o->ga.a[gen_id]; // array may have resized/reallocated in loop!
	gas->is_visited = false;
	--o->gen_nest_level;
	if (!GenRefArr_push(&o->vo_graph, gen_ref))
		return false;
	return true;
}

/*
 * Create generator graph for voice using data built
 * during allocation, assigning a generator reference
 * list to the voice and block IDs to the generators.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_vograph_set(ParseSem *restrict o,
		sauParseEvData *restrict ev, uint32_t obj_id) {
	sauVoAllocState *vas = &o->va.a[ev->vo_id];
	sauParseObjInfo *info = &o->obj_arr.a[obj_id];
	vas->carr_obj_id = info->root_gen_obj;
	if (vas->carr_obj_id == SAU_POBJ_NO_ID) goto DONE;
	sauProgramGenRef gen_ref = {0, SAU_MOD_N_carr, 0};
	if (!sem_vograph_handle_gen_node(o, vas->has_gen_renum, ev,
				vas->carr_obj_id, &gen_ref))
		return false;
	if (vas->has_new_graph &&
	    !GenRefArr_mpmemdup(&o->vo_graph,
				(sauProgramGenRef**) &ev->gen_list, o->mp))
		return false;
	ev->gen_count = o->vo_graph.count;
DONE:
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
	sem_sum_dur_ms(o, e->wait_ms);
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
	if (vas->has_new_graph || vas->has_gen_renum) {
		if (!sem_vograph_set(o, e, ref->obj_id)) goto MEM_ERR;
	}
	e->carr_obj_id = vas->carr_obj_id;
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
	if ((ok = sem_check_validity(o, parse) &&
	    _ObjInfoArr_mpmemdup(&o->obj_arr, &parse->objects, o->mp))) {
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
	GenRefArr_clear(&o->vo_graph);
	_GenDataArr_clear(&o->ev_gen_data);
	IDsArr_clear(&o->ev_ids);
	IDBuf_clear(&o->idbuf);
	_sauGenAlloc_clear(&o->ga);
	_sauVoAlloc_clear(&o->va);
	_ObjInfoArr_clear(&o->obj_arr);
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
	if (gd->copy_to_id != SAU_PGEN_NO_ID) {
		sau_printf("\n     mv op %-2u to op %-2u",
				gd->id, gd->copy_to_id);
	}
	bool reset = !gd->prev_ref;
	const char *head = reset ? "\n    new\t" : "\n\t";
	sau_printf("%sop %-2u %c", head, gd->id, type);
	if (gd->time.flags & SAU_TIMEP_SET) {
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
			for (uint32_t i = 0; i < gd->mods_count; ++i) {
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
