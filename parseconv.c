/* sgensys: Parse result to audio program converter.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#include "script.h"
#include "program.h"
#include "mempool.h"
#include "arrtype.h"
#include <stdio.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static const SGS_ProgramIDArr blank_idarr = {0};

static const SGS_ProgramIDArr *
SGS_create_ProgramIDArr(SGS_Mempool *restrict mp,
		const SGS_ScriptListData *restrict list_in) {
	uint32_t count = 0;
	for (SGS_ScriptOpData *op = list_in->first_on; op; op = op->next)
		++count;
	if (!count)
		return &blank_idarr;
	SGS_ProgramIDArr *idarr = SGS_mpalloc(mp,
			sizeof(SGS_ProgramIDArr) + sizeof(uint32_t) * count);
	if (!idarr)
		return NULL;
	idarr->count = count;
	uint32_t i = 0;
	for (SGS_ScriptOpData *op = list_in->first_on; op; op = op->next)
		idarr->ids[i++] = op->op_id;
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
	const SGS_ProgramIDArr *op_graph;
	uint32_t flags;
	uint32_t duration_ms;
} SGS_VoAllocState;

sgsArrType(SGS_VoAlloc, SGS_VoAllocState, _)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SGS_ScriptEvData *restrict ve) {
	uint32_t duration_ms = 0;
	for (SGS_ScriptOpData *op = ve->operators.first_on; op; op = op->next) {
		if (op->time.v_ms > duration_ms)
			duration_ms = op->time.v_ms;
	}
	return duration_ms;
}

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true if voice found, false if voice added or recycled
 */
static bool SGS_VoAlloc_get_id(SGS_VoAlloc *restrict va,
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
			return false;
		}
	}
	*vo_id = va->count;
	_SGS_VoAlloc_add(va, NULL);
	return false;
}

/*
 * Update voices for event and return a voice ID for the event.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 */
static uint32_t SGS_VoAlloc_update(SGS_VoAlloc *restrict va,
		SGS_ScriptEvData *restrict e) {
	uint32_t vo_id;
	for (vo_id = 0; vo_id < va->count; ++vo_id) {
		if (va->a[vo_id].duration_ms < e->wait_ms)
			va->a[vo_id].duration_ms = 0;
		else
			va->a[vo_id].duration_ms -= e->wait_ms;
	}
	SGS_VoAlloc_get_id(va, e, &vo_id);
	e->vo_id = vo_id;
	SGS_VoAllocState *vas = &va->a[vo_id];
	vas->last_ev = e;
	vas->flags &= ~SGS_VAS_GRAPH;
	if ((e->ev_flags & SGS_SDEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static inline void SGS_VoAlloc_clear(SGS_VoAlloc *restrict o) {
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
 * \return true if operator found, false if operator added or recycled
 */
static bool SGS_OpAlloc_get_id(SGS_OpAlloc *restrict oa,
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
//			return false;
//		}
//	}
	*op_id = oa->count;
	_SGS_OpAlloc_add(oa, NULL);
	return false;
}

/*
 * Update operators for event and return an operator ID for the event.
 *
 * Use the current operator if any, otherwise allocating a new one.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * Only valid to call for single-operator nodes.
 */
static uint32_t SGS_OpAlloc_update(SGS_OpAlloc *restrict oa,
		SGS_ScriptOpData *restrict od) {
//	SGS_ScriptEvData *e = od->event;
	uint32_t op_id;
//	for (op_id = 0; op_id < oa->count; ++op_id) {
//		if (oa->a[op_id].duration_ms < e->wait_ms)
//			oa->a[op_id].duration_ms = 0;
//		else
//			oa->a[op_id].duration_ms -= e->wait_ms;
//	}
	SGS_OpAlloc_get_id(oa, od, &op_id);
	od->op_id = op_id;
	SGS_OpAllocState *oas = &oa->a[op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static inline void SGS_OpAlloc_clear(SGS_OpAlloc *restrict o) {
	_SGS_OpAlloc_clear(o);
}

sgsArrType(SGS_PEvArr, SGS_ProgramEvent, )

sgsArrType(OpRefArr, SGS_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SGS_VoiceGraph {
	OpRefArr op_list;
	SGS_VoAlloc *va;
	SGS_OpAlloc *oa;
	uint32_t op_nest_depth;
} SGS_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void SGS_init_VoiceGraph(SGS_VoiceGraph *restrict o,
		SGS_VoAlloc *restrict va, SGS_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

void SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o);

bool SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev,
		SGS_Mempool *restrict mp);

sgsArrType(OpDataArr, SGS_ProgramOpData, )

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
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 */
static void ParseConv_convert_opdata(ParseConv *restrict o,
		const SGS_ScriptOpData *restrict op, uint32_t op_id) {
	SGS_OpAllocState *oas = &o->oa.a[op_id];
	SGS_ProgramOpData *ood = OpDataArr_add(&o->ev_op_data, NULL);
	ood->id = op_id;
	ood->params = op->op_params;
	ood->time = op->time;
	ood->silence_ms = op->silence_ms;
	ood->wave = op->wave;
	ood->freq = op->freq;
	ood->amp = op->amp;
	ood->phase = op->phase;
	ood->dynfreq = op->dynfreq;
	ood->dynamp = op->dynamp;
	SGS_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	if (op->amods) {
		ood->amods = oas->amods =
			SGS_create_ProgramIDArr(o->mp, op->amods);
		vas->flags |= SGS_VAS_GRAPH;
	}
	if (op->fmods) {
		ood->fmods = oas->fmods =
			SGS_create_ProgramIDArr(o->mp, op->fmods);
		vas->flags |= SGS_VAS_GRAPH;
	}
	if (op->pmods) {
		ood->pmods = oas->pmods =
			SGS_create_ProgramIDArr(o->mp, op->pmods);
		vas->flags |= SGS_VAS_GRAPH;
	}
}

/*
 * Visit each operator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * operator data.
 */
static void ParseConv_convert_ops(ParseConv *restrict o,
		SGS_ScriptListData *restrict op_list) {
	if (!op_list)
		return;
	for (SGS_ScriptOpData *op = op_list->first_on; op; op = op->next) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SGS_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id = SGS_OpAlloc_update(&o->oa, op);
		ParseConv_convert_ops(o, op->fmods);
		ParseConv_convert_ops(o, op->pmods);
		ParseConv_convert_ops(o, op->amods);
		ParseConv_convert_opdata(o, op, op_id);
	}
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool SGS_VoiceGraph_traverse_ops(SGS_VoiceGraph *restrict o,
		SGS_ProgramOpRef *restrict op_ref, uint32_t level) {
	SGS_OpAllocState *oas = &o->oa->a[op_ref->id];
	uint32_t i;
	if ((oas->flags & SGS_OAS_VISITED) != 0) {
		SGS_warning("parseconv",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	SGS_ProgramOpRef mod_op_ref;
	op_ref->level = level++;
	oas->flags |= SGS_OAS_VISITED;
	if (oas->amods != NULL) for (i = 0; i < oas->amods->count; ++i) {
		mod_op_ref.id = oas->amods->ids[i];
		mod_op_ref.use = SGS_POP_AMOD;
//		fprintf(stderr, "visit amod node %d\n", mod_op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
			return false;
	}
	if (oas->fmods != NULL) for (i = 0; i < oas->fmods->count; ++i) {
		mod_op_ref.id = oas->fmods->ids[i];
		mod_op_ref.use = SGS_POP_FMOD;
//		fprintf(stderr, "visit fmod node %d\n", mod_op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
			return false;
	}
	if (oas->pmods != NULL) for (i = 0; i < oas->pmods->count; ++i) {
		mod_op_ref.id = oas->pmods->ids[i];
		mod_op_ref.use = SGS_POP_PMOD;
//		fprintf(stderr, "visit pmod node %d\n", mod_op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
			return false;
	}
	oas->flags &= ~SGS_OAS_VISITED;
	if (!OpRefArr_add(&o->op_list, op_ref))
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
bool SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev,
		SGS_Mempool *restrict mp) {
	SGS_ProgramOpRef op_ref = {0, SGS_POP_CARR, 0};
	SGS_VoAllocState *vas = &o->va->a[ev->vo_id];
	SGS_ProgramVoData *vd = (SGS_ProgramVoData*) ev->vo_data;
	const SGS_ProgramIDArr *graph = vas->op_graph;
	uint32_t i;
	if (!graph)
		return true;
	for (i = 0; i < graph->count; ++i) {
		op_ref.id = graph->ids[i];
//		fprintf(stderr, "visit node %u\n", op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &op_ref, 0))
			return false;
	}
	if (!OpRefArr_mpmemdup(&o->op_list,
				(SGS_ProgramOpRef**) &vd->op_list, mp))
		return false;
	vd->op_count = o->op_list.count;
	o->op_list.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
void SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o) {
	OpRefArr_clear(&o->op_list);
}

/*
 * Convert all voice and operator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 */
static void ParseConv_convert_event(ParseConv *restrict o,
		SGS_ScriptEvData *restrict e) {
	uint32_t vo_id = SGS_VoAlloc_update(&o->va, e);
	uint32_t vo_params;
	SGS_VoAllocState *vas = &o->va.a[vo_id];
	SGS_ProgramEvent *out_ev = SGS_PEvArr_add(&o->ev_arr, NULL);
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	ParseConv_convert_ops(o, &e->operators);
	if (o->ev_op_data.count > 0) {
		OpDataArr_mpmemdup(&o->ev_op_data,
				(SGS_ProgramOpData**) &out_ev->op_data, o->mp);
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
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if ((e->ev_flags & SGS_SDEV_NEW_OPGRAPH) != 0) {
			vas->op_graph =
				SGS_create_ProgramIDArr(o->mp, &e->op_graph);
		}
		out_ev->vo_data = ovd;
		if ((vas->flags & SGS_VAS_GRAPH) != 0) {
			SGS_VoiceGraph_set(&o->ev_vo_graph, out_ev, o->mp);
		}
	}
}

static SGS_Program *_ParseConv_copy_out(ParseConv *restrict o,
		SGS_Script *restrict parse) {
	SGS_Program *prg = SGS_mpalloc(o->mp, sizeof(SGS_Program));
	if (!prg) goto ERROR;
	if (!SGS_PEvArr_mpmemdup(&o->ev_arr,
				(SGS_ProgramEvent**) &prg->events, o->mp))
		goto ERROR;
	prg->ev_count = o->ev_arr.count;
	if (!(parse->sopt.set & SGS_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SGS_PMODE_AMP_DIV_VOICES;
	}
	if (o->va.count > SGS_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			parse->name, SGS_PVO_MAX_ID);
		goto ERROR;
	}
	prg->vo_count = o->va.count;
	if (o->oa.count > SGS_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			parse->name, SGS_POP_MAX_ID);
		goto ERROR;
	}
	prg->op_count = o->oa.count;
	if (o->ev_vo_graph.op_nest_depth > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %u levels, maximum is %u levels\n",
			parse->name, o->ev_vo_graph.op_nest_depth, UINT8_MAX);
		goto ERROR;
	}
	prg->op_nest_depth = o->ev_vo_graph.op_nest_depth;
	prg->duration_ms = o->duration_ms;
	prg->name = parse->name;
	prg->mp = o->mp;
	o->mp = NULL; // don't destroy
	return prg;
ERROR:
	return NULL;
}

static void _ParseConv_cleanup(ParseConv *restrict o) {
	SGS_OpAlloc_clear(&o->oa);
	SGS_VoAlloc_clear(&o->va);
	SGS_fini_VoiceGraph(&o->ev_vo_graph);
	OpDataArr_clear(&o->ev_op_data);
	SGS_PEvArr_clear(&o->ev_arr);
	SGS_destroy_Mempool(o->mp); // NULL'd if kept for result
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SGS_Program *ParseConv_convert(ParseConv *restrict o,
		SGS_Script *restrict parse) {
	SGS_Program *prg;
	SGS_ScriptEvData *e;
	size_t i;
	uint32_t remaining_ms = 0;

	o->mp = SGS_create_Mempool(0);
	SGS_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	for (e = parse->events; e; e = e->next) {
		ParseConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (i = 0; i < o->va.count; ++i) {
		SGS_VoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;

	prg = _ParseConv_copy_out(o, parse);
	_ParseConv_cleanup(o);
	return prg;
}

/**
 * Create program for the given parser output.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build_Program(SGS_Script *restrict sd) {
	ParseConv pc = (ParseConv){0};
	SGS_Program *o = ParseConv_convert(&pc, sd);
	return o;
}

/**
 * Destroy instance.
 */
void SGS_discard_Program(SGS_Program *restrict o) {
	if (!o)
		return;
	SGS_destroy_Mempool(o->mp);
}

static void print_linked(const char *restrict header,
		const char *restrict footer,
		const SGS_ProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	fprintf(stdout, "%s%u", header, idarr->ids[0]);
	for (uint32_t i = 0; ++i < idarr->count; )
		fprintf(stdout, ", %u", idarr->ids[i]);
	fprintf(stdout, "%s", footer);
}

static void print_oplist(const SGS_ProgramOpRef *restrict list,
		uint32_t count) {
	if (!list)
		return;
	static const char *const uses[SGS_POP_USES] = {
		"CA",
		"AM",
		"FM",
		"PM",
	};

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", stdout);
	for (;;) {
		const uint32_t indent = list[i].level * 2;
		if (indent > max_indent) max_indent = indent;
		fprintf(stdout, "%6u:  ", list[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', stdout);
		fputs(uses[list[i].use], stdout);
		if (++i == count) break;
		fputs("\n\t     ", stdout);
	}
	for (uint32_t j = max_indent; j > 0; --j)
		putc(' ', stdout);
	putc(']', stdout);
}

static void print_opline(const SGS_ProgramOpData *restrict od) {
	if (od->time.flags & SGS_TIMEP_IMPLICIT) {
		fprintf(stdout,
			"\n\top %u \tt=IMPL  \t", od->id);
	} else {
		fprintf(stdout,
			"\n\top %u \tt=%-6u\t", od->id, od->time.v_ms);
	}
	if ((od->freq.flags & SGS_RAMPP_STATE) != 0) {
		if ((od->freq.flags & SGS_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f=%-6.2f->%-6.2f", od->freq.v0, od->freq.vt);
		else
			fprintf(stdout,
				"f=%-6.2f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SGS_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f->%-6.2f\t", od->freq.vt);
		else
			fprintf(stdout,
				"\t\t");
	}
	if ((od->amp.flags & SGS_RAMPP_STATE) != 0) {
		if ((od->amp.flags & SGS_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"\ta=%-6.2f->%-6.2f", od->amp.v0, od->amp.vt);
		else
			fprintf(stdout,
				"\ta=%-6.2f", od->amp.v0);
	} else if ((od->amp.flags & SGS_RAMPP_GOAL) != 0) {
		fprintf(stdout,
			"\ta->%-6.2f", od->amp.vt);
	}
}

/**
 * Print information about program contents. Useful for debugging.
 */
void SGS_Program_print_info(const SGS_Program *restrict o) {
	fprintf(stdout,
		"Program: \"%s\"\n", o->name);
	fprintf(stdout,
		"\tDuration: \t%u ms\n"
		"\tEvents:   \t%zu\n"
		"\tVoices:   \t%hu\n"
		"\tOperators:\t%u\n",
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const SGS_ProgramEvent *ev = &o->events[ev_id];
		const SGS_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"\\%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			fprintf(stdout,
				"\n\tvo %u", ev->vo_id);
			print_oplist(vd->op_list, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SGS_ProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    aw[", "]", od->amods);
			print_linked("\n\t    fw[", "]", od->fmods);
			print_linked("\n\t    p[", "]", od->pmods);
		}
		putc('\n', stdout);
	}
}
