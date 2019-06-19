/* SAU library: Parse result to audio program converter.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
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

static const SAU_ProgramIDArr blank_idarr = {0};

static uint32_t
copy_list_ids(uint32_t *dst, const SAU_ScriptListData *list_in) {
	uint8_t use_type = list_in->use_type;
	unsigned i = 0;
COPY:
	for (SAU_ScriptOpData *op = list_in->first_item; op; op = op->next)
		dst[i++] = op->info->id;
	while ((list_in = list_in->next)) {
		if (list_in->use_type == use_type) {
			if (!list_in->append) i = 0;
			goto COPY;
		}
	}
	return i;
}

static sauNoinline const SAU_ProgramIDArr *
SAU_create_ProgramIDArr(SAU_Mempool *restrict mp,
		const SAU_ScriptListData *restrict list_in,
		const SAU_ProgramIDArr *restrict copy) {
	uint8_t use_type = list_in->use_type;
	uint32_t count = list_in->count;
	for (SAU_ScriptListData *next = list_in->next; next; next = next->next)
		if (next->use_type == use_type) count += next->count;
	if (!list_in->append) copy = NULL;
	if (!count)
		return copy ? copy : &blank_idarr;
	if (copy) count += copy->count;
	SAU_ProgramIDArr *idarr = SAU_mpalloc(mp,
			sizeof(SAU_ProgramIDArr) + sizeof(uint32_t) * count);
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
typedef struct SAU_VoAllocState {
	SAU_ScriptEvData *last_ev;
	uint32_t duration_ms;
	uint32_t carr_op_id;
	uint32_t flags;
} SAU_VoAllocState;

sauArrType(SAU_VoAlloc, SAU_VoAllocState, _)

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_VoAlloc_get_id(SAU_VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->root_ev != NULL) {
		*vo_id = e->root_ev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SAU_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (SAU_VoAllocState){0};
			*vo_id = id;
			goto ASSIGNED;
		}
	}
	*vo_id = va->count;
	if (!_SAU_VoAlloc_add(va, NULL))
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
SAU_VoAlloc_update(SAU_VoAlloc *restrict va,
		SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (uint32_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	if (!SAU_VoAlloc_get_id(va, e, vo_id))
		return false;
	e->vo_id = *vo_id;
	SAU_VoAllocState *vas = &va->a[*vo_id];
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
SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
	_SAU_VoAlloc_clear(o);
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
typedef struct SAU_OpAllocState {
	SAU_ScriptOpData *last_pod;
	const SAU_ProgramIDArr *amods, *ramods;
	const SAU_ProgramIDArr *fmods, *rfmods;
	const SAU_ProgramIDArr *pmods, *fpmods;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

sauArrType(SAU_OpAlloc, SAU_OpAllocState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_OpAlloc_get_id(SAU_OpAlloc *restrict oa,
		const SAU_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->prev_ref != NULL) {
		*op_id = od->info->id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[id].last_pod->op_flags & SAU_SDOP_LATER_USED)
//			&& oa->a[id].duration_ms == 0) {
//			oa->a[id] = (SAU_OpAllocState){0};
//			*op_id = id;
//			goto ASSIGNED;
//		}
//	}
	*op_id = oa->count;
	if (!_SAU_OpAlloc_add(oa, NULL))
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
SAU_OpAlloc_update(SAU_OpAlloc *restrict oa,
		SAU_ScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	SAU_ScriptEvData *e = od->event;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!SAU_OpAlloc_get_id(oa, od, op_id))
		return false;
	SAU_OpAllocState *oas = &oa->a[*op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static inline void
SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

sauArrType(SAU_PEvArr, SAU_ProgramEvent, )

sauArrType(OpRefArr, SAU_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SAU_VoiceGraph {
	OpRefArr vo_graph;
	SAU_VoAlloc *va;
	SAU_OpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
} SAU_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void
SAU_init_VoiceGraph(SAU_VoiceGraph *restrict o,
		SAU_VoAlloc *restrict va, SAU_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

static void
SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o);

static bool
SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev,
		SAU_Mempool *restrict mp);

sauArrType(OpDataArr, SAU_ProgramOpData, _)

typedef struct ParseConv {
	SAU_PEvArr ev_arr;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	SAU_VoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
	SAU_Mempool *mp;
} ParseConv;

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool
set_oplist(const SAU_ProgramIDArr **restrict dstp,
		const SAU_ScriptListData *restrict src,
		SAU_Mempool *restrict mem) {
	const SAU_ProgramIDArr *dst = SAU_create_ProgramIDArr(mem, src, *dstp);
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
		const SAU_ScriptOpData *restrict op, uint32_t op_id) {
	SAU_OpAllocState *oas = &o->oa.a[op_id];
	SAU_ProgramOpData *ood = _OpDataArr_add(&o->ev_op_data, NULL);
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
	SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	const SAU_ScriptListData *mods[SAU_POP_USES] = {0};
	for (SAU_ScriptListData *in_list = op->mods;
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
		SAU_ScriptListData *restrict op_list) {
	if (op_list) for (SAU_ScriptOpData *op = op_list->first_item;
			op; op = op->next) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id;
		if (!SAU_OpAlloc_update(&o->oa, op, &op_id))
			return false;
		for (SAU_ScriptListData *in_list = op->mods;
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
SAU_VoiceGraph_handle_op_node(SAU_VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_VoiceGraph_handle_op_list(SAU_VoiceGraph *restrict o,
		const SAU_ProgramIDArr *restrict op_list, uint8_t mod_use) {
	if (!op_list)
		return true;
	SAU_ProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!SAU_VoiceGraph_handle_op_node(o, &op_ref))
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
SAU_VoiceGraph_handle_op_node(SAU_VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref) {
	SAU_OpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SAU_OAS_VISITED) {
		SAU_warning("voicegraph",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SAU_OAS_VISITED;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->amods, SAU_POP_AMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->ramods, SAU_POP_RAMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->fmods, SAU_POP_FMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->rfmods, SAU_POP_RFMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->pmods, SAU_POP_PMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->fpmods, SAU_POP_FPMOD))
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
SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev,
		SAU_Mempool *restrict mp) {
	SAU_VoAllocState *vas = &o->va->a[ev->vo_id];
	if (!(vas->flags & SAU_VAS_HAS_CARR)) goto DONE;
	SAU_ProgramOpRef op_ref = {vas->carr_op_id, SAU_POP_CARR, 0};
	if (!SAU_VoiceGraph_handle_op_node(o, &op_ref))
		return false;
	SAU_ProgramVoData *vd = (SAU_ProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(SAU_ProgramOpRef**) &vd->op_list, mp))
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
SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o) {
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
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id;
	if (!SAU_VoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	SAU_VoAllocState *vas = &o->va.a[vo_id];
	SAU_ProgramEvent *out_ev = SAU_PEvArr_add(&o->ev_arr, NULL);
	if (!out_ev) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ParseConv_convert_ops(o, &e->objs)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(SAU_ProgramOpData**) &out_ev->op_data,
					o->mp)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (!e->root_ev) {
		SAU_ScriptOpData *op = e->objs.first_item;
		vas->flags |= SAU_VAS_GRAPH | SAU_VAS_HAS_CARR;
		vas->carr_op_id = op->info->id;
	}
	if ((vas->flags & SAU_VAS_GRAPH) != 0) {
		SAU_ProgramVoData *ovd =
			SAU_mpalloc(o->mp, sizeof(SAU_ProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->carr_op_id = vas->carr_op_id;
		out_ev->vo_data = ovd;
		if (!SAU_VoiceGraph_set(&o->ev_vo_graph, out_ev, o->mp))
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
		SAU_Script *restrict parse) {
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

static SAU_Program *
ParseConv_create_program(ParseConv *restrict o,
		SAU_Script *restrict parse) {
	SAU_Program *prg = SAU_mpalloc(o->mp, sizeof(SAU_Program));
	if (!prg) goto MEM_ERR;
	if (!SAU_PEvArr_mpmemdup(&o->ev_arr,
				(SAU_ProgramEvent**) &prg->events, o->mp))
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
static SAU_Program *
ParseConv_convert(ParseConv *restrict o,
		SAU_Script *restrict parse) {
	SAU_Program *prg = NULL;
	o->mp = parse->prg_mp;
	SAU_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	uint32_t remaining_ms = 0;
	for (SAU_ScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
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
		SAU_error("parseconv", "memory allocation failure");
	}
	SAU_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
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
SAU_Program *
SAU_build_Program(SAU_Script *restrict parse, bool keep_parse) {
	if (!parse)
		return NULL;
	ParseConv pc = (ParseConv){0};
	SAU_Program *o = ParseConv_convert(&pc, parse);
	if (!keep_parse) {
		if (o) {
			parse->prg_mp = NULL;
			o->parse = NULL;
		}
		SAU_discard_Script(parse);
	}
	return o;
}

/**
 * Destroy instance. Also free parse data if held.
 */
void
SAU_discard_Program(SAU_Program *restrict o) {
	if (!o)
		return;
	if (o->parse && o->parse->prg_mp == o->mp) // avoid double-destroy
		o->parse->prg_mp = NULL;
	SAU_discard_Script(o->parse);
	SAU_destroy_Mempool(o->mp);
}

static sauNoinline void
print_linked(const char *restrict header,
		const SAU_ProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	SAU_printf("%s[%u", header, idarr->ids[0]);
	for (uint32_t i = 0; ++i < idarr->count; )
		SAU_printf(", %u", idarr->ids[i]);
	SAU_printf("]");
}

static void
print_oplist(const SAU_ProgramOpRef *restrict list,
		uint32_t count) {
	if (!list)
		return;
	FILE *out = SAU_print_stream();
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
print_ramp(const SAU_Ramp *restrict ramp, char c) {
	if (!ramp)
		return;
	if ((ramp->flags & SAU_RAMPP_STATE) != 0) {
		if ((ramp->flags & SAU_RAMPP_GOAL) != 0)
			SAU_printf("\t%c=%-6.2f->%-6.2f", c, ramp->v0, ramp->vt);
		else
			SAU_printf("\t%c=%-6.2f\t", c, ramp->v0);
	} else {
		if ((ramp->flags & SAU_RAMPP_GOAL) != 0)
			SAU_printf("\t%c->%-6.2f\t", c, ramp->vt);
		else
			SAU_printf("\t%c", c);
	}
}

static void
print_opline(const SAU_ProgramOpData *restrict od) {
	if (od->time.flags & SAU_TIMEP_IMPLICIT) {
		SAU_printf("\n\top %u \tt=IMPL  ", od->id);
	} else {
		SAU_printf("\n\top %u \tt=%-6u", od->id, od->time.v_ms);
	}
	print_ramp(od->freq, 'f');
	print_ramp(od->amp, 'a');
}

/**
 * Print information about program contents. Useful for debugging.
 */
void
SAU_Program_print_info(const SAU_Program *restrict o) {
	SAU_printf("Program: \"%s\"\n"
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
		const SAU_ProgramEvent *ev = &o->events[ev_id];
		const SAU_ProgramVoData *vd = ev->vo_data;
		SAU_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			SAU_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(vd->op_list, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SAU_ProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    a", od->amods);
			print_linked("\n\t    a.r", od->ramods);
			print_linked("\n\t    f", od->fmods);
			print_linked("\n\t    f.r", od->rfmods);
			print_linked("\n\t    p", od->pmods);
			print_linked("\n\t    p.f", od->fpmods);
		}
		SAU_printf("\n");
	}
}
