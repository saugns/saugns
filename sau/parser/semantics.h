/* SAU library: Extra semantics handling code for parser.
 * Copyright (c) 2011-2012, 2017-2026 Joel K. Pettersson
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
#include <sau/render.h>

/*
 * Semantics code running with parsing, prior to the audio rendering
 * interpretation. As a first layer of interpreting, this focuses on
 * "What does it need, what exactly does it use?". Thus, later stage
 * interpreting can handle it simply, focusing on signal processing.
 *
 * In part, this is timing logic (pre-calculating when possible). In
 * part it is 'measuring' and allocating IDs, establishing exact use
 * of resources in advance. Rendering instructions are built to fit.
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

// as srate for lines, matches 1ms resolution of script time
#define SAU_UPDATE_RATE 1000

static sauNoinline void
init_range(sauRange *restrict r, float v0, bool v0_ratio, float vt,
		unsigned user_flags) {
	sau_init_Line(&r->a, v0, v0_ratio);
	sau_init_Line(&r->b, vt, false);
	sau_init_Line(&r->e, vt, false);
	r->env.mode = SAU_ENV_FN_DEFAULT;
	r->a.user_flags = user_flags;
}

static sauRangeSet *
set_gen_valr(sauMempool *restrict mp,
		sauParseGenData *restrict gen) {
	if (!gen->valr) gen->valr = sau_mpalloc(mp, sizeof(*gen->valr));
	return gen->valr;
}

static bool sauEnvPar_has_time(sauEnvPar *restrict o) {
	if (o->flags & SAU_ENVP_R_STRETCH)
		return true;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) if (o->time_ms[i] > 0)
		return true;
	return false;
}

const float sau_pd_v_defaults[SAU_PPD_TYPES] = {
	[SAU_PPD_C] = 1.0,
	[SAU_PPD_D] = 1.0,
	[SAU_PPD_H] = 0.0,
	[SAU_PPD_X] = 0.5,
	[SAU_PPD_Y] = 0.5,
};

/*
 * Per-generator state used during program data allocation.
 * In parsed generator data, \a obj_id refers to an element of this.
 */
typedef struct SemGenObj {
	bool is_labeled   : 1; // can be reached by label, even indirectly
	bool is_visited   : 1; // temporary marking for voice traversal
	bool is_timed     : 1; // use \a time_ms to change \a is_expired?
	bool is_expired   : 1; // available for reuse
	bool has_next_ref : 1; // copied from generator node on update
	uint8_t gen_type;      // type of audio generator, if such
	uint16_t last_vo_id;   // for voice allocation (objects change voices)
	uint32_t time_ms;      // counts down to track usage scope
	uint32_t root_gen_obj; // root gen for gen
	uint32_t dst_obj_id;   // for gen allocation (copying)
	struct sauParseGenData *last_gd;
	const sauProgramIDArr *mods_idarr[SAU_MOD_NAMED - 1];
	sauRange valr[SAU_PVALR_TYPES];
} SemGenObj;
sauArrType(SemGenObjArr, SemGenObj, _)

/*
 * Per-voice state during data allocation.
 */
typedef struct sauVoAllocState {
	uint32_t obj_id;
	uint32_t time_ms;
	bool has_carrier    : 1; // has an object valid for use as carrier
	bool has_new_graph  : 1; // traverse to make updated graph in event
	bool has_gen_expiry : 1; // traverse to update generator expiry state
} sauVoAllocState;
sauArrType(sauVoAlloc, sauVoAllocState, _)

typedef struct sauPrintGenRef {
	uint32_t id;
	uint8_t use;
	uint16_t level; /* > 0 if used as a modulator */
} sauPrintGenRef;
sauArrType(PrintGenRefArr, sauPrintGenRef, )

sauArrType(GenDataArr, sauParseGenData*, _)
sauArrType(IDBuf, uint32_t, )
sauArrType(RInsArr, sauRIns, )

typedef struct ParseSem {
	sauVoAlloc va;
	PrintGenRefArr vo_graph;
	uint32_t gen_nest_level, gen_nest_max;
	uint32_t tot_dur_ms;
	uint16_t sbuf_count, max_sbuf_count; // sample value buffers needed
	bool print_info : 1;
	bool print_verbose : 1;
	SemGenObjArr gen_obj;
	sauParseGenData *ga_main_clone; // point to cloned gen during cloning
	IDBuf idbuf;
	size_t ev_count;
	GenDataArr ev_gen_data; // flat list of pointers
	RInsArr ev_ins; // per-event, all current audio rendering instructions
	sauMempool *mp;
} ParseSem;

/*
 * Advance time and state parameters in \p info for value range parameters.
 */
static void
sem_valr_advance(unsigned id, SemGenObj *info, uint32_t skip_time) {
	sauLine_skip(&info->valr[id].a, skip_time);
	sauLine_skip(&info->valr[id].b, skip_time);
	sauLine_skip(&info->valr[id].e, skip_time);
}

/*
 * Check if sem_valr_advance() caused a change that needs to be copied on.
 */
static bool
sem_valr_need_inherit(unsigned id, SemGenObj *info) {
	return	(info->valr[id].a.flags & SAU_LINEP) ||
		(info->valr[id].b.flags & SAU_LINEP) ||
		(info->valr[id].e.flags & SAU_LINEP);
}

/*
 * Copy values for a value range parameter \p id from \p info.
 */
static bool
sem_valr_inherit(ParseSem *restrict o, unsigned id,
		sauParseGenData *restrict gen, SemGenObj *info) {
	if (!set_gen_valr(o->mp, gen))
		return false;
	sauRange **valr = *gen->valr;
	if (!valr[id]) {
		valr[id] = sau_mpmemdup(o->mp, &info->valr[id],
				sizeof(sauRange));
	} else {
		sauLine_inherit(&valr[id]->a, &info->valr[id].a);
		sauLine_inherit(&valr[id]->b, &info->valr[id].b);
		sauLine_inherit(&valr[id]->e, &info->valr[id].e);
		if (!valr[id]->a.user_flags)
			valr[id]->a.user_flags = info->valr[id].a.user_flags;
	}
	/*
	 * Clear state change tracking set by sem_valr_advance()
	 * and checked by sem_valr_need_inherit().
	 */
	info->valr[id].a.flags &= ~SAU_LINEP;
	info->valr[id].b.flags &= ~SAU_LINEP;
	info->valr[id].e.flags &= ~SAU_LINEP;
	return !!valr[id];
}

/*
 * Override and update values in \p info for value range parameters.
 *
 * Also update \a pos and \a end information the other way around,
 * copying it from \p info.
 */
static void
sem_valr_update(unsigned id, sauRangeSet valr,
		SemGenObj *info) {
	if (!valr[id])
		return;
	sauLine_copy(&info->valr[id].a, &valr[id]->a, SAU_UPDATE_RATE);
	sauLine_copy(&info->valr[id].b, &valr[id]->b, SAU_UPDATE_RATE);
	sauLine_copy(&info->valr[id].e, &valr[id]->e, SAU_UPDATE_RATE);
	// copy the time values used during audio rendering stage
	valr[id]->a.pos = info->valr[id].a.pos;
	valr[id]->a.end = info->valr[id].a.end;
	valr[id]->b.pos = info->valr[id].b.pos;
	valr[id]->b.end = info->valr[id].b.end;
	valr[id]->e.pos = info->valr[id].e.pos;
	valr[id]->e.end = info->valr[id].e.end;
}

/*
 * Get a recycled generator ID if available.
 *
 * \return an ID, or SAU_POBJ_NO_ID
 */
static inline uint32_t
sem_gen_obj_recycle(ParseSem *restrict o) {
	for (uint32_t id = 0; id < o->gen_obj.count; ++id) {
		SemGenObj *old = &o->gen_obj.a[id];
		// forbid use of generators now visited in deep clone traversal
		if (old->is_expired && !old->is_visited)
			return id;
	}
	return SAU_POBJ_NO_ID;
}

/*
 * Allocate generator info; prior marked generator info objects can be reused.
 *
 * \return current array element, or NULL on allocation failure
 */
static SemGenObj *
sem_gen_obj_add(ParseSem *restrict o, sauParseObjRef *restrict ref,
		uint32_t owner_obj_id) {
	uint32_t id = sem_gen_obj_recycle(o);
	SemGenObj *info;
	if (id != SAU_POBJ_NO_ID) {
		info = &o->gen_obj.a[id];
		*info = (SemGenObj){0};
	} else {
		id = o->gen_obj.count;
		info = _SemGenObjArr_add(&o->gen_obj);
		if (!info)
			return NULL;
	}
	ref->obj_id = id;
	ref->is_new = true;
	info->is_labeled = ref->is_labeled;
	info->gen_type = ref->gen_type;
	info->last_vo_id = SAU_PVO_NO_ID;
	info->dst_obj_id = SAU_POBJ_NO_ID;
	if (owner_obj_id != SAU_POBJ_NO_ID) {
		ref->is_nested = true;
		SemGenObj *owner_info = &o->gen_obj.a[owner_obj_id];
		// modulators count as reachable from carrier
		info->is_labeled |= owner_info->is_labeled;
		info->root_gen_obj = owner_info->root_gen_obj;
	} else {
		info->root_gen_obj = id;
	}
	for (int i = 1; i < SAU_MOD_NAMED; ++i)
		info->mods_idarr[i-1] = &blank_idarr;
	if (ref->is_cloned) {
		/*
		 * Copy parts of info to be passed along directly on clone.
		 */
		const sauParseObjRef *src_ref = ref->prev_ref;
		const SemGenObj *src_info =
			&o->gen_obj.a[src_ref->obj_id];
		memcpy(info->valr, src_info->valr, sizeof(info->valr));
		return info;
	}
	sauParseGenData *gen = (void*)ref;
	const sauParseSetOptions *sopt = gen->sopt;
	init_range(&info->valr[SAU_PVALR_PAN],
			sopt->def_chanmix, false, 0.0, sopt->def_pan_law);
	init_range(&info->valr[SAU_PVALR_AMP],
			sopt->def_ampmult, false, 0.0, 0);
	init_range(&info->valr[SAU_PVALR_FREQ],
			ref->is_nested ? sopt->def_relfreq : sopt->def_freq,
			ref->is_nested, 0.0, 0);
	init_range(&info->valr[SAU_PVALR_PMA],
			0.0, false, 0.0, 0);
	for (int j = 0; j < SAU_PPD_TYPES; ++j) {
		float def = sau_pd_v_defaults[j];
		int i = sau_pd_to_valr(j);
		init_range(&info->valr[i+0], def, false, def, 0);
		init_range(&info->valr[i+1], 1.0, false, 0.0, 0);
		init_range(&info->valr[i+2], 0.0, false, 0.0, 0);
	}
	return info;
}

/*
 * Initialize object reference for use prior to assigning object info.
 */
static inline void
sem_obj_ref_init(sauParseObjRef *restrict ref,
		uint8_t obj_type, uint8_t gen_type, bool is_nested) {
	ref->obj_id = SAU_POBJ_NO_ID; // not assigned yet
	ref->obj_type = obj_type;
	ref->gen_type = gen_type;
	ref->is_new = true; // if not, \a obj_id can be copied from an old ref
	ref->is_nested = is_nested;
}

/*
 * Update object info for an object reference, filling in the ID using
 * either an old reference or a fresh info allocation.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_obj_ref_update(ParseSem *restrict o, sauParseObjRef *restrict ref,
		sauParseObjRef *restrict owner_ref) {
	if (ref->obj_id != SAU_POBJ_NO_ID)
		return true; // nothing to do
	if (!ref->is_new) {
		ref->obj_id = ref->prev_ref->obj_id;
		return true;
	}
	switch (ref->obj_type) {
	case SAU_POBJT_GEN:
		return sem_gen_obj_add(o, ref,
				owner_ref ? owner_ref->obj_id : SAU_POBJ_NO_ID);
	}
	return false;
}

static bool
sem_vograph_traverse(ParseSem *restrict o, sauVoAllocState *restrict vas);

typedef bool (*semVoGraph_cb)(ParseSem *o, uint32_t obj_id,
		sauPrintGenRef *print_ref);

static bool
sem_vograph_handle_gen_node(ParseSem *restrict o, semVoGraph_cb node_cb,
		bool gens_expired, sauVoAllocState *restrict vas,
		uint32_t obj_id, sauPrintGenRef *restrict print_ref);

/*
 * Traverse generator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static inline bool
sem_vograph_handle_gen_list(ParseSem *restrict o, semVoGraph_cb node_cb,
		bool gens_expired, sauVoAllocState *restrict vas,
		const sauProgramIDArr *restrict gen_list, uint8_t mod_use) {
	sauPrintGenRef print_ref = {0, mod_use, o->gen_nest_level};
	for (uint32_t i = 0; i < gen_list->count; ++i) {
		uint32_t obj_id = gen_list->ids[i];
		if (!sem_vograph_handle_gen_node(o, node_cb,
					gens_expired, vas, obj_id, &print_ref))
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
	e->vo_id = SAU_PVO_NO_ID;        // not assigned yet
	/*
	 * Update all generators, for generator reuse; a call
	 * to sem_gen_obj_add() reuses the result of this. As
	 * time advances per event the preparations are here.
	 *
	 * Avoid reusing IDs during split steps. These may be
	 * for generators adding modulators and/or any mod in
	 * a mod list. Object ID reuse can't be done then, as
	 * next refs exist, which need old objects to remain.
	 *
	 * To reuse things properly for zero durations, where
	 * generator times set and also the event wait times,
	 * both, are zero, the check for if a generator has a
	 * next ref is *also* needed for this case.
	 */
	for (uint32_t id = 0; id < o->gen_obj.count; ++id) {
		SemGenObj *info = &o->gen_obj.a[id];
		if (info->time_ms <= e->wait_ms)
			info->time_ms = 0;
		else
			info->time_ms -= e->wait_ms;
		// update time for generator subcomponents
		for (int i = 0; i < SAU_PVALR_TYPES; ++i)
			sem_valr_advance(i, info, e->wait_ms);
		if (info->is_labeled || info->has_next_ref) continue;
		if (!(info->time_ms == 0 && info->is_timed)) continue;
		info->is_expired = true;
		/*
		 * Set up graph traversal to mark linked nodes expired.
		 * This testing must be done before reuse of the voice.
		 */
		if (info->root_gen_obj == SAU_POBJ_NO_ID) continue; // orphaned
		SemGenObj *root_info = &o->gen_obj.a[info->root_gen_obj];
		if (root_info->last_vo_id != SAU_PVO_NO_ID) {
			sauVoAllocState *vas = &o->va.a[root_info->last_vo_id];
			vas->has_gen_expiry = true;
			if (id == info->root_gen_obj)
				vas->has_carrier = false; // voice is now bogus
		}
	}
	/*
	 * Count down remaining durations before voice reuse.
	 *
	 * For a voice ready to reuse, ensure each applicable
	 * generator (including all modulators) is marked for
	 * reuse.
	 */
	for (uint32_t id = 0; id < o->va.count; ++id) {
		sauVoAllocState *vas = &o->va.a[id];
		if (vas->time_ms <= e->wait_ms)
			vas->time_ms = 0;
		else
			vas->time_ms -= e->wait_ms;
		if (!(vas->time_ms == 0)) continue;
		if (vas->has_gen_expiry) // must call before generator reuse
			sem_vograph_traverse(o, vas);
	}
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
	if (!sem_obj_ref_update(o, &obj->ref, NULL))
		return NULL;
	SemGenObj *info = &o->gen_obj.a[obj->ref.obj_id];
	obj_id = info->root_gen_obj;
	if (obj_id == SAU_POBJ_NO_ID)
		return NULL; // orphaned node, has no voice
	info = &o->gen_obj.a[obj_id];
	sauVoAllocState *vas;
	if (obj->ref.prev_ref && info->last_vo_id != SAU_PVO_NO_ID) {
		vo_id = info->last_vo_id;
		vas = &o->va.a[vo_id];
		goto PRESERVED;
	}
	if (!obj->ref.is_nested && (obj->params & SAU_PGENP_TIME) != 0)
		has_new_graph = true; // need to assign one
	/*
	 * Reuse first lowest free voice (duration expired), if any.
	 */
	for (uint32_t id = 0; id < o->va.count; ++id) {
		vas = &o->va.a[id];
		if (vas->time_ms == 0) {
			SemGenObj *old_info = &o->gen_obj.a[vas->obj_id];
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
	if ((e->ev_flags & SAU_PEV_VOICE_SET_DUR) != 0)
		vas->time_ms = e->dur_ms;
	e->vo_id = vo_id;
	vas->has_carrier = true; // always set on voice (re)allocation
	vas->has_new_graph = has_new_graph;
	return vas;
}

static bool
sem_vograph_cb_clonegen(ParseSem *restrict o, uint32_t obj_id,
		sauPrintGenRef *restrict print_ref);

/*
 * Update generator data for event and return object info.
 *
 * Use the current generator if any, otherwise reusing an expired
 * generator if possible, or allocating a new if not.
 *
 * \return SemGenObj, or NULL on allocation failure
 */
static SemGenObj *
sem_genalloc_update(ParseSem *restrict o, sauParseGenData *restrict g,
		sauParseGenData *restrict owner_g) {
	if (!sem_obj_ref_update(o, &g->ref, &owner_g->ref))
		return NULL;
	uint32_t obj_id = g->ref.obj_id;
	SemGenObj *info = &o->gen_obj.a[obj_id];
	info->last_gd = g;
	if (g->params & SAU_PGENP_TIME) {
		info->time_ms = g->time.v_ms;
		info->is_timed = !(g->time.flags & SAU_TIMEP_IMPLICIT);
	}
	info->has_next_ref = g->ref.has_next_ref;
	g->copy_from_id = SAU_POBJ_NO_ID;
	/*
	 * Deep cloning of a modulator tree begins here. Traverse and use
	 * a callback to make the extra generators. A few details are needed
	 * to avoid trouble on recursion.
	 */
	if (g->ref.is_cloned && !o->ga_main_clone) {
		sauVoAllocState *vas = &o->va.a[g->event->vo_id];
		uint32_t src_obj_id = g->ref.prev_ref->obj_id;
		o->ga_main_clone = g; // for use by the callback function
		if (!sem_vograph_handle_gen_node(o, sem_vograph_cb_clonegen,
				false, vas, src_obj_id, NULL))
			return NULL;
		o->ga_main_clone = NULL;
		vas->has_new_graph = true;
		info = &o->gen_obj.a[obj_id]; // array may have resized!
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
		SemGenObj *src_info = &o->gen_obj.a[src->ids[j]];
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
sem_vograph_cb_clonegen(ParseSem *restrict o, uint32_t obj_id,
		sauPrintGenRef *restrict print_ref) {
	sauParseGenData *dst_gen;
	SemGenObj *dst_info, *info = &o->gen_obj.a[obj_id];
	bool is_main = !print_ref;
	if (is_main) { // detect first gen clone, from parser begin_gen()
		dst_gen = o->ga_main_clone;
		dst_info = &o->gen_obj.a[dst_gen->ref.obj_id];
	} else {
		sauParseGenData *dst_pgen = info->last_gd;
		uint32_t type = info->gen_type;
		sauParseGenData **dst_gen_a = _GenDataArr_add(&o->ev_gen_data);
		dst_gen = sau_mpalloc(o->mp, sizeof(*dst_gen));
		if (!dst_gen_a || !dst_gen)
			return false;
		*dst_gen_a = dst_gen;
		dst_gen->ref.prev_ref = &dst_pgen->ref;
		dst_gen->event = o->ga_main_clone->event;
		dst_gen->sopt = o->ga_main_clone->sopt;
		sem_obj_ref_init(&dst_gen->ref, SAU_POBJT_GEN, type, true);
		dst_gen->ref.is_cloned = true;
		if (!(dst_info = sem_genalloc_update(o, dst_gen,
				    o->ga_main_clone)))
			return false;
		info = &o->gen_obj.a[obj_id]; // array may have resized!
	}
	info->dst_obj_id = dst_gen->ref.obj_id;
	dst_gen->copy_from_id = obj_id;
	/*
	 * Clone/update modulator ID lists as well, to link to the new objects.
	 */
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		const sauProgramIDArr *src = info->mods_idarr[i], *dst;
		if (!src->count) continue;
		if (!(dst = sem_make_clone_idarr(o, src)))
			return false;
		dst_info->mods_idarr[i] = dst;
	}
	return true;
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
sem_handle_list(ParseSem *restrict o, const sauParseListData *restrict list_in,
		sauParseGenData *restrict owner_gen);

static void
gen_pardef_env(sauParseGenData *restrict gen, unsigned id,
		sauRangeSet valr, SemGenObj *info) {
	sauRange *r = valr[id];
	if (!r || !(r->e.flags & SAU_LINEP))
		return;
	const sauParseSetOptions *sopt = gen->sopt;
	// TODO: feature to only change defaults, not always apply it
	if (!(r->e.flags & SAU_LINEP_STATE) && sopt->def_parenv_v != 0.f) {
		r->e.v0 = sopt->def_parenv_v;
		r->e.flags |= SAU_LINEP_STATE;
	}
	if (!r->env.line_all_p1)
		r->env.line_all_p1 = sopt->def_parenv.line_all_p1;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) {
		if (!(r->env.time_flags & SAU_ENVP_TIME(i)))
			r->env.time_ms[i] = sopt->def_parenv.time_ms[i];
		if (!r->env.line_p1[i])
			r->env.line_p1[i] = sopt->def_parenv.line_p1[i];
	}
	r->env.time_flags |= sopt->def_parenv.time_flags;
	if (!(r->env.flags & SAU_ENVP_S))
		r->env.s_val = sopt->def_parenv.s_val;
	if (!(r->env.flags & SAU_ENVP_MODE))
		r->env.mode = sopt->def_parenv.mode;
	// TODO: if setting defaults only, need more for SAU_ENVP_R_STRETCH
	r->env.flags |= sopt->def_parenv.flags;
	/*
	 * Update copy in info...
	 */
	if (r->env.line_all_p1 > 0)
		info->valr[id].env.line_all_p1 = r->env.line_all_p1;
	for (int i = 0; i < SAU_ENV_TIMES; ++i) {
		if (r->env.time_flags & SAU_ENVP_TIME(i))
			info->valr[id].env.time_ms[i] = r->env.time_ms[i];
		if (r->env.line_p1[i] > 0)
			info->valr[id].env.line_p1[i] = r->env.line_p1[i];
	}
	if (r->env.time_flags & SAU_ENVP_TIME(SAU_ENV_TIME_R)) {
		uint8_t mask = SAU_ENVP_R_STRETCH;
		info->valr[id].env.flags &= ~mask;
		info->valr[id].env.flags |= r->env.flags & mask;
	}
	if (r->env.flags & SAU_ENVP_S)
		info->valr[id].env.s_val = r->env.s_val;
	if (r->env.flags & SAU_ENVP_MODE)
		info->valr[id].env.mode = r->env.mode;
}

/*
 * Apply set options to generator data.
 */
static void
sem_handle_gen_pardef(ParseSem *restrict o, sauParseGenData *restrict gen,
		SemGenObj *info) {
	const sauParseSetOptions *sopt = gen->sopt;
	if (gen->valr) {
		/*
		 * Use input from parser to update parameter state.
		 */
		for (int i = 0; i < SAU_PVALR_TYPES; ++i)
			gen_pardef_env(gen, i, *gen->valr, info);
		sauRange *amp = (*gen->valr)[SAU_PVALR_AMP];
		if (amp) {
			amp->a.v0 *= sopt->def_ampmult;
			amp->a.vt *= sopt->def_ampmult;
			amp->b.v0 *= sopt->def_ampmult;
			amp->b.vt *= sopt->def_ampmult;
			amp->e.v0 *= sopt->def_ampmult;
			amp->e.vt *= sopt->def_ampmult;
		}
		for (int i = 0; i < SAU_PVALR_TYPES; ++i)
			sem_valr_update(i, *gen->valr, info);
	}
	if (gen->ref.is_new && !gen->ref.is_cloned) {
		/*
		 * Fill in initial default values if missing.
		 */
		if (sopt->def_ampmult != 1.f)
			sem_valr_inherit(o, SAU_PVALR_AMP, gen, info);
		if (!gen->ref.is_nested &&
		    (sopt->def_chanmix != 0.f || sopt->def_pan_law))
			sem_valr_inherit(o, SAU_PVALR_PAN, gen, info);
		if (gen->ref.is_nested || sopt->def_freq != SAU_PDEF_FREQ)
			sem_valr_inherit(o, SAU_PVALR_FREQ, gen, info);
	}
	/*
	 * Copy tracked/timed state changes from \p info to output.
	 */
	for (int i = 0; i < SAU_PVALR_TYPES; ++i) {
		if (sem_valr_need_inherit(i, info))
			sem_valr_inherit(o, i, gen, info);
	}
	gen->sopt = NULL; // uses temporary allocation; clear after use
}

/*
 * Handle generator data node (and recurse for its lists in turn),
 * listing it among those in the current event.
 *
 * \return true, or false on allocation failure
 */
static bool
sem_handle_gendata(ParseSem *restrict o, sauParseGenData *restrict gen,
		sauParseGenData *restrict owner_gen) {
	sauParseGenData **gen_a = _GenDataArr_add(&o->ev_gen_data);
	if (!gen_a) goto MEM_ERR;
	*gen_a = gen;
	SemGenObj *info = sem_genalloc_update(o, gen, owner_gen);
	if (!info) goto MEM_ERR;
	sem_handle_gen_pardef(o, gen, info);
	for (sauParseListData *in_list = gen->mods;
			in_list != NULL; in_list = in_list->ref.next) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = sem_handle_list(o, in_list, gen)))
			goto MEM_ERR;
		/*
		 * Addresses in resized arrays got here, after maybe changing.
		 */
		info = &o->gen_obj.a[gen->ref.obj_id];
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
		mods[type] = arr;
		if (vas) vas->has_new_graph = true;
	}
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
sem_handle_list(ParseSem *restrict o, const sauParseListData *restrict list_in,
		sauParseGenData *restrict owner_gen) {
	const sauProgramIDArr *idarr = NULL;
	size_t offset = o->idbuf.count;
	if (list_in) for (sauParseObjRef *ref = list_in->first_item;
			ref; ref = ref->next) {
		if (ref->obj_type == SAU_POBJT_LIST) {
			if (!sem_handle_list(o, (void*)ref, NULL)) goto RETURN;
			continue;
		} else if (ref->obj_type != SAU_POBJT_GEN) continue;
		sauParseGenData *gen = (void*)ref;
		size_t list_max_count = o->idbuf.asize / sizeof(uint32_t);
		if (o->idbuf.count == list_max_count) {
			if (!IDBuf_upsize(&o->idbuf, list_max_count + 1024))
				goto RETURN;
		}
		if (!sem_handle_gendata(o, gen, owner_gen)) goto RETURN;
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
sem_vograph_cb_genref(ParseSem *restrict o, uint32_t obj_id,
		sauPrintGenRef *restrict print_ref) {
	(void)obj_id;
	return PrintGenRefArr_push(&o->vo_graph, print_ref);
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
		uint32_t obj_id, sauPrintGenRef *restrict print_ref) {
	SemGenObj *info = &o->gen_obj.a[obj_id];
	if (info->is_visited) {
		sau_warning("voicegraph",
"skipping generator %u; circular references unsupported", obj_id);
		return true;
	}
	if (o->gen_nest_level > o->gen_nest_max)
		o->gen_nest_max = o->gen_nest_level;
	info->is_visited = true;
	if (gens_expired) {
		info->is_expired = !(info->is_labeled || info->has_next_ref);
		info->root_gen_obj = SAU_POBJ_NO_ID; // orphaned node
	} else if (vas->has_gen_expiry && info->is_expired)
		gens_expired = true;
	++o->gen_nest_level;
	for (int i = 1; i < SAU_MOD_NAMED; ++i) {
		if (!sem_vograph_handle_gen_list(o, node_cb, gens_expired, vas,
					info->mods_idarr[i-1], i))
			return false;
		// array may have been resized by action of callback function!
		info = &o->gen_obj.a[obj_id];
	}
	--o->gen_nest_level;
	if (!gens_expired && node_cb) {
		if (print_ref) print_ref->id = obj_id;
		if (!node_cb(o, obj_id, print_ref)) // may resize arrays...
			return false;
		info = &o->gen_obj.a[obj_id]; // array may have resized!
	}
	info->is_visited = false;
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
sem_vograph_traverse(ParseSem *restrict o, sauVoAllocState *restrict vas) {
	sauPrintGenRef print_ref = {0, SAU_MOD_N_carr, 0};
	if (!sem_vograph_handle_gen_node(o, sem_vograph_cb_genref,
				false, vas, vas->obj_id, &print_ref))
		return false;
	vas->has_new_graph = vas->has_gen_expiry = false;
	return true;
}

static inline void
time_line(sauLine *restrict line, uint32_t default_time_ms) {
	unsigned mask = SAU_LINEP_GOAL | SAU_LINEP_TIME_IF_NEW;
	if ((line->flags & mask) == mask) { // update fallback value
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

static void
time_gen_lines(sauParseGenData *restrict gen) {
	if (!gen->valr)
		return;
	uint32_t dur_ms = gen->time.v_ms;
	for (int i = 0; i < SAU_PVALR_TYPES; ++i)
		time_range((*gen->valr)[i], dur_ms);
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
	} else if (!gen->ref.is_nested) {
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
				 *ne_gen_prev = (void*)ne_gen->ref.prev_ref,
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
		    || !e_gen->ref.is_nested) {
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

/**
 * Print head info about program contents.
 */
static void
sem_print_head(const char *const name) {
	sau_printf("Program: \"%s\"\n", name);
}

/**
 * Print information (duration, statistics) about program contents.
 */
static void
sem_print_stats(const sauParse *restrict o) {
	sau_printf("\tDuration:\t%u ms\n"
		"\tVoices:  \t%hu\n"
		"\tEvents:  \t%zu\tBuffers:\t%hu\n"
		"\tGenerators:\t%u\tChain length:\t%hu\n",
		o->duration_ms,
		o->vo_count,
		o->ev_count,
		o->sbuf_count,
		o->gen_count,
		o->gen_count ? o->gen_nest_depth+1 : 0);
}

static void
print_genlist(const sauPrintGenRef *restrict list,
		uint32_t count) {
	static const char *const uses[SAU_MOD_NAMED] = {
		SAU_MOD__ITEMS(SAU_MOD__X_GRAPH)
	};
	if (!list)
		return;
	FILE *out = sau_print_stream();
	int32_t i = count - 1;
	uint32_t max_indent = 0;
	fputs("\n\t    [", out);
	for (;;) {
		const uint32_t indent = list[i].level * 3;
		if (indent > max_indent) max_indent = indent;
		fprintf(out, "%6u:  ", list[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', out);
		fputs(uses[list[i].use], out);
		if (--i < 0) break;
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
	const sauLine *line = &r->a; // currently prints only the first line
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
	const char *head = gd->ref.is_new ? "\n    new\t" : "\n\t";
	if (gd->copy_from_id != SAU_POBJ_NO_ID) {
		sau_printf("\n     cp op %-2u to op %-2u",
				gd->copy_from_id, gd->ref.obj_id);
		head = "\n    dup\t";
	}
	sau_printf("%sop %-2u %c", head, gd->ref.obj_id, type);
	if (gd->params & SAU_PGENP_TIME) {
		if (gd->time.flags & SAU_TIMEP_IMPLICIT)
			sau_printf(" t=IMPL  ");
		else
			sau_printf(" t=%-6u", gd->time.v_ms);
	}
	if (!gd->valr)
		return;
	print_range((*gd->valr)[SAU_PVALR_FREQ], 'f');
	print_range((*gd->valr)[SAU_PVALR_AMP], 'a');
}

/*
 * Print first semantics representation per-event debug info for script.
 */
static void
sem_print_event(ParseSem *restrict o, const sauParseEvData *restrict ev) {
//	static const char *const mods_syntax[SAU_MOD_NAMED] = {
//		SAU_MOD__ITEMS(SAU_MOD__X_SYNTAX)
//	};
	sau_printf("/%u \tEV %zu", ev->wait_ms, o->ev_count);
	if (ev->vo_id != SAU_PVO_NO_ID)
		sau_printf(" \t(VO %hu)", ev->vo_id);
	if (ev->ins)
		sau_printf(" \t(%hu ins)", ev->ins_count);
	if (o->vo_graph.count > 0) {
		sau_printf(
			"\n\tvo %u", ev->vo_id);
		print_genlist(o->vo_graph.a, o->vo_graph.count);
	}
	for (size_t i = 0; i < ev->gen_data_count; ++i) {
		const sauParseGenData *gd = ev->gen_data[i];
		print_genline(gd);
	}
	sau_printf("\n");
}

const char *const sauRIns_names[SAU_RINS_NAMED] = {
	SAU_RINS__ITEMS(SAU_RINS__X_NAME)
};

static bool
sem_conv_mods(ParseSem *restrict o, const sauProgramIDArr *restrict mods,
		uint32_t buf_count, uint32_t freq_buf, float freq_v0,
		uint8_t mix_mode);

static sauLine *
sem_get_line(uint8_t par_id, uint8_t sub_id, SemGenObj *restrict gen) {
	sauRange *r = &gen->valr[par_id];
	switch (sub_id) {
	case SAU_RANGE_A: return &r->a;
	case SAU_RANGE_B: return &r->b;
	case SAU_RANGE_E: return &r->e;
	default: return NULL;
	}
}

#define NEED_FILL(line, mods, mulbuf) \
	(((line)->flags & SAU_LINEP_GOAL) || (mods)->count > 0 || \
	 ((mulbuf) && ((line)->flags & SAU_LINEP_STATE_RATIO)))

/*
 * Convert line fill and modulator fills for a dynamic parameter with buffer.
 * If the buffer is not filled, \p out_buf will be set to 0; only non-zero
 * ID'd buffers are supported for this role.
 *
 * If \p used_v0 is provided, it will be set to the current state with the
 * single-value multiplier applied if applicable, a value meant for further
 * use only if the buffer is not filled.
 */
static const sauLine *
sem_conv_dynpar(ParseSem *restrict o, SemGenObj *restrict gen,
		const sauProgramIDArr *restrict mods,
		uint8_t par_id, uint8_t sub_id, float *restrict used_v0,
		uint32_t buf_count, uint32_t *restrict out_buf,
		uint32_t mul_buf, float mul_v0,
		uint32_t freq_buf, float freq_v0, bool force_fill) {
	const sauLine *line = sem_get_line(par_id, sub_id, gen);
	sauRIns *ins = NULL;
	if (used_v0) *used_v0 = (line->flags & SAU_LINEP_STATE_RATIO) ?
		line->v0 * mul_v0 :
		line->v0;
	if (!NEED_FILL(line, mods, mul_buf) && !force_fill) {
		*out_buf = 0; // no buffer
		return line;
	}
	ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_run_par_line(par_id, sub_id, line,
			buf_count, mul_buf, mul_v0);
	uint32_t par_buf = buf_count++;
	sem_conv_mods(o, mods, par_buf, freq_buf, freq_v0, SAU_RMIX_LAYER);
	*out_buf = par_buf;
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
	return line;
}

static uint32_t
sem_conv_valr_mods(ParseSem *restrict o, SemGenObj *restrict gen,
		unsigned mods_from, uint8_t par_id, float *restrict a_v0,
		uint32_t buf_count, uint32_t mul_buf, float mul_v0,
		uint32_t freq_buf, float freq_v0, bool force_fill) {
	sauRIns *ins = NULL;
	const sauProgramIDArr *mods =
		gen->mods_idarr[mods_from+SAU_MOD_VALR   -1];
	const sauProgramIDArr *mods2 =
		gen->mods_idarr[mods_from+SAU_MOD_VALR2  -1];
	const sauProgramIDArr *mods_r =
		gen->mods_idarr[mods_from+SAU_MOD_VALR_r -1];
	uint32_t par_buf;
	sem_conv_dynpar(o, gen, mods, par_id,
			SAU_RANGE_A, a_v0, buf_count, &par_buf,
			mul_buf, mul_v0, freq_buf, freq_v0, force_fill);
	if (par_buf) buf_count++;
	if (mods_r->count > 0) {
		bool had_par_buf = !!par_buf;
		if (!had_par_buf) {
			par_buf = buf_count++; // always filled, used
			if (freq_buf == par_buf) {
				freq_buf = 0;    // not filled yet, is so below
				freq_v0 = *a_v0; // may need multiplier applied
			}
		}
		uint32_t par2_buf;
		float b_v0;
		sem_conv_dynpar(o, gen, mods2, par_id,
				SAU_RANGE_B, &b_v0, buf_count, &par2_buf,
				mul_buf, mul_v0, freq_buf, freq_v0, false);
		if (par2_buf) buf_count++;
		sem_conv_mods(o, mods_r, buf_count, freq_buf, freq_v0,
				SAU_RMIX_MUL_WE);
		uint32_t mod_buf = buf_count++;
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_mix_valrange(par_buf, had_par_buf, *a_v0,
				par2_buf, b_v0, mod_buf);
	} else {
		// to keep timing in sync, run mods2 despite discarding result
		sem_conv_mods(o, mods2, buf_count, 0, 1.0, 0);
	}
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
	return par_buf;
}

static uint32_t
sem_conv_valr_env(ParseSem *restrict o, SemGenObj *restrict gen,
		unsigned mods_from, uint8_t par_id, float a_v0,
		uint32_t buf_count, uint32_t mul_buf, float mul_v0,
		uint32_t freq_buf, float freq_v0, uint32_t par_buf) {
	sauEnvPar *env = &gen->valr[par_id].env;
	sauRIns *ins = NULL;
	const sauProgramIDArr *mods_e =
		gen->mods_idarr[mods_from+SAU_MOD_VALR_e -1];
	if (/*env->mode > 0 &&*/ sauEnvPar_has_time(env)) {
		bool had_par_buf = !!par_buf;
		if (!had_par_buf) {
			par_buf = buf_count++; // always filled, used
			if (freq_buf == par_buf) {
				freq_buf = 0;   // not filled yet, is so below
				freq_v0 = a_v0; // may need multiplier applied
			}
		}
		uint32_t par2_buf;
		float e_v0;
		sem_conv_dynpar(o, gen, mods_e, par_id,
				SAU_RANGE_E, &e_v0, buf_count, &par2_buf,
				mul_buf, mul_v0, freq_buf, freq_v0, false);
		if (par2_buf) buf_count++;
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_run_par_env(par_id, buf_count);
		ins = RInsArr_add(&o->ev_ins);
		uint32_t env_buf = buf_count++;
		*ins = sauRIns_mix_valrange(par_buf, had_par_buf, a_v0,
				par2_buf, e_v0, env_buf);
	} else {
		// to keep timing in sync, run mods_e despite discarding result
		sem_conv_mods(o, mods_e, buf_count, 0, 1.0, 0);
	}
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
	return par_buf;
}

static bool
sem_conv_valr(ParseSem *restrict o, SemGenObj *restrict gen,
		unsigned mods_from, uint8_t par_id, float *restrict a_v0,
		uint32_t buf_count, uint32_t mul_buf, float mul_v0,
		uint32_t freq_buf, float freq_v0, bool force_fill) {
	const sauProgramIDArr *mods_a =
		gen->mods_idarr[mods_from+SAU_MOD_VALR_a -1];
	float tmp_a_v0;
	if (!a_v0) a_v0 = &tmp_a_v0;
	uint32_t par_buf = sem_conv_valr_mods(o, gen, mods_from, par_id, a_v0,
			buf_count, mul_buf, mul_v0, freq_buf, freq_v0,
			force_fill | (mods_a->count > 0));
	if (par_buf) buf_count++;
	par_buf = sem_conv_valr_env(o, gen, mods_from, par_id, *a_v0,
			buf_count, mul_buf, mul_v0, freq_buf, freq_v0,
			par_buf);
	return sem_conv_mods(o, mods_a, buf_count, freq_buf, freq_v0,
			par_buf ? SAU_RMIX_LAYER : 0);
}

static void
sem_conv_gen_amp(ParseSem *restrict o,
		uint32_t buf_count, uint32_t out_buf) {
	sauRIns *ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_nsetf(out_buf, 1.0);
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
}

static void
sem_conv_gen_noiseg(ParseSem *restrict o,
		uint32_t buf_count, uint32_t out_buf) {
	sauRIns *ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_run_noisegen(out_buf);
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
}

static bool
sem_conv_osc_pm_fill(ParseSem *restrict o, SemGenObj *restrict gen,
		uint32_t buf_count, uint32_t freq_buf, float freq_v0) {
	const sauProgramIDArr *pm_idarr  = gen->mods_idarr[SAU_MOD_N_p_pm  -1];
	const sauProgramIDArr *fpm_idarr = gen->mods_idarr[SAU_MOD_N_pf_pm -1];
	uint8_t mix_mode = 0;
	if (fpm_idarr->count > 0) {
		sem_conv_mods(o, fpm_idarr, buf_count, freq_buf, freq_v0, 0);
		const float fpm_scale = 1.0 / SAU_HUMMID;
		sauRIns *ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_nmulf(buf_count, freq_buf,
				!freq_buf ? freq_v0*fpm_scale : fpm_scale);
		mix_mode |= SAU_RMIX_LAYER;
	}
	return sem_conv_mods(o, pm_idarr, buf_count, freq_buf, freq_v0,
			mix_mode);
}

typedef void (*ConvPDist_cb)(ParseSem *restrict o,
		uint32_t phase_buf, uint32_t cycle_buf,
		uint32_t pd_buf_id, uint8_t pd_fn_id,
		uint32_t pd_f_buf_id, float pd_f_v0,
		uint32_t pd_p_buf_id, float pd_p_v0);

static void sem_conv_pdist_cb_wosc(ParseSem *restrict o,
		uint32_t phase_buf, uint32_t cycle_buf sauMaybeUnused,
		uint32_t pd_buf_id, uint8_t pd_fn_id,
		uint32_t pd_f_buf_id, float pd_f_v0,
		uint32_t pd_p_buf_id, float pd_p_v0) {
	sauRIns *ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_run_waveosc_pdist(phase_buf,
		pd_buf_id, pd_fn_id,
		pd_f_buf_id, pd_f_v0,
		pd_p_buf_id, pd_p_v0);
}

static void sem_conv_pdist_cb_rosc(ParseSem *restrict o,
		uint32_t phase_buf, uint32_t cycle_buf,
		uint32_t pd_buf_id, uint8_t pd_fn_id,
		uint32_t pd_f_buf_id, float pd_f_v0,
		uint32_t pd_p_buf_id, float pd_p_v0) {
	sauRIns *ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_run_ralsosc_pdist(phase_buf, cycle_buf,
		pd_buf_id, pd_fn_id,
		pd_f_buf_id, pd_f_v0,
		pd_p_buf_id, pd_p_v0);
}

static bool
sem_conv_osc_phase(ParseSem *restrict o, SemGenObj *restrict gen,
		uint32_t buf_count, uint32_t phase_buf, uint32_t cycle_buf,
		uint32_t freq_buf, float freq_v0, ConvPDist_cb pdist_cb) {
	sauRIns *ins = NULL;
	bool has_pm_in = sem_conv_osc_pm_fill(o, gen, buf_count,
			freq_buf, freq_v0);
	uint32_t pm_in_buf = has_pm_in ? (buf_count++) : 0;
	ins = RInsArr_add(&o->ev_ins);
	*ins = sauRIns_run_osc_phasor(phase_buf, cycle_buf,
			freq_buf, freq_v0, pm_in_buf);
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
	/*
	 * Convert PD synthesis part.
	 */
	int pd_mods_from = SAU_MOD_N_pd_c;
	for (int j = 0; j < SAU_PPD_TYPES; ++j) {
		const float nop_value = sau_pd_v_defaults[j];
		int pd_id = sau_pd_to_valr(j);
		int pd_f_id = pd_id+1;
		int pd_p_id = pd_id+2;
		int pd_f_mods_from = pd_mods_from+SAU_MODS_VALR;
		int pd_p_mods_from = pd_f_mods_from+SAU_MODS_VALR;
		sauLine *line_pd   = sem_get_line(pd_id, SAU_RANGE_A, gen);
		float pd_f_v0, pd_p_v0;
		bool has_pd_f = sem_conv_valr(o, gen, pd_f_mods_from,
				pd_f_id, &pd_f_v0, buf_count+0, 0, 1.0,
				freq_buf, freq_v0, false);
		bool has_pd_p = sem_conv_valr(o, gen, pd_p_mods_from,
				pd_p_id, &pd_p_v0, buf_count+1, 0, 1.0,
				freq_buf, freq_v0, false);
		bool force_use = line_pd->v0 != nop_value ||
			(sau_pd_f_is_fmul(j) &&
			 (has_pd_f || pd_f_v0 != 1.f));
		if (sem_conv_valr(o, gen, pd_mods_from, pd_id, NULL,
				buf_count+2, 0, 1.0, freq_buf, freq_v0,
				force_use)) {
			pdist_cb(o, phase_buf, cycle_buf,
				buf_count+2, j,
				has_pd_f ? buf_count+0 : 0, pd_f_v0,
				has_pd_p ? buf_count+1 : 0, pd_p_v0);
		}
		pd_mods_from += (SAU_MOD_N_pd_d - SAU_MOD_N_pd_c);
	}
	return true;
}

static bool
sem_conv_osc_pma_fill(ParseSem *restrict o, SemGenObj *restrict gen,
		uint32_t buf_count, uint32_t freq_buf, float freq_v0) {
	sauLine *line = sem_get_line(SAU_PVALR_PMA, SAU_RANGE_A, gen);
	return sem_conv_valr(o, gen, SAU_MOD_N_pa_pm, SAU_PVALR_PMA,
			NULL, buf_count, 0, 1.0,
			freq_buf, freq_v0, line->v0 != 0.f);
}

static void
sem_conv_gen_wosc(ParseSem *restrict o, SemGenObj *restrict gen,
		uint32_t buf_count, uint32_t out_buf,
		uint32_t freq_buf, float freq_v0) {
	uint32_t phase_buf = out_buf; // reuse
	sem_conv_osc_phase(o, gen, buf_count, phase_buf, 0,
			freq_buf, freq_v0, sem_conv_pdist_cb_wosc);
	sauRIns *ins = NULL;
	if (sem_conv_osc_pma_fill(o, gen, buf_count, freq_buf, freq_v0)) {
		uint32_t pma_buf = buf_count++;
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_run_waveosc_selfmod(out_buf, pma_buf);
	} else {
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_run_waveosc(out_buf);
	}
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
}

static void
sem_conv_gen_rosc(ParseSem *restrict o, SemGenObj *restrict gen,
		uint32_t buf_count, uint32_t out_buf,
		uint32_t freq_buf, float freq_v0) {
	uint32_t phase_buf = out_buf; // reuse
	uint32_t cycle_buf = buf_count++;
	sem_conv_osc_phase(o, gen, buf_count, phase_buf, cycle_buf,
			freq_buf, freq_v0, sem_conv_pdist_cb_rosc);
	sauRIns *ins = NULL;
	if (sem_conv_osc_pma_fill(o, gen, buf_count, freq_buf, freq_v0)) {
		uint32_t pma_buf = buf_count++;
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_run_ralsosc_selfmod(out_buf, cycle_buf,
				pma_buf);
	} else {
		uint32_t end_a_buf = buf_count++;
		uint32_t end_b_buf = buf_count++;
		ins = RInsArr_add(&o->ev_ins);
		*ins = sauRIns_run_ralsosc(out_buf, cycle_buf,
				end_a_buf, end_b_buf);
	}
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
}

/*
 * Recurse for generator node, converting to audio rendering instructions,
 * with topolocial sorting. See sem_conv_traverse() for the main function.
 *
 * The \p parent_freq_buf is 0 if unused.
 */
static bool
sem_conv_gen(ParseSem *restrict o, uint32_t obj_id,
		uint32_t buf_count,
		uint32_t parent_freq_buf, float parent_freq_v0,
		uint8_t mix_mode) {
	SemGenObj *gen = &o->gen_obj.a[obj_id];
	if (gen->is_visited)
		return false; // guard against circular reference
	sauRIns *ins = NULL;
	uint32_t check_ins_i = o->ev_ins.count;
	if (gen->is_timed && gen->time_ms == 0)
		return false; // omit entirely
	if (!(ins = RInsArr_add(&o->ev_ins))) goto MEM_ERR;
	gen->is_visited = true;
	bool layer = mix_mode & SAU_RMIX_LAYER;
	uint32_t mix_buf  = buf_count++; // #0
	float freq_v0;
	bool has_freq = sem_conv_valr(o, gen, SAU_MOD_N_f_fm, SAU_PVALR_FREQ,
			&freq_v0, buf_count,
			parent_freq_buf, parent_freq_v0,
			buf_count, 1.0, false);
	uint32_t freq_buf = has_freq ? buf_count++ : 0; // #1 if used
	/*
	 * Sub-functions, dividing per generator type.
	 */
	uint32_t in_buf = layer ? buf_count++ : mix_buf;
	switch (gen->gen_type) {
	case SAU_PGEN_N_amp:
		sem_conv_gen_amp(o, buf_count, in_buf);
		break;
	case SAU_PGEN_N_noise:
		sem_conv_gen_noiseg(o, buf_count, in_buf);
		break;
	case SAU_PGEN_N_wave:
		sem_conv_gen_wosc(o, gen, buf_count, in_buf,
				freq_buf, freq_v0);
		break;
	case SAU_PGEN_N_rals:
		sem_conv_gen_rosc(o, gen, buf_count, in_buf,
				freq_buf, freq_v0);
		break;
	}
	float amp_v0;
	bool has_amp = sem_conv_valr(o, gen, SAU_MOD_N_a_am,
			SAU_PVALR_AMP, &amp_v0, buf_count, 0, 1.0,
			freq_buf, freq_v0, false);
	uint32_t amp_buf = has_amp ? buf_count++ : 0;
	float pan_v0;
	bool has_pan = sem_conv_valr(o, gen, SAU_MOD_N_c_am,
			SAU_PVALR_PAN, &pan_v0, buf_count, 0, 1.0,
			freq_buf, freq_v0, false);
	uint32_t pan_buf = has_pan ? buf_count++ : 0;
	if (!(ins = RInsArr_add(&o->ev_ins))) goto MEM_ERR;
	*ins = sauRIns_gen_pop_mix(mix_buf, mix_mode, in_buf,
			amp_buf, has_amp, amp_v0,
			pan_buf, has_pan, pan_v0);
	gen->is_visited = false;
	// insert time handling (checking, subtraction) if finite time used;
	// done here since a jump needs a destination, which needs ins count
	ins = RInsArr_get(&o->ev_ins, check_ins_i); // array may have resized!
	*ins = sauRIns_gen_push_jz(obj_id, o->ev_ins.count, mix_buf, mix_mode);
	if (o->sbuf_count < buf_count) o->sbuf_count = buf_count;
	return true;
MEM_ERR:
	// TODO longjmp
	return false;
}

static bool
sem_conv_mods(ParseSem *restrict o, const sauProgramIDArr *restrict mods,
		uint32_t buf_count, uint32_t freq_buf, float freq_v0,
		uint8_t mix_mode) {
	for (uint32_t i = 0; i < mods->count; ++i) {
		if (sem_conv_gen(o, mods->ids[i], buf_count,
					freq_buf, freq_v0, mix_mode))
			mix_mode |= SAU_RMIX_LAYER;
	}
	return (mix_mode & SAU_RMIX_LAYER) != 0; // true if buffer filled
}

/*
 * Convert from parse data to program for rendering audio.
 * Uses topological sort to schedule instructions.
 * Needs to be called once per input event.
 *
 * Traverses and schedules all voices every call (allowing more,
 * or easier, optimization than doing more incremental changes).
 */
static bool
sem_conv_traverse(ParseSem *restrict o, sauParseEvData *restrict e) {
	if (e->next && e->next->wait_ms == 0)
		return true; // skip, instructions would be replaced before use
	o->sbuf_count = 0; // reset
	for (size_t i = 0; i < o->va.count; ++i) {
		sauVoAllocState *vas = &o->va.a[i];
		if (!vas->has_carrier) continue;
		sem_conv_gen(o, vas->obj_id, 0, 0, 1.0, 0);
	}
	if (o->max_sbuf_count < o->sbuf_count)
		o->max_sbuf_count = o->sbuf_count;
	return true;
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
	sauParseObjRef *ref = e->main_obj;
	switch (ref->obj_type) {
	case SAU_POBJT_LIST:
		if (!sem_handle_list(o, (void*)ref, NULL)) goto MEM_ERR;
		break;
	case SAU_POBJT_GEN: {
		sauParseGenData *gen = (void*)ref;
		// time adjustment, only for outermost generator
		time_gen_tailing(gen, e, cur_longest, wait_sum);
		sauVoAllocState *vas = sem_voalloc_update(o, e);
		if (!sem_handle_gendata(o, gen, NULL)) goto MEM_ERR;
		if (vas && (vas->has_new_graph || vas->has_gen_expiry)) {
			if (!sem_vograph_traverse(o, vas)) goto MEM_ERR;
		}
		break; }
	}
	if (!sem_conv_traverse(o, e)) goto MEM_ERR;
	if (o->ev_gen_data.count > 0) {
		if (!_GenDataArr_mpmemdup(&o->ev_gen_data,
				(sauParseGenData***) &e->gen_data,
				o->mp)) goto MEM_ERR;
		e->gen_data_count = o->ev_gen_data.count;
		o->ev_gen_data.count = 0; // reuse allocation
	}
	if (o->ev_ins.count > 0) {
		if (!RInsArr_mpmemdup(&o->ev_ins,
				(sauRIns**) &e->ins,
				o->mp)) goto MEM_ERR;
		e->ins_count = o->ev_ins.count;
		o->ev_ins.count = 0; // reuse allocation
	}
	if (o->print_verbose) sem_print_event(o, e);
	// must be after printout...
	o->vo_graph.count = 0; // reuse allocation
	++o->ev_count;
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
	if (o->gen_obj.count > SAU_POBJ_MAX_ID) {
		fprintf(stderr,
"%s: error: number of generators used cannot exceed %u\n",
			parse->name, SAU_POBJ_MAX_ID);
		error = true;
	}
	return !error;
}

static bool
init_ParseSem(ParseSem *restrict o, const sauScriptArg *restrict arg,
		sauMempool *restrict mp) {
	o->print_info = arg->print_info;
	o->print_verbose = arg->print_info && arg->verbose;
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
		parse->gen_count = o->gen_obj.count;
		parse->gen_nest_depth = o->gen_nest_max;
		parse->sbuf_count = o->max_sbuf_count;
		parse->duration_ms = o->tot_dur_ms;
		parse->mp = o->mp;
		if (o->print_info) sem_print_stats(parse);
	}
	_sauVoAlloc_clear(&o->va);
	PrintGenRefArr_clear(&o->vo_graph);
	_SemGenObjArr_clear(&o->gen_obj);
	IDBuf_clear(&o->idbuf);
	_GenDataArr_clear(&o->ev_gen_data);
	RInsArr_clear(&o->ev_ins);
	return ok ? parse : NULL;
}
