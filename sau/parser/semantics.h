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
	return info;
}

/*
 * Voice allocation state flags.
 */
enum {
	SAU_VAS_HAS_CARR  = 1U<<0,
	SAU_VAS_SET_GRAPH = 1U<<1,
};

/*
 * Per-voice state during data allocation.
 */
typedef struct sauVoAllocState {
	uint32_t obj_id;
	uint32_t duration_ms;
	uint32_t carr_gen_id;
	uint32_t flags;
} sauVoAllocState;

sauArrType(sauVoAlloc, sauVoAllocState, _)

/*
 * Update voices for event and return state for voice.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 *
 * \return current array element, or NULL on allocation failure
 */
static sauVoAllocState *
sauVoAlloc_update(sauVoAlloc *restrict va,
		sauParseObjInfo *restrict info_a,
		sauParseEvData *restrict e) {
	uint32_t vo_id, obj_id;
	/*
	 * Count down remaining durations before voice reuse.
	 */
	for (uint32_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	/*
	 * Use voice without change if possible.
	 */
	sauParseGenData *obj = e->main_obj;
	sauParseObjInfo *info = &info_a[(obj_id = obj->ref.obj_id)];
	sauVoAllocState *vas;
	if (obj->prev_ref) {
		info = &info_a[(obj_id = info->root_gen_obj)];
		if (info->last_vo_id != SAU_PVO_NO_ID) {
			vo_id = info->last_vo_id;
			vas = &va->a[vo_id];
			goto PRESERVED;
		}
	}
	e->ev_flags |= SAU_PEV_ASSIGN_VOICE; // now new, renumbered, or reused
	/*
	 * Reuse first lowest free voice (duration expired), if any.
	 */
	for (size_t id = 0; id < va->count; ++id) {
		vas = &va->a[id];
		if (vas->duration_ms == 0) {
			sauParseObjInfo *old_info = &info_a[vas->obj_id];
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
PRESERVED:
	if ((e->ev_flags & SAU_PEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = e->dur_ms;
	obj->ref.vo_id = vo_id;
	return vas;
}

/*
 * Generator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-generator state used during program data allocation.
 */
typedef struct sauGenAllocState {
	const sauProgramIDArr *mods[SAU_MOD_NAMED - 1];
	uint32_t flags;
} sauGenAllocState;

sauArrType(sauGenAlloc, sauGenAllocState, _)

/*
 * Update generator data for event and return object info.
 *
 * Use the current generator if any, otherwise allocating a new one.
 * (TODO: Implement tracking of expired generators (requires look at
 * nesting and use of modulators by carriers), for reusing of IDs.)
 *
 * Only valid to call for single-generator nodes.
 *
 * \return sauParseObjInfo, or NULL on allocation failure
 */
static sauParseObjInfo *
sauGenAlloc_update(sauGenAlloc *restrict o,
		sauParseObjInfo *restrict info_a,
		const sauParseGenData *restrict gd) {
	sauParseObjInfo *info = &info_a[gd->ref.obj_id];
	if (!gd->prev_ref) {
		uint32_t gen_id = o->count;
		sauGenAllocState *gas = _sauGenAlloc_add(o);
		if (!gas)
			return NULL;
		info->last_gen_id = gen_id;
		for (int i = 1; i < SAU_MOD_NAMED; ++i) {
			gas->mods[i - 1] = &blank_idarr;
		}
	}
	return info;
}

/*
 * Clear generator allocator.
 */
static inline void
sauGenAlloc_clear(sauGenAlloc *restrict o) {
	_sauGenAlloc_clear(o);
}

sauArrType(IDsArr, sauProgramIDs, )
sauArrType(GenRefArr, sauProgramGenRef, )

/*
 * Voice data, held during program building and set per event.
 */
typedef struct sauVoiceGraph {
	GenRefArr vo_graph;
	sauVoAlloc *va;
	sauGenAlloc *ga;
	uint32_t gen_nest_level, gen_nest_max;
} sauVoiceGraph;

/*
 * Initialize instance for use.
 */
static inline void
sau_init_VoiceGraph(sauVoiceGraph *restrict o,
		sauVoAlloc *restrict va, sauGenAlloc *restrict ga) {
	o->va = va;
	o->ga = ga;
}

static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o);

static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauParseEvData *restrict ev,
		sauMempool *restrict mp);

sauArrType(GenDataArr, sauParseGenData*, _)
sauArrType(IDBuf, uint32_t, )

typedef struct ParseSem {
	sauGenAlloc ga;
	IDBuf idbuf;
	size_t ev_count;
	sauVoiceGraph ev_vo_graph;
	GenDataArr ev_gen_data;
	IDsArr ev_ids;
	sauMempool *mp;
	sauVoAlloc va;
	ObjInfoArr obj_arr;
	uint32_t tot_dur_ms;
} ParseSem;

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
sem_handle_gendata(ParseSem *restrict o,
		sauParseGenData *restrict gen, uint32_t *restrict gen_id) {
	sauParseGenData **gen_a = _GenDataArr_add(&o->ev_gen_data);
	if (!gen_a) goto MEM_ERR;
	*gen_a = gen;
	sauParseObjInfo *info = sauGenAlloc_update(&o->ga, o->obj_arr.a, gen);
	if (!info) goto MEM_ERR;
	*gen_id = gen->id = info->last_gen_id;
	const sauProgramIDArr *mods[SAU_MOD_NAMED - 1] = {0}; // node's only
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
		sauGenAllocState *gas = &o->ga.a[*gen_id];
		if (in_list->append) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = concat_ProgramIDArr(o->mp,
					gas->mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == gas->mods[type]) continue; // omit no-op
		}
		mods[type] = gas->mods[type] = arr;
		if (vas) vas->flags |= SAU_VAS_SET_GRAPH;
	}
	o->ev_ids.count = 0; // reuse allocation
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		sauProgramIDs *ids;
		if (!mods[i]) continue;
		if (!(ids = IDsArr_add(&o->ev_ids))) goto MEM_ERR;
		ids->a = mods[i];
		ids->use = i + 1;
	}
	gen->mods_count = o->ev_ids.count;
	IDsArr_mpmemdup(&o->ev_ids, (sauProgramIDs**) &gen->mods_idarr, o->mp);
	return true;
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
			if (!sem_handle_list(o, (void*)ref))
				goto RETURN;
			continue;
		} else if (ref->obj_type != SAU_POBJT_GEN) continue;
		sauParseGenData *gen = (void*)ref;
		size_t list_max_count = o->idbuf.asize / sizeof(uint32_t);
		if (o->idbuf.count == list_max_count) {
			if (!IDBuf_upsize(&o->idbuf, list_max_count + 1024))
				goto RETURN;
		}
		uint32_t gen_id;
		if (!sem_handle_gendata(o, gen, &gen_id))
			goto RETURN;
		o->idbuf.a[o->idbuf.count++] = gen_id;

	}
	idarr = create_ProgramIDArr(o->mp,
			&o->idbuf.a[offset], o->idbuf.count - offset);
RETURN:
	o->idbuf.count = offset; // reuse allocation (zero when fully out)
	return idarr;
}

/*
 * Prepare voice graph for traversal related to event.
 */
static sauVoAllocState *
sauVoiceGraph_prepare(sauVoiceGraph *restrict o,
		sauParseObjRef *restrict obj) {
	sauVoAllocState *vas = &o->va->a[obj->vo_id];
	vas->flags &= ~SAU_VAS_SET_GRAPH;
	return vas;
}

static bool
sauVoiceGraph_handle_gen_node(sauVoiceGraph *restrict o,
		sauProgramGenRef *restrict gen_ref);

/*
 * Traverse generator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_handle_gen_list(sauVoiceGraph *restrict o,
		const sauProgramIDArr *restrict gen_list, uint8_t mod_use) {
	if (!gen_list)
		return true;
	sauProgramGenRef gen_ref = {0, mod_use, o->gen_nest_level};
	for (uint32_t i = 0; i < gen_list->count; ++i) {
		gen_ref.id = gen_list->ids[i];
		if (!sauVoiceGraph_handle_gen_node(o, &gen_ref))
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
sauVoiceGraph_handle_gen_node(sauVoiceGraph *restrict o,
		sauProgramGenRef *restrict gen_ref) {
	sauGenAllocState *gas = &o->ga->a[gen_ref->id];
	if (gas->flags & SAU_OAS_VISITED) {
		sau_warning("voicegraph",
"skipping generator %u; circular references unsupported",
			gen_ref->id);
		return true;
	}
	if (o->gen_nest_level > o->gen_nest_max) {
		o->gen_nest_max = o->gen_nest_level;
	}
	++o->gen_nest_level;
	gas->flags |= SAU_OAS_VISITED;
	for (int i = 1; i < SAU_MOD_NAMED; ++i) {
		if (!sauVoiceGraph_handle_gen_list(o, gas->mods[i - 1], i))
			return false;
	}
	gas->flags &= ~SAU_OAS_VISITED;
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
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauParseEvData *restrict ev,
		sauMempool *restrict mp) {
	sauVoAllocState *vas = &o->va->a[ev->vo_id];
	if (!(vas->flags & SAU_VAS_HAS_CARR)) goto DONE;
	sauProgramGenRef gen_ref = {vas->carr_gen_id, SAU_MOD_N_carr, 0};
	if (!sauVoiceGraph_handle_gen_node(o, &gen_ref))
		return false;
	if (!GenRefArr_mpmemdup(&o->vo_graph,
				(sauProgramGenRef**) &ev->gen_list, mp))
		return false;
	ev->gen_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/*
 * Destroy data held by instance.
 */
static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o) {
	GenRefArr_clear(&o->vo_graph);
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
	uint32_t gen_id;
	time_gen_tailing(gen, e, cur_longest, wait_sum); // only for outermost
	sauVoAlloc_update(&o->va, o->obj_arr.a, e);
	sauVoAllocState *vas = sauVoiceGraph_prepare(&o->ev_vo_graph, ref);
	e->vo_id = ref->vo_id;
	if (!sem_handle_gendata(o, gen, &gen_id)) goto MEM_ERR;
	if (o->ev_gen_data.count > 0) {
		if (!_GenDataArr_mpmemdup(&o->ev_gen_data,
					(sauParseGenData***) &e->gen_data,
					o->mp)) goto MEM_ERR;
		e->gen_data_count = o->ev_gen_data.count;
		o->ev_gen_data.count = 0; // reuse allocation
	}
	if (e->ev_flags & SAU_PEV_ASSIGN_VOICE) {
		sauParseObjInfo *info = &o->obj_arr.a[ref->obj_id];
		info = &o->obj_arr.a[info->root_gen_obj]; // for carrier
		vas->flags |= SAU_VAS_HAS_CARR | SAU_VAS_SET_GRAPH;
		vas->carr_gen_id = info->last_gen_id;
	}
	e->carr_gen_id = vas->carr_gen_id;
	if ((vas->flags & SAU_VAS_SET_GRAPH) != 0) {
		if (!sauVoiceGraph_set(&o->ev_vo_graph, e, o->mp))
			goto MEM_ERR;
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
	if (o->ga.count > SAU_PGEN_MAX_ID) {
		fprintf(stderr,
"%s: error: number of generators used cannot exceed %u\n",
			parse->name, SAU_PGEN_MAX_ID);
		error = true;
	}
	return !error;
}

static bool
init_ParseSem(ParseSem *restrict o,
		sauMempool *restrict mp) {
	o->mp = mp;
	sau_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->ga);
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
		parse->gen_nest_depth = o->ev_vo_graph.gen_nest_max;
		parse->duration_ms = o->tot_dur_ms;
		parse->mp = o->mp;
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_GenDataArr_clear(&o->ev_gen_data);
	IDsArr_clear(&o->ev_ids);
	IDBuf_clear(&o->idbuf);
	sauGenAlloc_clear(&o->ga);
	_sauVoAlloc_clear(&o->va);
	_ObjInfoArr_clear(&o->obj_arr);
	return ok ? parse : NULL;
}

static void
print_linked(const char *restrict header,
		const sauProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	sau_printf("\n\t    %s[%u", header, idarr->ids[0]);
	for (uint32_t i = 0; ++i < idarr->count; )
		sau_printf(", %u", idarr->ids[i]);
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
	if (gd->time.flags & SAU_TIMEP_IMPLICIT) {
		sau_printf("\n\top %-2u %c t=IMPL  ", gd->id, type);
	} else {
		sau_printf("\n\top %-2u %c t=%-6u",
				gd->id, type, gd->time.v_ms);
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
		"\tGenerators:\t%u\n",
		o->name,
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->gen_count);
	size_t ev_id = 0;
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
			print_genline(gd);
			for (uint32_t i = 0; i < gd->mods_count; ++i) {
				const sauProgramIDs *ids = &gd->mods_idarr[i];
				print_linked(mods_syntax[ids->use], ids->a);
			}
		}
		sau_printf("\n");
		++ev_id;
	}
}
