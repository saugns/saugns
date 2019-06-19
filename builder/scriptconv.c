/* saugns: Script data to audio program converter.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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

#include "scriptconv.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from script data.
 *
 * Allocation of events, voices, operators.
 */

static SAU_ProgramOpGraph
*create_OpGraph(const SAU_ScriptEvData *restrict vo_in) {
	uint32_t size;
	size = vo_in->op_graph.count;
	if (!size)
		return NULL;
	const SAU_ScriptOpData **ops;
	uint32_t i;
	ops = (const SAU_ScriptOpData**) SAU_PtrList_ITEMS(&vo_in->op_graph);
	SAU_ProgramOpGraph *o;
	o = malloc(sizeof(SAU_ProgramOpGraph) + sizeof(int32_t) * (size - 1));
	if (!o)
		return NULL;
	o->opc = size;
	for (i = 0; i < size; ++i) {
		o->ops[i] = ops[i]->op_id;
	}
	return o;
}

static SAU_ProgramOpAdjcs
*create_OpAdjcs(const SAU_ScriptOpData *restrict op_in) {
	uint32_t size;
	size = op_in->fmods.count +
		op_in->pmods.count +
		op_in->amods.count;
	if (!size)
		return NULL;
	const SAU_ScriptOpData **ops;
	uint32_t i;
	uint32_t *data;
	SAU_ProgramOpAdjcs *o;
	o = malloc(sizeof(SAU_ProgramOpAdjcs) + sizeof(int32_t) * (size - 1));
	if (!o)
		return NULL;
	o->fmodc = op_in->fmods.count;
	o->pmodc = op_in->pmods.count;
	o->amodc = op_in->amods.count;
	data = o->adjcs;
	ops = (const SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op_in->fmods);
	for (i = 0; i < o->fmodc; ++i)
		*data++ = ops[i]->op_id;
	ops = (const SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op_in->pmods);
	for (i = 0; i < o->pmodc; ++i)
		*data++ = ops[i]->op_id;
	ops = (const SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op_in->amods);
	for (i = 0; i < o->amodc; ++i)
		*data++ = ops[i]->op_id;
	return o;
}

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SAU_ScriptEvData *restrict ve) {
	SAU_ScriptOpData **ops;
	uint32_t duration_ms = 0;
	/* FIXME: node list type? */
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&ve->op_graph);
	for (size_t i = 0; i < ve->op_graph.count; ++i) {
		SAU_ScriptOpData *op = ops[i];
		if (op->time_ms > duration_ms)
			duration_ms = op->time_ms;
	}
	return duration_ms;
}

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true if voice found, false if voice added or recycled
 */
static bool SAU_VoAlloc_get_id(SAU_VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->vo_prev != NULL) {
		*vo_id = e->vo_prev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SAU_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			free(vas->op_graph);
			*vas = (SAU_VoAllocState){0};
			*vo_id = id;
			return false;
		}
	}
	*vo_id = va->count;
	_SAU_VoAlloc_add(va, NULL);
	return false;
}

/*
 * Update voices for event and return a voice ID for the event.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 */
static uint32_t SAU_VoAlloc_update(SAU_VoAlloc *restrict va,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id;
	for (vo_id = 0; vo_id < va->count; ++vo_id) {
		if (va->a[vo_id].duration_ms < e->wait_ms)
			va->a[vo_id].duration_ms = 0;
		else
			va->a[vo_id].duration_ms -= e->wait_ms;
	}
	SAU_VoAlloc_get_id(va, e, &vo_id);
	e->vo_id = vo_id;
	SAU_VoAllocState *vas = &va->a[vo_id];
	vas->last_ev = e;
	vas->flags &= ~SAU_VAS_GRAPH;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static void SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		SAU_VoAllocState *vas = &o->a[i];
		free(vas->op_graph);
	}
	_SAU_VoAlloc_clear(o);
}

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true if operator found, false if operator added or recycled
 */
static bool SAU_OpAlloc_get_id(SAU_OpAlloc *restrict oa,
		const SAU_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->op_prev != NULL) {
		*op_id = od->op_prev->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		SAU_OpAllocState *oas = &oa->a[id];
//		if (!(oas->last_sod->op_flags & SAU_SDOP_LATER_USED)
//			&& oas->duration_ms == 0) {
//			//free(oas->adjcs);
//			*oas = (SAU_OpAllocState){0};
//			*op_id = id;
//			return false;
//		}
//	}
	*op_id = oa->count;
	_SAU_OpAlloc_add(oa, NULL);
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
static uint32_t SAU_OpAlloc_update(SAU_OpAlloc *restrict oa,
		SAU_ScriptOpData *restrict od) {
//	SAU_ScriptEvData *e = od->event;
	uint32_t op_id;
//	for (op_id = 0; op_id < oa->count; ++op_id) {
//		if (oa->a[op_id].duration_ms < e->wait_ms)
//			oa->a[op_id].duration_ms = 0;
//		else
//			oa->a[op_id].duration_ms -= e->wait_ms;
//	}
	SAU_OpAlloc_get_id(oa, od, &op_id);
	od->op_id = op_id;
	SAU_OpAllocState *oas = &oa->a[op_id];
	oas->last_sod = od;
//	oas->duration_ms = od->time_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static void SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

SAU_DEF_ArrType(OpDataArr, SAU_ProgramOpData, )

typedef struct ScriptConv {
	SAU_PtrList ev_list;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	SAU_VoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
} ScriptConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 */
static void ScriptConv_convert_opdata(ScriptConv *restrict o,
		const SAU_ScriptOpData *restrict op, uint32_t op_id) {
	SAU_ProgramOpData ood = {0};
	ood.id = op_id;
	ood.params = op->op_params;
	ood.adjcs = NULL;
	ood.time_ms = op->time_ms;
	ood.silence_ms = op->silence_ms;
	ood.wave = op->wave;
	ood.freq = op->freq;
	ood.freq2 = op->freq2;
	ood.amp = op->amp;
	ood.amp2 = op->amp2;
	ood.phase = op->phase;
	OpDataArr_add(&o->ev_op_data, &ood);
}

/*
 * Convert the flat script operator data list in two stages,
 * adding all the operator data nodes, then filling in
 * the adjacency lists when all nodes are registered.
 */
static void ScriptConv_convert_ops(ScriptConv *restrict o,
		SAU_PtrList *restrict op_list) {
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(op_list);
	for (size_t i = op_list->old_count; i < op_list->count; ++i) {
		SAU_ScriptOpData *op = ops[i];
		// TODO: handle multiple operator nodes
		if (op->op_flags & SAU_SDOP_MULTIPLE) continue;
		uint32_t op_id = SAU_OpAlloc_update(&o->oa, op);
		ScriptConv_convert_opdata(o, op, op_id);
	}
	for (size_t i = 0; i < o->ev_op_data.count; ++i) {
		SAU_ProgramOpData *od = &o->ev_op_data.a[i];
		SAU_OpAllocState *oas = &o->oa.a[od->id];
		if (od->params & SAU_POPP_ADJCS) {
			SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
			vas->flags |= SAU_VAS_GRAPH;
//			free(oas->adjcs);
			oas->adjcs = create_OpAdjcs(oas->last_sod);
			od->adjcs = oas->adjcs;
		}
	}
}

/*
 * Convert all voice and operator data for a script event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 */
static void ScriptConv_convert_event(ScriptConv *restrict o,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id = SAU_VoAlloc_update(&o->va, e);
	uint32_t vo_params;
	SAU_VoAllocState *vas = &o->va.a[vo_id];
	SAU_ProgramEvent *out_ev = calloc(1, sizeof(SAU_ProgramEvent));
	SAU_PtrList_add(&o->ev_list, out_ev);
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	ScriptConv_convert_ops(o, &e->op_all);
	if (o->ev_op_data.count > 0) {
		OpDataArr_memdup(&o->ev_op_data, &out_ev->op_data);
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->flags |= SAU_VAS_GRAPH;
	if (vas->flags & SAU_VAS_GRAPH)
		vo_params |= SAU_PVOP_OPLIST;
	if (vo_params != 0) {
		SAU_ProgramVoData *ovd = calloc(1, sizeof(SAU_ProgramVoData));
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
			free(vas->op_graph);
			vas->op_graph = create_OpGraph(e);
		}
		out_ev->vo_data = ovd;
		if (vas->flags & SAU_VAS_GRAPH) {
			SAU_VoiceGraph_set(&o->ev_vo_graph, out_ev);
		}
	}
}

/*
 * Check whether program can be returned for use.
 *
 * \return true, unless invalid data detected
 */
static bool ScriptConv_check_validity(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	bool error = false;
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			script->name, SAU_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			script->name, SAU_POP_MAX_ID);
		error = true;
	}
	if (o->ev_vo_graph.op_nest_depth > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %d levels, maximum is %d levels\n",
			script->name, o->ev_vo_graph.op_nest_depth, UINT8_MAX);
		error = true;
	}
	if (error) {
		return false;
	}
	return true;
}

static void Program_destroy_event_data(SAU_ProgramEvent *restrict e);

static SAU_Program *ScriptConv_create_program(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = calloc(1, sizeof(SAU_Program));
	if (!prg) goto ERROR;
	if (!SAU_PtrList_memdup(&o->ev_list, (void***) &prg->events))
		goto ERROR;
	prg->ev_count = o->ev_list.count;
	if (!(script->sopt.changed & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = o->va.count;
	prg->op_count = o->oa.count;
	prg->op_nest_depth = o->ev_vo_graph.op_nest_depth;
	prg->duration_ms = o->duration_ms;
	prg->name = script->name;
	return prg;
ERROR:
	free(prg);
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program *ScriptConv_convert(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = NULL;
	SAU_ScriptEvData *e;
	size_t i;
	uint32_t remaining_ms = 0;

	SAU_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	for (e = script->events; e; e = e->next) {
		ScriptConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;
	if (ScriptConv_check_validity(o, script)) {
		prg = ScriptConv_create_program(o, script);
	}
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
	OpDataArr_clear(&o->ev_op_data);
	SAU_fini_VoiceGraph(&o->ev_vo_graph);
	SAU_PtrList_clear(&o->ev_list);
	return prg;
}

/**
 * Create internal program for the given script data.
 *
 * \return instance or NULL on error
 */
SAU_Program* SAU_build_Program(SAU_Script *restrict sd) {
	ScriptConv sc = (ScriptConv){0};
	SAU_Program *o = ScriptConv_convert(&sc, sd);
	return o;
}

/*
 * Destroy data stored for event. Does not free the event itself.
 */
static void Program_destroy_event_data(SAU_ProgramEvent *restrict e) {
	if (e->vo_data != NULL) {
		free((void*)e->vo_data->op_list);
		free((void*)e->vo_data);
	}
	if (e->op_data != NULL) {
		for (size_t i = 0; i < e->op_data_count; ++i) {
			free((void*)e->op_data[i].adjcs);
		}
		free((void*)e->op_data);
	}
	free(e);
}

/**
 * Destroy instance.
 */
void SAU_discard_Program(SAU_Program *restrict o) {
	if (!o)
		return;
	if (o->events != NULL) {
		for (size_t i = 0; i < o->ev_count; ++i) {
			SAU_ProgramEvent *e = (SAU_ProgramEvent*) o->events[i];
			Program_destroy_event_data(e);
		}
		free(o->events);
	}
	free(o);
}

static void print_linked(const char *restrict header,
		const char *restrict footer,
		uint32_t count, const uint32_t *restrict nodes) {
	uint32_t i;
	if (!count)
		return;
	fprintf(stdout, "%s%d", header, nodes[0]);
	for (i = 0; ++i < count; )
		fprintf(stdout, ", %d", nodes[i]);
	fprintf(stdout, "%s", footer);
}

static void print_oplist(const SAU_ProgramOpRef *restrict list,
		uint32_t count) {
	static const char *const uses[SAU_POP_USES] = {
		"CA",
		"FM",
		"PM",
		"AM"
	};

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", stdout);
	for (;;) {
		const uint32_t indent = list[i].level * 2;
		if (indent > max_indent) max_indent = indent;
		fprintf(stdout, "%6d:  ", list[i].id);
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

static void print_opline(const SAU_ProgramOpData *restrict od) {
	if (od->time_ms == SAU_TIME_INF) {
		fprintf(stdout,
			"\n\top %d \tt=INF   \t", od->id);
	} else {
		fprintf(stdout,
			"\n\top %d \tt=%-6d\t", od->id, od->time_ms);
	}
	if ((od->freq.flags & SAU_RAMP_STATE) != 0) {
		if ((od->freq.flags & SAU_RAMP_CURVE) != 0)
			fprintf(stdout,
				"f=%-6.1f->%-6.1f", od->freq.v0, od->freq.vt);
		else
			fprintf(stdout,
				"f=%-6.1f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SAU_RAMP_CURVE) != 0)
			fprintf(stdout,
				"f->%-6.1f\t", od->freq.vt);
		else
			fprintf(stdout,
				"\t\t");
	}
	if ((od->amp.flags & SAU_RAMP_STATE) != 0) {
		if ((od->amp.flags & SAU_RAMP_CURVE) != 0)
			fprintf(stdout,
				"\ta=%-6.1f->%-6.1f", od->amp.v0, od->amp.vt);
		else
			fprintf(stdout,
				"\ta=%-6.1f", od->amp.v0);
	} else if ((od->amp.flags & SAU_RAMP_CURVE) != 0) {
		fprintf(stdout,
			"\ta->%-6.1f", od->amp.vt);
	}
}

/**
 * Print information about program contents. Useful for debugging.
 */
void SAU_Program_print_info(const SAU_Program *restrict o) {
	fprintf(stdout,
		"Program: \"%s\"\n", o->name);
	fprintf(stdout,
		"\tDuration: \t%d ms\n"
		"\tEvents:   \t%zd\n"
		"\tVoices:   \t%hd\n"
		"\tOperators:\t%d\n",
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const SAU_ProgramEvent *ev = o->events[ev_id];
		const SAU_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"\\%d \tEV %zd \t(VO %hd)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			const SAU_ProgramOpRef *ol = vd->op_list;
			fprintf(stdout,
				"\n\tvo %d", ev->vo_id);
			if (ol != NULL)
				print_oplist(ol, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SAU_ProgramOpData *od = &ev->op_data[i];
			const SAU_ProgramOpAdjcs *ga = od->adjcs;
			print_opline(od);
			if (ga != NULL) {
				print_linked("\n\t    f~[", "]", ga->fmodc,
					ga->adjcs);
				print_linked("\n\t    p+[", "]", ga->pmodc,
					&ga->adjcs[ga->fmodc]);
				print_linked("\n\t    a~[", "]", ga->amodc,
					&ga->adjcs[ga->fmodc + ga->pmodc]);
			}
		}
		putc('\n', stdout);
	}
}
