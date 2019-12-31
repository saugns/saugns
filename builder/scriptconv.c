/* saugns: Script data to audio program converter.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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

#include "builder.h"
#include "../program.h"
#include "../ptrlist.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from script data.
 *
 * Allocation of events, voices, operators.
 */

static const SAU_ProgramOpList blank_oplist = {0};

static sauNoinline const SAU_ProgramOpList
*create_ProgramOpList(const SAU_NodeList *restrict op_list,
		SAU_MemPool *restrict mem) {
	if (!op_list)
		return &blank_oplist;
	uint32_t count = 0;
	for (SAU_NodeRef *ref = op_list->refs; ref != NULL; ref = ref->next)
		++count;
	if (count == 0)
		return &blank_oplist;
	SAU_ProgramOpList *o;
	o = SAU_MemPool_alloc(mem,
			sizeof(SAU_ProgramOpList) + sizeof(uint32_t) * count);
	if (!o)
		return NULL;
	o->count = count;
	size_t i = 0;
	for (SAU_NodeRef *ref = op_list->refs; ref != NULL; ref = ref->next) {
		SAU_ScriptOpData *od = ref->data;
		o->ids[i++] = od->op_id;
	}
	return o;
}

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SAU_ScriptEvData *restrict ve) {
	uint32_t duration_ms = 0;
	for (SAU_NodeRef *ref = ve->op_carriers->refs;
			ref != NULL; ref = ref->next) {
		SAU_ScriptOpData *op = ref->data;
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
	vas->flags &= ~SAU_VOAS_GRAPH;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static void SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
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
	oas->flags = 0;
//	oas->duration_ms = od->time_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static void SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

sauArrType(OpDataArr, SAU_ProgramOpData, )

typedef struct ScriptConv {
	SAU_PtrList ev_list;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	OpDataArr ev_op_data;
	VoiceGraph ev_vo_graph;
	uint32_t duration_ms;
	SAU_MemPool *mem;
	SAU_MemPool *tmp; // for allocations not kept in output
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
 * Check whether or not a new program op list is needed.
 *
 * \return true given non-copied items or list clearing
 */
static bool need_new_oplist(const SAU_NodeList *restrict op_list,
		const SAU_ProgramOpList *restrict prev_pol) {
	if (!prev_pol)
		return true;
	if (!op_list)
		return false;
	return (op_list->new_refs != NULL) ||
		((prev_pol->count > 0) && !op_list->refs);
}

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool update_oplist(const SAU_ProgramOpList **restrict dstp,
		const SAU_NodeList *restrict src,
		SAU_MemPool *restrict mem) {
	const SAU_ProgramOpList *dst = create_ProgramOpList(src, mem);
	if (!dst)
		return false;
	*dstp = dst;
	return true;
}

/*
 * Update program operator lists for updated lists in script data.
 *
 * Ensures non-NULL list pointers for SAU_OpAllocState nodes, while for
 * output nodes, they are non-NULL initially and upon changes.
 *
 * \return true, or false on allocation failure
 */
static bool ScriptConv_update_modlists(ScriptConv *restrict o,
		SAU_ProgramOpData *restrict od) {
	SAU_OpAllocState *oas = &o->oa.a[od->id];
	SAU_ScriptOpData *sod = oas->last_sod;
	const SAU_NodeList *fmod_list = NULL;
	const SAU_NodeList *pmod_list = NULL;
	const SAU_NodeList *amod_list = NULL;
	for (const SAU_NodeList *list = sod->mod_lists;
			list != NULL; list = list->next) {
		switch (list->type) {
		case SAU_SDLT_FMODS:
			fmod_list = list;
			break;
		case SAU_SDLT_PMODS:
			pmod_list = list;
			break;
		case SAU_SDLT_AMODS:
			amod_list = list;
			break;
		}
	}
	SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	if (need_new_oplist(fmod_list, oas->fmods)) {
		if (!update_oplist(&oas->fmods, fmod_list, o->mem))
			return false;
		vas->flags |= SAU_VOAS_GRAPH;
		od->fmods = oas->fmods;
	}
	if (need_new_oplist(pmod_list, oas->pmods)) {
		if (!update_oplist(&oas->pmods, pmod_list, o->mem))
			return false;
		vas->flags |= SAU_VOAS_GRAPH;
		od->pmods = oas->pmods;
	}
	if (need_new_oplist(amod_list, oas->amods)) {
		if (!update_oplist(&oas->amods, amod_list, o->mem))
			return false;
		vas->flags |= SAU_VOAS_GRAPH;
		od->amods = oas->amods;
	}
	return true;
}

/*
 * Convert the flat script operator data list in two stages,
 * adding all the operator data nodes, then filling in
 * the adjacency lists when all nodes are registered.
 */
static void ScriptConv_convert_ops(ScriptConv *restrict o,
		SAU_NodeList *restrict op_list) {
	SAU_NodeRef *ref = op_list->new_refs;
	for (; ref != NULL; ref = ref->next) {
		SAU_ScriptOpData *op = ref->data;
		uint32_t op_id = SAU_OpAlloc_update(&o->oa, op);
		ScriptConv_convert_opdata(o, op, op_id);
	}
	for (size_t i = 0; i < o->ev_op_data.count; ++i) {
		SAU_ProgramOpData *od = &o->ev_op_data.a[i];
		ScriptConv_update_modlists(o, od);
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
	SAU_ProgramEvent *out_ev;
	out_ev = SAU_MemPool_alloc(o->tmp, sizeof(SAU_ProgramEvent));
	SAU_PtrList_add(&o->ev_list, out_ev);
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	ScriptConv_convert_ops(o, &e->op_all);
	if (o->ev_op_data.count > 0) {
		OpDataArr_mpmemdup(&o->ev_op_data, &out_ev->op_data, o->mem);
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->flags |= SAU_VOAS_GRAPH;
	if (vas->flags & SAU_VOAS_GRAPH)
		vo_params |= SAU_PVOP_GRAPH;
	if (vo_params != 0) {
		SAU_ProgramVoData *ovd;
		ovd = SAU_MemPool_alloc(o->mem, sizeof(SAU_ProgramVoData));
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
			vas->op_carriers = create_ProgramOpList(e->op_carriers,
							o->tmp);
		}
		out_ev->vo_data = ovd;
		if (vas->flags & SAU_VOAS_GRAPH) {
			SAU_VoiceGraph_set(&o->ev_vo_graph, out_ev);
		}
	}
}

static SAU_Program *_ScriptConv_copy_out(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = NULL;
	SAU_ProgramEvent *events = NULL;
	size_t ev_count;
	ev_count = o->ev_list.count;
	if (ev_count > 0) {
		SAU_ProgramEvent **in_events;
		in_events = (SAU_ProgramEvent**) SAU_PtrList_ITEMS(&o->ev_list);
		events = calloc(ev_count, sizeof(SAU_ProgramEvent));
		if (!events) goto ERROR;
		for (size_t i = 0; i < ev_count; ++i) {
			events[i] = *in_events[i];
		}
		o->ev_list.count = 0; // items used, don't destroy them
	}
	prg = SAU_MemPool_alloc(o->mem, sizeof(SAU_Program));
	if (!prg) goto ERROR;
	prg->events = events;
	prg->ev_count = ev_count;
	if (!(script->sopt.changed & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			script->name, SAU_PVO_MAX_ID);
		goto ERROR;
	}
	prg->vo_count = o->va.count;
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			script->name, SAU_POP_MAX_ID);
		goto ERROR;
	}
	prg->op_count = o->oa.count;
	if (o->ev_vo_graph.op_nest_max > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %d levels, maximum is %d levels\n",
			script->name, o->ev_vo_graph.op_nest_max, UINT8_MAX);
		goto ERROR;
	}
	prg->op_nest_depth = o->ev_vo_graph.op_nest_max;
	prg->duration_ms = o->duration_ms;
	prg->name = script->name;
	prg->mem = o->mem;
	o->mem = NULL; // pass on to program
	return prg;
ERROR:
	free(events);
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program *ScriptConv_convert(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = NULL;
	o->mem = SAU_create_MemPool(0);
	if (!o->mem) goto EXIT;
	o->tmp = SAU_create_MemPool(0);
	if (!o->tmp) goto EXIT;
	SAU_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa, o->mem);

	uint32_t remaining_ms = 0;
	for (SAU_ScriptEvData *e = script->events; e; e = e->next) {
		ScriptConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;

	prg = _ScriptConv_copy_out(o, script);
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
	OpDataArr_clear(&o->ev_op_data);
	SAU_PtrList_clear(&o->ev_list);
EXIT:
	SAU_fini_VoiceGraph(&o->ev_vo_graph);
	SAU_destroy_MemPool(o->mem);
	SAU_destroy_MemPool(o->tmp);
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

/**
 * Destroy instance.
 */
void SAU_discard_Program(SAU_Program *restrict o) {
	if (!o)
		return;
	free(o->events);
	SAU_destroy_MemPool(o->mem);
}

static void print_linked(const char *restrict header,
		const char *restrict footer,
		const SAU_ProgramOpList *restrict list) {
	if (!list || !list->count)
		return;
	fprintf(stdout, "%s%d", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %d", list->ids[i]);
	fprintf(stdout, "%s", footer);
}

static void print_graph(const SAU_ProgramOpRef *restrict graph,
		uint32_t count) {
	static const char *const uses[SAU_POP_USES] = {
		"CA",
		"FM",
		"PM",
		"AM"
	};
	if (!graph)
		return;

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", stdout);
	for (;;) {
		const uint32_t indent = graph[i].level * 2;
		if (indent > max_indent) max_indent = indent;
		fprintf(stdout, "%6d:  ", graph[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', stdout);
		fputs(uses[graph[i].use], stdout);
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
		const SAU_ProgramEvent *ev = &o->events[ev_id];
		const SAU_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"\\%d \tEV %zd \t(VO %hd)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			fprintf(stdout,
				"\n\tvo %d", ev->vo_id);
			print_graph(vd->graph, vd->graph_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SAU_ProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    f~[", "]", od->fmods);
			print_linked("\n\t    p+[", "]", od->pmods);
			print_linked("\n\t    a~[", "]", od->amods);
		}
		putc('\n', stdout);
	}
}
