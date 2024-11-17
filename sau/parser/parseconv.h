/* SAU library: Parse result to audio program converter.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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
 * Program construction from parse data.
 *
 * Allocation of events, voices, generators.
 */

static const sauProgramIDArr blank_idarr = {0};

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
		sauScriptObjInfo *restrict info_a,
		sauScriptEvData *restrict e) {
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
	sauScriptGenData *obj = e->main_obj;
	sauScriptObjInfo *info = &info_a[(obj_id = obj->ref.obj_id)];
	sauVoAllocState *vas;
	if (obj->prev_ref) {
		info = &info_a[(obj_id = info->root_gen_obj)];
		if (info->last_vo_id != SAU_PVO_NO_ID) {
			vo_id = info->last_vo_id;
			vas = &va->a[vo_id];
			goto PRESERVED;
		}
	}
	e->ev_flags |= SAU_SDEV_ASSIGN_VOICE; // now new, renumbered, or reused
	/*
	 * Reuse first lowest free voice (duration expired), if any.
	 */
	for (size_t id = 0; id < va->count; ++id) {
		vas = &va->a[id];
		if (vas->duration_ms == 0) {
			sauScriptObjInfo *old_info = &info_a[vas->obj_id];
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
	if ((e->ev_flags & SAU_SDEV_VOICE_SET_DUR) != 0)
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
 * \return sauScriptObjInfo, or NULL on allocation failure
 */
static sauScriptObjInfo *
sauGenAlloc_update(sauGenAlloc *restrict o,
		sauScriptObjInfo *restrict info_a,
		const sauScriptGenData *restrict gd) {
	sauScriptObjInfo *info = &info_a[gd->ref.obj_id];
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

sauArrType(sauPEvArr, sauProgramEvent, )

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
		sauProgramEvent *restrict ev,
		sauMempool *restrict mp);

sauArrType(GenDataArr, sauProgramGenData, _)

typedef struct ParseConv {
	sauPEvArr ev_arr;
	sauGenAlloc ga;
	sauProgramEvent *ev;
	sauVoiceGraph ev_vo_graph;
	GenDataArr ev_gen_data;
	IDsArr ev_ids;
	sauMempool *mp;
	sauVoAlloc va;
	uint32_t tot_dur_ms;
} ParseConv;

#define ParseConv_sum_dur_ms(o, add_ms) ((o)->tot_dur_ms += (add_ms))

/*
 * Add last duration (greatest remaining duration for a voice) to counter.
 *
 * \return duration in ms
 */
static uint32_t
ParseConv_end_dur_ms(ParseConv *restrict o) {
	uint32_t remaining_ms = 0;
	for (size_t i = 0; i < o->va.count; ++i) {
		sauVoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	return ParseConv_sum_dur_ms(o, remaining_ms);
}

static uint32_t
ParseConv_count_list(const sauScriptListData *restrict list_in) {
	uint32_t count = 0;
	for (sauScriptGenData *gen = list_in->first_item;
			gen; gen = gen->ref.next) {
		if (gen->ref.obj_type != SAU_POBJT_GEN) continue;
		++count;
	}
	return count;
}

static sauNoinline const sauProgramIDArr *
ParseConv_convert_list(ParseConv *restrict o,
		sauScriptObjInfo *restrict objects,
		const sauScriptListData *restrict list_in) {
	uint32_t count = ParseConv_count_list(list_in);
	if (!count)
		return &blank_idarr;
	sauProgramIDArr *idarr = sau_mpalloc(o->mp,
			sizeof(sauProgramIDArr) + sizeof(uint32_t) * count);
	if (!idarr)
		return NULL;
	idarr->count = count;
	uint32_t i = 0;
	for (sauScriptGenData *gen = list_in->first_item;
			gen; gen = gen->ref.next) {
		if (gen->ref.obj_type != SAU_POBJT_GEN) continue;
		sauScriptObjInfo *info = &objects[gen->ref.obj_id];
		idarr->ids[i++] = info->last_gen_id;
	}
	return idarr;
}

/*
 * Convert data for a generator node to program generator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_gendata(ParseConv *restrict o,
		sauScriptObjInfo *restrict objects,
		const sauScriptGenData *restrict gen,
		uint8_t use_type,
		const sauScriptObjInfo *restrict info) {
	uint32_t gen_id = info->last_gen_id;
	sauGenAllocState *gas = &o->ga.a[gen_id];
	sauProgramGenData *ogd = _GenDataArr_push(&o->ev_gen_data, NULL);
	if (!ogd) goto MEM_ERR;
	ogd->id = gen_id;
	ogd->params = gen->params;
	ogd->time = gen->time;
	ogd->amp = gen->amp;
	ogd->pan = gen->pan;
	ogd->freq = gen->freq;
	ogd->pm_a = gen->pm_a;
	ogd->pd_c = gen->pd_c;
	ogd->phase = gen->phase;
	ogd->use_type = use_type;
	/* TODO: separation of types */
	ogd->type = info->gen_type;
	ogd->seed = gen->seed;
	ogd->mode = gen->mode;
	sauVoAllocState *vas = &o->va.a[o->ev->vo_id];
	const sauProgramIDArr *mods[SAU_MOD_NAMED - 1] = {0}; // node's only
	for (sauScriptListData *in_list = gen->mods;
			in_list != NULL; in_list = in_list->ref.next) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = ParseConv_convert_list(o, objects, in_list)))
			goto MEM_ERR;
		if (in_list->append) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = concat_ProgramIDArr(o->mp,
					gas->mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == gas->mods[type]) continue; // omit no-op
		}
		mods[type] = gas->mods[type] = arr;
		vas->flags |= SAU_VAS_SET_GRAPH;
	}
	o->ev_ids.count = 0; // reuse allocation
	for (int i = 0; i < SAU_MOD_NAMED - 1; ++i) {
		sauProgramIDs *ids;
		if (!mods[i]) continue;
		if (!(ids = IDsArr_add(&o->ev_ids))) goto MEM_ERR;
		ids->a = mods[i];
		ids->use = i + 1;
	}
	ogd->mod_count = o->ev_ids.count;
	IDsArr_mpmemdup(&o->ev_ids, (sauProgramIDs**) &ogd->mods, o->mp);
	return true;
MEM_ERR:
	return false;
}

/*
 * Visit each generator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * generator data.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_gens(ParseConv *restrict o,
		sauScriptObjInfo *restrict objects,
		sauScriptListData *restrict gen_list, bool link) {
	if (gen_list) for (sauScriptGenData *gen = gen_list->first_item;
			gen; gen = gen->ref.next) {
		if (gen->ref.obj_type != SAU_POBJT_GEN) continue;
		// TODO: handle multiple generator nodes
		if ((gen->gen_flags & SAU_SDGEN_MULTIPLE) != 0) continue;
		sauScriptObjInfo *info;
		if (!(info = sauGenAlloc_update(&o->ga, objects, gen)))
			return false;
		for (sauScriptListData *in_list = gen->mods;
				in_list != NULL; in_list = in_list->ref.next) {
			if (!ParseConv_convert_gens(o, objects, in_list, link))
				return false;
		}
		if (link &&
		    !ParseConv_convert_gendata(o, objects, gen,
			    gen_list->use_type, info))
			return false;
	}
	return true;
}

/*
 * Prepare voice graph for traversal related to event.
 */
static sauVoAllocState *
sauVoiceGraph_prepare(sauVoiceGraph *restrict o,
		sauScriptObjRef *restrict obj) {
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
		sauProgramEvent *restrict ev,
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

/*
 * Convert all voice and generator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_event(ParseConv *restrict o,
		sauScriptObjInfo *restrict objects,
		sauScriptEvData *restrict e) {
	sauScriptObjRef *obj = e->main_obj;
	switch (obj->obj_type) {
	case SAU_POBJT_LIST:
		if (!ParseConv_convert_gens(o, objects, (void*)obj, false))
			goto MEM_ERR;
		return true;
	case SAU_POBJT_GEN:
		break; /* below */
	default:
		return true; /* no handling yet */
	}
	sauVoAllocState *vas = sauVoiceGraph_prepare(&o->ev_vo_graph, obj);
	sauProgramEvent *out_ev = sauPEvArr_add(&o->ev_arr);
	if (!out_ev) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = obj->vo_id;
	o->ev = out_ev;
	sauScriptListData e_objs = {0};
	e_objs.first_item = obj;
	if (!ParseConv_convert_gens(o, objects, &e_objs, true)) goto MEM_ERR;
	if (o->ev_gen_data.count > 0) {
		if (!_GenDataArr_mpmemdup(&o->ev_gen_data,
					(sauProgramGenData**) &out_ev->gen_data,
					o->mp)) goto MEM_ERR;
		out_ev->gen_data_count = o->ev_gen_data.count;
		o->ev_gen_data.count = 0; // reuse allocation
	}
	if (e->ev_flags & SAU_SDEV_ASSIGN_VOICE) {
		sauScriptObjInfo *info = &objects[obj->obj_id];
		info = &objects[info->root_gen_obj]; // for carrier
		vas->flags |= SAU_VAS_HAS_CARR | SAU_VAS_SET_GRAPH;
		vas->carr_gen_id = info->last_gen_id;
	}
	out_ev->carr_gen_id = vas->carr_gen_id;
	if ((vas->flags & SAU_VAS_SET_GRAPH) != 0) {
		if (!sauVoiceGraph_set(&o->ev_vo_graph, out_ev, o->mp))
			goto MEM_ERR;
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Check whether program can be returned for use.
 *
 * \return true, unless invalid data detected
 */
static bool
ParseConv_check_validity(ParseConv *restrict o,
		sauScript *restrict parse) {
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

static sauProgram *
ParseConv_create_program(ParseConv *restrict o,
		sauScript *restrict parse) {
	sauProgram *prg = sau_mpalloc(o->mp, sizeof(sauProgram));
	if (!prg) goto MEM_ERR;
	if (!sauPEvArr_mpmemdup(&o->ev_arr,
				(sauProgramEvent**) &prg->events, o->mp))
		goto MEM_ERR;
	prg->ev_count = o->ev_arr.count;
	prg->ampmult = parse->sopt.ampmult;
	if (!(parse->sopt.set & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = o->va.count;
	prg->gen_count = o->ga.count;
	prg->gen_nest_depth = o->ev_vo_graph.gen_nest_max;
	prg->duration_ms = o->tot_dur_ms;
	prg->name = parse->name;
	prg->mp = o->mp;
	prg->parse = parse;
	o->mp = NULL; // don't destroy
	return prg;
MEM_ERR:
	return NULL;
}

static bool
init_ParseConv(ParseConv *restrict o,
		sauMempool *restrict mp) {
	o->mp = mp;
	sau_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->ga);
	return true;
}

/*
 * Build program using the gathered data, after conversion calls.
 */
static sauProgram *
fini_ParseConv(ParseConv *restrict o,
		sauScript *restrict parse) {
	sauProgram *prg = NULL;
	if (ParseConv_check_validity(o, parse)) {
		if (!(prg = ParseConv_create_program(o, parse))) goto MEM_ERR;
	}
	if (false)
	MEM_ERR: {
		sau_error("parseconv", "memory allocation failure");
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_GenDataArr_clear(&o->ev_gen_data);
	IDsArr_clear(&o->ev_ids);
	sauGenAlloc_clear(&o->ga);
	_sauVoAlloc_clear(&o->va);
	sauPEvArr_clear(&o->ev_arr);
	return prg;
}

static sauNoinline void
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
	if (!list)
		return;
	FILE *out = sau_print_stream();
	static const char *const uses[SAU_MOD_NAMED] = {
		SAU_MOD__ITEMS(SAU_MOD__X_GRAPH)
	};

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
print_genline(const sauProgramGenData *restrict gd) {
	char type = '?';
	switch (gd->type) {
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

static const char *const mods_syntax[SAU_MOD_NAMED] = {
	SAU_MOD__ITEMS(SAU_MOD__X_SYNTAX)
};

/**
 * Print information about program contents. Useful for debugging.
 */
void
sauProgram_print_info(const sauProgram *restrict o) {
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
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const sauProgramEvent *ev = &o->events[ev_id];
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (ev->gen_list != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_genlist(ev->gen_list, ev->gen_count);
		}
		for (size_t i = 0; i < ev->gen_data_count; ++i) {
			const sauProgramGenData *gd = &ev->gen_data[i];
			print_genline(gd);
			for (uint32_t i = 0; i < gd->mod_count; ++i) {
				const sauProgramIDs *ids = &gd->mods[i];
				print_linked(mods_syntax[ids->use], ids->a);
			}
		}
		sau_printf("\n");
	}
}
