/* sgensys: Parse result to audio program converter.
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

#include "../arrtype.h"
#include <stdio.h>
#include <string.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static const SGS_ProgramIDArr blank_idarr = {0};

static void copy_list_ids(uint32_t **dst, const SGS_ScriptListData *list_in) {
	if (list_in->append && list_in->prev)
		copy_list_ids(dst, list_in->prev);
	for (SGS_ScriptOpData *op = list_in->first_on; op; op = op->next)
		*(*dst)++ = op->op_id;
}

static sgsNoinline const SGS_ProgramIDArr *
SGS_create_ProgramIDArr(SGS_Mempool *restrict mp,
		const SGS_ScriptListData *restrict list_in,
		const SGS_ProgramIDArr *restrict copy) {
	uint32_t count = list_in->count;
	if (!list_in->append) copy = NULL;
	if (!count)
		return copy ? copy : &blank_idarr;
	if (copy) count += copy->count;
	SGS_ProgramIDArr *idarr = SGS_mpalloc(mp,
			sizeof(SGS_ProgramIDArr) + sizeof(uint32_t) * count);
	if (!idarr)
		return NULL;
	idarr->count = count;
	uint32_t i = 0;
	if (copy) {
		memcpy(idarr->ids, copy->ids, sizeof(uint32_t) * copy->count);
		i = copy->count;
	}
	uint32_t *ids = &idarr->ids[i];
	copy_list_ids(&ids, list_in);
	return idarr;
}

/*
 * Voice allocation state flags.
 */
enum {
	SGS_VAS_GRAPH = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct SGS_VoAllocState {
	SGS_ScriptEvData *last_ev;
	const SGS_ProgramIDArr *op_carrs;
	uint32_t flags;
	uint32_t duration_ms;
} SGS_VoAllocState;

sgsArrType(SGS_VoAlloc, SGS_VoAllocState, _)

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true, or false on allocation failure
 */
static bool
SGS_VoAlloc_get_id(SGS_VoAlloc *restrict va,
		const SGS_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->voice_prev != NULL) {
		*vo_id = e->voice_prev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SGS_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SGS_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (SGS_VoAllocState){0};
			*vo_id = id;
			goto ASSIGNED;
		}
	}
	*vo_id = va->count;
	if (!_SGS_VoAlloc_add(va, NULL))
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
SGS_VoAlloc_update(SGS_VoAlloc *restrict va,
		SGS_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (uint32_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	if (!SGS_VoAlloc_get_id(va, e, vo_id))
		return false;
	e->vo_id = *vo_id;
	SGS_VoAllocState *vas = &va->a[*vo_id];
	vas->last_ev = e;
	vas->flags &= ~SGS_VAS_GRAPH;
	if ((e->ev_flags & SGS_SDEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = e->dur_ms;
	return true;
}

/*
 * Clear voice allocator.
 */
static inline void
SGS_VoAlloc_clear(SGS_VoAlloc *restrict o) {
	_SGS_VoAlloc_clear(o);
}

/*
 * Operator allocation state flags.
 */
enum {
	SGS_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct SGS_OpAllocState {
	SGS_ScriptOpData *last_pod;
	const SGS_ProgramIDArr *amods, *fmods, *pmods;
	uint32_t flags;
	//uint32_t duration_ms;
} SGS_OpAllocState;

sgsArrType(SGS_OpAlloc, SGS_OpAllocState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool
SGS_OpAlloc_get_id(SGS_OpAlloc *restrict oa,
		const SGS_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->on_prev != NULL) {
		*op_id = od->on_prev->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[id].last_pod->op_flags & SGS_SDOP_LATER_USED)
//			&& oa->a[id].duration_ms == 0) {
//			oa->a[id] = (SGS_OpAllocState){0};
//			*op_id = id;
//			goto ASSIGNED;
//		}
//	}
	*op_id = oa->count;
	if (!_SGS_OpAlloc_add(oa, NULL))
		return false;
//ASSIGNED:
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
SGS_OpAlloc_update(SGS_OpAlloc *restrict oa,
		SGS_ScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	SGS_ScriptEvData *e = od->event;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!SGS_OpAlloc_get_id(oa, od, op_id))
		return false;
	od->op_id = *op_id;
	SGS_OpAllocState *oas = &oa->a[*op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static inline void
SGS_OpAlloc_clear(SGS_OpAlloc *restrict o) {
	_SGS_OpAlloc_clear(o);
}

sgsArrType(SGS_PEvArr, SGS_ProgramEvent, )

sgsArrType(OpRefArr, SGS_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SGS_VoiceGraph {
	OpRefArr vo_graph;
	SGS_VoAlloc *va;
	SGS_OpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
} SGS_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void
SGS_init_VoiceGraph(SGS_VoiceGraph *restrict o,
		SGS_VoAlloc *restrict va, SGS_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

static void
SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o);

static bool
SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev,
		SGS_Mempool *restrict mp);

sgsArrType(OpDataArr, SGS_ProgramOpData, _)

typedef struct ParseConv {
	SGS_PEvArr ev_arr;
	SGS_VoAlloc va;
	SGS_OpAlloc oa;
	SGS_ProgramEvent *ev;
	SGS_VoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
	SGS_Mempool *mp;
} ParseConv;

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool
set_oplist(const SGS_ProgramIDArr **restrict dstp,
		const SGS_ScriptListData *restrict src,
		SGS_Mempool *restrict mem) {
	const SGS_ProgramIDArr *dst = SGS_create_ProgramIDArr(mem, src, *dstp);
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
		const SGS_ScriptOpData *restrict op, uint32_t op_id) {
	SGS_OpAllocState *oas = &o->oa.a[op_id];
	SGS_ProgramOpData *ood = _OpDataArr_add(&o->ev_op_data, NULL);
	if (!ood) goto MEM_ERR;
	ood->id = op_id;
	ood->params = op->op_params;
	ood->time = op->time;
	ood->amp = op->amp;
	ood->dynamp = op->dynamp;
	ood->freq = op->freq;
	ood->phase = op->phase;
	ood->dynfreq = op->dynfreq;
	ood->wave = op->wave;
	SGS_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	if (op->amods) {
		vas->flags |= SGS_VAS_GRAPH;
		if (!set_oplist(&oas->amods, op->amods, o->mp))
			goto MEM_ERR;
		ood->amods = oas->amods;
	}
	if (op->fmods) {
		vas->flags |= SGS_VAS_GRAPH;
		if (!set_oplist(&oas->fmods, op->fmods, o->mp))
			goto MEM_ERR;
		ood->fmods = oas->fmods;
	}
	if (op->pmods) {
		vas->flags |= SGS_VAS_GRAPH;
		if (!set_oplist(&oas->pmods, op->pmods, o->mp))
			goto MEM_ERR;
		ood->pmods = oas->pmods;
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
		SGS_ScriptListData *restrict op_list) {
	if (!op_list)
		return true;
	if (op_list->append) ParseConv_convert_ops(o, op_list->prev);
	for (SGS_ScriptOpData *op = op_list->first_on; op; op = op->next) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SGS_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id;
		if (!SGS_OpAlloc_update(&o->oa, op, &op_id) ||
		    !ParseConv_convert_ops(o, op->amods) ||
		    !ParseConv_convert_ops(o, op->fmods) ||
		    !ParseConv_convert_ops(o, op->pmods) ||
		    !ParseConv_convert_opdata(o, op, op_id))
			return false;
	}
	return true;
}

static bool
SGS_VoiceGraph_handle_op_node(SGS_VoiceGraph *restrict o,
		SGS_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
SGS_VoiceGraph_handle_op_list(SGS_VoiceGraph *restrict o,
		const SGS_ProgramIDArr *restrict op_list, uint8_t mod_use) {
	if (!op_list)
		return true;
	SGS_ProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!SGS_VoiceGraph_handle_op_node(o, &op_ref))
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
SGS_VoiceGraph_handle_op_node(SGS_VoiceGraph *restrict o,
		SGS_ProgramOpRef *restrict op_ref) {
	SGS_OpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SGS_OAS_VISITED) {
		SGS_warning("voicegraph",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SGS_OAS_VISITED;
	if (!SGS_VoiceGraph_handle_op_list(o, oas->amods, SGS_POP_AMOD))
		return false;
	if (!SGS_VoiceGraph_handle_op_list(o, oas->fmods, SGS_POP_FMOD))
		return false;
	if (!SGS_VoiceGraph_handle_op_list(o, oas->pmods, SGS_POP_PMOD))
		return false;
	oas->flags &= ~SGS_OAS_VISITED;
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
SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev,
		SGS_Mempool *restrict mp) {
	SGS_VoAllocState *vas = &o->va->a[ev->vo_id];
	if (!vas->op_carrs || !vas->op_carrs->count) goto DONE;
	if (!SGS_VoiceGraph_handle_op_list(o, vas->op_carrs, SGS_POP_CARR))
		return false;
	SGS_ProgramVoData *vd = (SGS_ProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(SGS_ProgramOpRef**) &vd->op_list, mp))
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
SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o) {
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
		SGS_ScriptEvData *restrict e) {
	uint32_t vo_id;
	uint32_t vo_params;
	if (!SGS_VoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	SGS_VoAllocState *vas = &o->va.a[vo_id];
	SGS_ProgramEvent *out_ev = SGS_PEvArr_add(&o->ev_arr, NULL);
	if (!out_ev) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ParseConv_convert_ops(o, &e->operators)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(SGS_ProgramOpData**) &out_ev->op_data,
					o->mp)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if ((e->ev_flags & SGS_SDEV_NEW_OPGRAPH) != 0)
		vas->flags |= SGS_VAS_GRAPH;
	if ((vas->flags & SGS_VAS_GRAPH) != 0)
		vo_params |= SGS_PVOP_OPLIST;
	if (vo_params != 0) {
		SGS_ProgramVoData *ovd =
			SGS_mpalloc(o->mp, sizeof(SGS_ProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if ((e->ev_flags & SGS_SDEV_NEW_OPGRAPH) != 0) {
			if (!set_oplist(&vas->op_carrs, &e->op_graph, o->mp))
				goto MEM_ERR;
			ovd->carrs = vas->op_carrs;
		}
		out_ev->vo_data = ovd;
		if ((vas->flags & SGS_VAS_GRAPH) != 0) {
			if (!SGS_VoiceGraph_set(&o->ev_vo_graph, out_ev, o->mp))
				goto MEM_ERR;
		}
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
		SGS_Script *restrict parse) {
	bool error = false;
	if (o->va.count > SGS_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			parse->name, SGS_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SGS_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			parse->name, SGS_POP_MAX_ID);
		error = true;
	}
	return !error;
}

static SGS_Program *
ParseConv_create_program(ParseConv *restrict o,
		SGS_Script *restrict parse) {
	SGS_Program *prg = SGS_mpalloc(o->mp, sizeof(SGS_Program));
	if (!prg) goto MEM_ERR;
	if (!SGS_PEvArr_mpmemdup(&o->ev_arr,
				(SGS_ProgramEvent**) &prg->events, o->mp))
		goto MEM_ERR;
	prg->ev_count = o->ev_arr.count;
	if (!(parse->sopt.set & SGS_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SGS_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = o->va.count;
	prg->op_count = o->oa.count;
	prg->op_nest_depth = o->ev_vo_graph.op_nest_max;
	prg->duration_ms = o->duration_ms;
	prg->name = parse->name;
	prg->mp = o->mp;
	o->mp = NULL; // don't destroy
	return prg;
MEM_ERR:
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SGS_Program *
ParseConv_convert(ParseConv *restrict o,
		SGS_Script *restrict parse) {
	SGS_Program *prg = NULL;
	o->mp = SGS_create_Mempool(0);
	if (!o->mp) goto MEM_ERR;
	SGS_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	uint32_t remaining_ms = 0;
	for (SGS_ScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SGS_VoAllocState *vas = &o->va.a[i];
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
		SGS_error("parseconv", "memory allocation failure");
	}
	SGS_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	SGS_OpAlloc_clear(&o->oa);
	SGS_VoAlloc_clear(&o->va);
	SGS_PEvArr_clear(&o->ev_arr);
	SGS_destroy_Mempool(o->mp); // NULL'd if kept for result
	return prg;
}

/**
 * Create program for the given parser output.
 *
 * \return instance or NULL on error
 */
SGS_Program *
SGS_build_Program(SGS_Script *restrict sd) {
	ParseConv pc = (ParseConv){0};
	SGS_Program *o = ParseConv_convert(&pc, sd);
	return o;
}

/**
 * Destroy instance.
 */
void
SGS_discard_Program(SGS_Program *restrict o) {
	if (!o)
		return;
	SGS_destroy_Mempool(o->mp);
}

static sgsNoinline void
print_linked(const char *restrict header,
		const SGS_ProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	SGS_printf("%s[%u", header, idarr->ids[0]);
	for (uint32_t i = 0; ++i < idarr->count; )
		SGS_printf(", %u", idarr->ids[i]);
	SGS_printf("]");
}

static void
print_oplist(const SGS_ProgramOpRef *restrict list,
		uint32_t count) {
	if (!list)
		return;
	FILE *out = SGS_print_stream();
	static const char *const uses[SGS_POP_USES] = {
		"CA",
		"AM",
		"FM",
		"PM",
	};

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", out);
	for (;;) {
		const uint32_t indent = list[i].level * 2;
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

static void
print_opline(const SGS_ProgramOpData *restrict od) {
	if (od->time.flags & SGS_TIMEP_IMPLICIT) {
		SGS_printf(
			"\n\top %u \tt=IMPL  \t", od->id);
	} else {
		SGS_printf(
			"\n\top %u \tt=%-6u\t", od->id, od->time.v_ms);
	}
	if ((od->freq.flags & SGS_RAMPP_STATE) != 0) {
		if ((od->freq.flags & SGS_RAMPP_GOAL) != 0)
			SGS_printf(
				"f=%-6.2f->%-6.2f", od->freq.v0, od->freq.vt);
		else
			SGS_printf(
				"f=%-6.2f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SGS_RAMPP_GOAL) != 0)
			SGS_printf(
				"f->%-6.2f\t", od->freq.vt);
		else
			SGS_printf(
				"\t\t");
	}
	if ((od->amp.flags & SGS_RAMPP_STATE) != 0) {
		if ((od->amp.flags & SGS_RAMPP_GOAL) != 0)
			SGS_printf(
				"\ta=%-6.2f->%-6.2f", od->amp.v0, od->amp.vt);
		else
			SGS_printf(
				"\ta=%-6.2f", od->amp.v0);
	} else if ((od->amp.flags & SGS_RAMPP_GOAL) != 0) {
		SGS_printf(
			"\ta->%-6.2f", od->amp.vt);
	}
}

/**
 * Print information about program contents. Useful for debugging.
 */
void
SGS_Program_print_info(const SGS_Program *restrict o) {
	SGS_printf("Program: \"%s\"\n"
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
		const SGS_ProgramEvent *ev = &o->events[ev_id];
		const SGS_ProgramVoData *vd = ev->vo_data;
		SGS_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			SGS_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(vd->op_list, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SGS_ProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    a,w", od->amods);
			print_linked("\n\t    f,w", od->fmods);
			print_linked("\n\t    p", od->pmods);
		}
		SGS_printf("\n");
	}
}
