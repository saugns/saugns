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

#include <sau/arrtype.h>
#include <stdio.h>
#include <string.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static const sauProgramIDArr blank_idarr = {0};

static uint32_t
copy_list_ids(uint32_t *dst, const sauScriptListData *list_in) {
	uint8_t use_type = list_in->use_type;
	unsigned i = 0;
COPY:
	for (sauScriptOpData *op = list_in->first_item; op; op = op->next)
		dst[i++] = op->info->id;
	while ((list_in = list_in->next)) {
		if (list_in->use_type == use_type) {
			if (!list_in->append) i = 0;
			goto COPY;
		}
	}
	return i;
}

static sauNoinline const sauProgramIDArr *
sau_create_ProgramIDArr(sauMempool *restrict mp,
		const sauScriptListData *restrict list_in,
		const sauProgramIDArr *restrict copy) {
	uint8_t use_type = list_in->use_type;
	uint32_t count = list_in->count;
	for (sauScriptListData *next = list_in->next; next; next = next->next)
		if (next->use_type == use_type) count += next->count;
	if (!list_in->append) copy = NULL;
	if (!count)
		return copy ? copy : &blank_idarr;
	if (copy) count += copy->count;
	sauProgramIDArr *idarr = sau_mpalloc(mp,
			sizeof(sauProgramIDArr) + sizeof(uint32_t) * count);
	if (!idarr)
		return NULL;
	uint32_t i = 0;
	if (copy) {
		memcpy(idarr->ids, copy->ids, sizeof(uint32_t) * copy->count);
		i = copy->count;
	}
	uint32_t *ids = &idarr->ids[i];
	idarr->count = copy_list_ids(ids, list_in);
	return idarr;
}

/*
 * Voice allocation state flags.
 */
enum {
	SAU_VAS_GRAPH    = 1U<<0,
	SAU_VAS_HAS_CARR = 1U<<1,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct sauVoAllocState {
	sauScriptEvData *last_ev;
	uint32_t duration_ms;
	uint32_t carr_op_id;
	uint32_t flags;
} sauVoAllocState;

sauArrType(sauVoAlloc, sauVoAllocState, _)

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoAlloc_get_id(sauVoAlloc *restrict va,
		const sauScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->root_ev != NULL) {
		*vo_id = e->root_ev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		sauVoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (sauVoAllocState){0};
			*vo_id = id;
			goto ASSIGNED;
		}
	}
	*vo_id = va->count;
	if (!_sauVoAlloc_add(va, NULL))
		return false;
ASSIGNED:
	return true;
}

/*
 * Update voices for event and return a voice ID for the event.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoAlloc_update(sauVoAlloc *restrict va,
		sauScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (uint32_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	if (!sauVoAlloc_get_id(va, e, vo_id))
		return false;
	e->vo_id = *vo_id;
	sauVoAllocState *vas = &va->a[*vo_id];
	vas->last_ev = e;
	vas->flags &= ~SAU_VAS_GRAPH;
	if ((e->ev_flags & SAU_SDEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = e->dur_ms;
	return true;
}

/*
 * Clear voice allocator.
 */
static inline void
sauVoAlloc_clear(sauVoAlloc *restrict o) {
	_sauVoAlloc_clear(o);
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
	sauScriptOpData *last_pod;
	const sauProgramIDArr *amods, *ramods;
	const sauProgramIDArr *fmods, *rfmods;
	const sauProgramIDArr *pmods, *fpmods;
	uint32_t flags;
	//uint32_t duration_ms;
} sauOpAllocState;

sauArrType(sauOpAlloc, sauOpAllocState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool
sauOpAlloc_get_id(sauOpAlloc *restrict oa,
		const sauScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->prev_ref != NULL) {
		*op_id = od->info->id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[id].last_pod->op_flags & SAU_SDOP_LATER_USED)
//			&& oa->a[id].duration_ms == 0) {
//			oa->a[id] = (sauOpAllocState){0};
//			*op_id = id;
//			goto ASSIGNED;
//		}
//	}
	*op_id = oa->count;
	if (!_sauOpAlloc_add(oa, NULL))
		return false;
//ASSIGNED:
	od->info->id = *op_id;
	return true;
}

/*
 * Update operators for event and return an operator ID for the event.
 *
 * Use the current operator if any, otherwise allocating a new one.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * Only valid to call for single-operator nodes.
 *
 * \return true, or false on allocation failure
 */
static bool
sauOpAlloc_update(sauOpAlloc *restrict oa,
		sauScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	sauScriptEvData *e = od->event;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!sauOpAlloc_get_id(oa, od, op_id))
		return false;
	sauOpAllocState *oas = &oa->a[*op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static inline void
sauOpAlloc_clear(sauOpAlloc *restrict o) {
	_sauOpAlloc_clear(o);
}

sauArrType(SAU_PEvArr, sauProgramEvent, )

sauArrType(OpRefArr, sauProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct sauVoiceGraph {
	OpRefArr vo_graph;
	sauVoAlloc *va;
	sauOpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
} sauVoiceGraph;

/**
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
		const sauProgramEvent *restrict ev,
		sauMempool *restrict mp);

sauArrType(OpDataArr, sauProgramOpData, _)

typedef struct ParseConv {
	SAU_PEvArr ev_arr;
	sauVoAlloc va;
	sauOpAlloc oa;
	sauProgramEvent *ev;
	sauVoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
	sauMempool *mp;
} ParseConv;

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool
set_oplist(const sauProgramIDArr **restrict dstp,
		const sauScriptListData *restrict src,
		sauMempool *restrict mem) {
	const sauProgramIDArr *dst = sau_create_ProgramIDArr(mem, src, *dstp);
	if (!dst)
		return false;
	*dstp = dst;
	return true;
}

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_opdata(ParseConv *restrict o,
		const sauScriptOpData *restrict op, uint32_t op_id) {
	sauOpAllocState *oas = &o->oa.a[op_id];
	sauProgramOpData *ood = _OpDataArr_add(&o->ev_op_data, NULL);
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
	ood->wave = op->wave;
	/* TODO: separation of types */
	ood->type = op->info->type;
	ood->seed = op->info->seed;
	ood->ras_opt = op->ras_opt;
	sauVoAllocState *vas = &o->va.a[o->ev->vo_id];
	const sauScriptListData *mods[SAU_POP_USES] = {0};
	for (sauScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->next) {
		vas->flags |= SAU_VAS_GRAPH;
		if (!mods[in_list->use_type]) mods[in_list->use_type] = in_list;
	}
	if (mods[SAU_POP_AMOD] != NULL) {
		if (!set_oplist(&oas->amods, mods[SAU_POP_AMOD], o->mp))
			goto MEM_ERR;
		ood->amods = oas->amods;
	}
	if (mods[SAU_POP_RAMOD] != NULL) {
		if (!set_oplist(&oas->ramods, mods[SAU_POP_RAMOD], o->mp))
			goto MEM_ERR;
		ood->ramods = oas->ramods;
	}
	if (mods[SAU_POP_FMOD] != NULL) {
		if (!set_oplist(&oas->fmods, mods[SAU_POP_FMOD], o->mp))
			goto MEM_ERR;
		ood->fmods = oas->fmods;
	}
	if (mods[SAU_POP_RFMOD] != NULL) {
		if (!set_oplist(&oas->rfmods, mods[SAU_POP_RFMOD], o->mp))
			goto MEM_ERR;
		ood->rfmods = oas->rfmods;
	}
	if (mods[SAU_POP_PMOD] != NULL) {
		if (!set_oplist(&oas->pmods, mods[SAU_POP_PMOD], o->mp))
			goto MEM_ERR;
		ood->pmods = oas->pmods;
	}
	if (mods[SAU_POP_FPMOD] != NULL) {
		if (!set_oplist(&oas->fpmods, mods[SAU_POP_FPMOD], o->mp))
			goto MEM_ERR;
		ood->fpmods = oas->fpmods;
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
		sauScriptListData *restrict op_list) {
	if (op_list) for (sauScriptOpData *op = op_list->first_item;
			op; op = op->next) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id;
		if (!sauOpAlloc_update(&o->oa, op, &op_id))
			return false;
		for (sauScriptListData *in_list = op->mods;
				in_list != NULL; in_list = in_list->next) {
			if (!ParseConv_convert_ops(o, in_list))
				return false;
		}
		if (!ParseConv_convert_opdata(o, op, op_id))
			return false;
	}
	return true;
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
	if (!sauVoiceGraph_handle_op_list(o, oas->amods, SAU_POP_AMOD))
		return false;
	if (!sauVoiceGraph_handle_op_list(o, oas->ramods, SAU_POP_RAMOD))
		return false;
	if (!sauVoiceGraph_handle_op_list(o, oas->fmods, SAU_POP_FMOD))
		return false;
	if (!sauVoiceGraph_handle_op_list(o, oas->rfmods, SAU_POP_RFMOD))
		return false;
	if (!sauVoiceGraph_handle_op_list(o, oas->pmods, SAU_POP_PMOD))
		return false;
	if (!sauVoiceGraph_handle_op_list(o, oas->fpmods, SAU_POP_FPMOD))
		return false;
	oas->flags &= ~SAU_OAS_VISITED;
	--o->op_nest_level;
	if (!OpRefArr_add(&o->vo_graph, op_ref))
		return false;
	return true;
}

/**
 * Create operator graph for voice using data built
 * during allocation, assigning an operator reference
 * list to the voice and block IDs to the operators.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		const sauProgramEvent *restrict ev,
		sauMempool *restrict mp) {
	sauVoAllocState *vas = &o->va->a[ev->vo_id];
	if (!(vas->flags & SAU_VAS_HAS_CARR)) goto DONE;
	sauProgramOpRef op_ref = {vas->carr_op_id, SAU_POP_CARR, 0};
	if (!sauVoiceGraph_handle_op_node(o, &op_ref))
		return false;
	sauProgramVoData *vd = (sauProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(sauProgramOpRef**) &vd->op_list, mp))
		return false;
	vd->op_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/**
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
	uint32_t vo_id;
	if (!sauVoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	sauVoAllocState *vas = &o->va.a[vo_id];
	sauProgramEvent *out_ev = SAU_PEvArr_add(&o->ev_arr, NULL);
	if (!out_ev) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ParseConv_convert_ops(o, &e->objs)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(sauProgramOpData**) &out_ev->op_data,
					o->mp)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (!e->root_ev) {
		sauScriptOpData *op = e->objs.first_item;
		vas->flags |= SAU_VAS_GRAPH | SAU_VAS_HAS_CARR;
		vas->carr_op_id = op->info->id;
	}
	if ((vas->flags & SAU_VAS_GRAPH) != 0) {
		sauProgramVoData *ovd =
			sau_mpalloc(o->mp, sizeof(sauProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->carr_op_id = vas->carr_op_id;
		out_ev->vo_data = ovd;
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
	if (!SAU_PEvArr_mpmemdup(&o->ev_arr,
				(sauProgramEvent**) &prg->events, o->mp))
		goto MEM_ERR;
	prg->ev_count = o->ev_arr.count;
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
	prg->duration_ms = o->duration_ms;
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
	o->mp = parse->prg_mp;
	sau_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	uint32_t remaining_ms = 0;
	for (sauScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		sauVoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;
	if (ParseConv_check_validity(o, parse)) {
		prg = ParseConv_create_program(o, parse);
		if (!prg) goto MEM_ERR;
	}

	if (false)
	MEM_ERR: {
		sau_error("parseconv", "memory allocation failure");
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	sauOpAlloc_clear(&o->oa);
	sauVoAlloc_clear(&o->va);
	SAU_PEvArr_clear(&o->ev_arr);
	return prg;
}

/**
 * Create internal program for the given script data. Includes a pointer
 * to \p parse as \a parse, unless \p keep_parse is false, in which case
 * the parse is destroyed after the conversion regardless of the result.
 *
 * \return instance or NULL on error
 */
sauProgram *
sau_build_Program(sauScript *restrict parse, bool keep_parse) {
	if (!parse)
		return NULL;
	ParseConv pc = (ParseConv){0};
	sauProgram *o = ParseConv_convert(&pc, parse);
	if (!keep_parse) {
		if (o) {
			parse->prg_mp = NULL;
			o->parse = NULL;
		}
		sau_discard_Script(parse);
	}
	return o;
}

/**
 * Destroy instance. Also free parse data if held.
 */
void
sau_discard_Program(sauProgram *restrict o) {
	if (!o)
		return;
	if (o->parse && o->parse->prg_mp == o->mp) // avoid double-destroy
		o->parse->prg_mp = NULL;
	sau_discard_Script(o->parse);
	sau_destroy_Mempool(o->mp);
}

static sauNoinline void
print_linked(const char *restrict header,
		const sauProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	sau_printf("%s[%u", header, idarr->ids[0]);
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
	static const char *const uses[SAU_POP_USES] = {
		" CA",
		" AM",
		"rAM",
		" FM",
		"rFM",
		" PM",
		"fPM",
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
	case SAU_POPT_WAVE: type = 'O'; break;
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
		const sauProgramVoData *vd = ev->vo_data;
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(vd->op_list, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const sauProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    a", od->amods);
			print_linked("\n\t    a.r", od->ramods);
			print_linked("\n\t    f", od->fmods);
			print_linked("\n\t    f.r", od->rfmods);
			print_linked("\n\t    p", od->pmods);
			print_linked("\n\t    p.f", od->fpmods);
		}
		sau_printf("\n");
	}
}
