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
 * Allocation of events, voices, operators.
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
	uint32_t carr_op_id;
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
	sauScriptOpData *obj = e->main_obj;
	sauScriptObjInfo *info = &info_a[(obj_id = obj->ref.obj_id)];
	sauVoAllocState *vas;
	if (obj->prev_ref) {
		info = &info_a[(obj_id = info->root_op_obj)];
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
 * Operator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct sauOpAllocState {
	const sauProgramIDArr *mods[SAU_POP_NAMED - 1];
	uint32_t flags;
} sauOpAllocState;

sauArrType(sauOpAlloc, sauOpAllocState, _)

/*
 * Update operator data for event and return an operator ID in \p op_id.
 *
 * Use the current operator if any, otherwise allocating a new one.
 * (TODO: Implement tracking of expired operators (requires look at
 * nesting and use of modulators by carriers), for reusing of IDs.)
 *
 * Only valid to call for single-operator nodes.
 *
 * \return sauScriptObjInfo, or NULL on allocation failure
 */
static sauScriptObjInfo *
sauOpAlloc_update(sauOpAlloc *restrict o,
		sauScriptObjInfo *restrict info_a,
		const sauScriptOpData *restrict od) {
	sauScriptObjInfo *info = &info_a[od->ref.obj_id];
	if (!od->prev_ref) {
		uint32_t op_id = o->count;
		sauOpAllocState *oas = _sauOpAlloc_add(o);
		if (!oas)
			return NULL;
		info->last_op_id = op_id;
		for (int i = 1; i < SAU_POP_NAMED; ++i) {
			oas->mods[i - 1] = &blank_idarr;
		}
	}
	return info;
}

/*
 * Clear operator allocator.
 */
static inline void
sauOpAlloc_clear(sauOpAlloc *restrict o) {
	_sauOpAlloc_clear(o);
}

sauArrType(sauPEvArr, sauProgramEvent, )

sauArrType(OpRefArr, sauProgramOpRef, )

/*
 * Voice data, held during program building and set per event.
 */
typedef struct sauVoiceGraph {
	OpRefArr vo_graph;
	sauVoAlloc *va;
	sauOpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
} sauVoiceGraph;

/*
 * Initialize instance for use.
 */
static inline void
sau_init_VoiceGraph(sauVoiceGraph *restrict o,
		sauVoAlloc *restrict va, sauOpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o);

static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauProgramEvent *restrict ev,
		sauMempool *restrict mp);

sauArrType(OpDataArr, sauProgramOpData, _)

typedef struct ParseConv {
	sauPEvArr ev_arr;
	sauOpAlloc oa;
	sauProgramEvent *ev;
	sauVoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	sauMempool *mp;
	sauVoAlloc va;
	uint32_t tot_dur_ms;
	sauScriptObjInfo *objects;
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
	for (sauScriptOpData *op = list_in->first_item; op; op = op->ref.next) {
		if (op->ref.obj_type != SAU_POBJT_OP) continue;
		++count;
	}
	return count;
}

static sauNoinline const sauProgramIDArr *
ParseConv_convert_list(ParseConv *restrict o,
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
	for (sauScriptOpData *op = list_in->first_item; op; op = op->ref.next) {
		if (op->ref.obj_type != SAU_POBJT_OP) continue;
		sauScriptObjInfo *info = &o->objects[op->ref.obj_id];
		idarr->ids[i++] = info->last_op_id;
	}
	return idarr;
}

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_opdata(ParseConv *restrict o,
		const sauScriptOpData *restrict op,
		uint8_t use_type,
		const sauScriptObjInfo *restrict info) {
	uint32_t op_id = info->last_op_id;
	sauOpAllocState *oas = &o->oa.a[op_id];
	sauProgramOpData *ood = _OpDataArr_push(&o->ev_op_data, NULL);
	if (!ood) goto MEM_ERR;
	ood->id = op_id;
	ood->params = op->params;
	ood->time = op->time;
	ood->pan = op->pan;
	ood->amp = op->amp;
	ood->amp2 = op->amp2;
	ood->freq = op->freq;
	ood->freq2 = op->freq2;
	ood->phase = op->phase;
	ood->use_type = use_type;
	/* TODO: separation of types */
	ood->type = info->op_type;
	ood->seed = info->seed;
	ood->wave = op->wave;
	ood->ras_opt = op->ras_opt;
	sauVoAllocState *vas = &o->va.a[o->ev->vo_id];
	for (sauScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->ref.next) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = ParseConv_convert_list(o, in_list)))
			goto MEM_ERR;
		if (in_list->append) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = concat_ProgramIDArr(o->mp,
					oas->mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == oas->mods[type]) continue; // omit no-op
		}
		oas->mods[type] = arr;
		vas->flags |= SAU_VAS_SET_GRAPH;
#define SAU_POP__X_CASE(NAME, IS_MOD, ...) \
SAU_IF(IS_MOD, case SAU_POP_N_##NAME: ood->NAME##s = oas->mods[type]; break;, )
		switch (type + 1) {
		SAU_POP__ITEMS(SAU_POP__X_CASE)
		}
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Visit each operator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * operator data.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_ops(ParseConv *restrict o,
		sauScriptListData *restrict op_list, bool link) {
	if (op_list) for (sauScriptOpData *op = op_list->first_item;
			op; op = op->ref.next) {
		if (op->ref.obj_type != SAU_POBJT_OP) continue;
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		sauScriptObjInfo *info;
		if (!(info = sauOpAlloc_update(&o->oa, o->objects, op)))
			return false;
		for (sauScriptListData *in_list = op->mods;
				in_list != NULL; in_list = in_list->ref.next) {
			if (!ParseConv_convert_ops(o, in_list, link))
				return false;
		}
		if (link &&
		    !ParseConv_convert_opdata(o, op, op_list->use_type, info))
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
sauVoiceGraph_handle_op_node(sauVoiceGraph *restrict o,
		sauProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_handle_op_list(sauVoiceGraph *restrict o,
		const sauProgramIDArr *restrict op_list, uint8_t mod_use) {
	if (!op_list)
		return true;
	sauProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!sauVoiceGraph_handle_op_node(o, &op_ref))
			return false;
	}
	return true;
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_handle_op_node(sauVoiceGraph *restrict o,
		sauProgramOpRef *restrict op_ref) {
	sauOpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SAU_OAS_VISITED) {
		sau_warning("voicegraph",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SAU_OAS_VISITED;
	for (int i = 1; i < SAU_POP_NAMED; ++i) {
		if (!sauVoiceGraph_handle_op_list(o, oas->mods[i - 1], i))
			return false;
	}
	oas->flags &= ~SAU_OAS_VISITED;
	--o->op_nest_level;
	if (!OpRefArr_push(&o->vo_graph, op_ref))
		return false;
	return true;
}

/*
 * Create operator graph for voice using data built
 * during allocation, assigning an operator reference
 * list to the voice and block IDs to the operators.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauProgramEvent *restrict ev,
		sauMempool *restrict mp) {
	sauVoAllocState *vas = &o->va->a[ev->vo_id];
	if (!(vas->flags & SAU_VAS_HAS_CARR)) goto DONE;
	sauProgramOpRef op_ref = {vas->carr_op_id, SAU_POP_N_carr, 0};
	if (!sauVoiceGraph_handle_op_node(o, &op_ref))
		return false;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(sauProgramOpRef**) &ev->op_list, mp))
		return false;
	ev->op_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/*
 * Destroy data held by instance.
 */
static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o) {
	OpRefArr_clear(&o->vo_graph);
}

/*
 * Convert all voice and operator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_event(ParseConv *restrict o,
		sauScriptEvData *restrict e) {
	sauScriptObjRef *obj = e->main_obj;
	switch (obj->obj_type) {
	case SAU_POBJT_LIST:
		if (!ParseConv_convert_ops(o, (void*)obj, false)) goto MEM_ERR;
		return true;
	case SAU_POBJT_OP:
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
	if (!ParseConv_convert_ops(o, &e_objs, true)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(sauProgramOpData**) &out_ev->op_data,
					o->mp)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (e->ev_flags & SAU_SDEV_ASSIGN_VOICE) {
		sauScriptObjInfo *info = &o->objects[obj->obj_id];
		info = &o->objects[info->root_op_obj]; // for carrier
		vas->flags |= SAU_VAS_HAS_CARR | SAU_VAS_SET_GRAPH;
		vas->carr_op_id = info->last_op_id;
	}
	out_ev->carr_op_id = vas->carr_op_id;
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
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			parse->name, SAU_POP_MAX_ID);
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
	prg->op_count = o->oa.count;
	prg->op_nest_depth = o->ev_vo_graph.op_nest_max;
	prg->duration_ms = o->tot_dur_ms;
	prg->name = parse->name;
	prg->mp = o->mp;
	prg->parse = parse;
	o->mp = NULL; // don't destroy
	return prg;
MEM_ERR:
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static sauProgram *
ParseConv_convert(ParseConv *restrict o,
		sauScript *restrict parse) {
	sauProgram *prg = NULL;
	o->mp = parse->mp;
	o->objects = parse->objects;
	sau_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	for (sauScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
	}
	if (ParseConv_check_validity(o, parse)) {
		if (!(prg = ParseConv_create_program(o, parse))) goto MEM_ERR;
	}
	if (false)
	MEM_ERR: {
		sau_error("parseconv", "memory allocation failure");
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	sauOpAlloc_clear(&o->oa);
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
print_oplist(const sauProgramOpRef *restrict list,
		uint32_t count) {
	if (!list)
		return;
	FILE *out = sau_print_stream();
	static const char *const uses[SAU_POP_NAMED] = {
		SAU_POP__ITEMS(SAU_POP__X_GRAPH)
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
print_line(const sauLine *restrict line, char c) {
	if (!line)
		return;
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

static void
print_opline(const sauProgramOpData *restrict od) {
	char type = '?';
	switch (od->type) {
	case SAU_POPT_WAVE: type = 'W'; break;
	case SAU_POPT_RAS: type = 'R'; break;
	}
	if (od->time.flags & SAU_TIMEP_IMPLICIT) {
		sau_printf("\n\top %-2u %c t=IMPL  ", od->id, type);
	} else {
		sau_printf("\n\top %-2u %c t=%-6u",
				od->id, type, od->time.v_ms);
	}
	print_line(od->freq, 'f');
	print_line(od->amp, 'a');
}

#define SAU_POP__X_PRINT(NAME, IS_MOD, LABEL, SYNTAX) \
SAU_IF(IS_MOD, print_linked(SYNTAX, od->NAME##s);, )

/**
 * Print information about program contents. Useful for debugging.
 */
void
sauProgram_print_info(const sauProgram *restrict o) {
	sau_printf("Program: \"%s\"\n"
		"\tDuration: \t%u ms\n"
		"\tEvents:   \t%zu\n"
		"\tVoices:   \t%hu\n"
		"\tOperators:\t%u\n",
		o->name,
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const sauProgramEvent *ev = &o->events[ev_id];
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (ev->op_list != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(ev->op_list, ev->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const sauProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			SAU_POP__ITEMS(SAU_POP__X_PRINT)
		}
		sau_printf("\n");
	}
}
