/* saugns: Parse result to audio program converter.
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

#include "parseconv.h"
#include "../ptrlist.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static SAU_ProgramOpList blank_oplist = {0};
#define free_nonblank(mem) do{ \
	if ((mem) != &blank_oplist) free((mem)); \
} while(0)

static sauNoinline SAU_ProgramOpList
*create_OpList(const SAU_ScriptListData *restrict list_in) {
	uint32_t count = 0;
	const SAU_ScriptOpData *op;
	for (op = list_in->first_item; op != NULL; op = op->next_item) {
		++count;
	}
	if (!count)
		return &blank_oplist;
	SAU_ProgramOpList *o;
	o = malloc(sizeof(SAU_ProgramOpList) + sizeof(uint32_t) * count);
	if (!o)
		return NULL;
	o->count = count;
	uint32_t i = 0;
	for (op = list_in->first_item; op != NULL; op = op->next_item) {
		o->ids[i++] = op->obj->op_id;
	}
	return o;
}

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SAU_ScriptEvData *restrict ve) {
	uint32_t duration_ms = 0;
	for (const SAU_ScriptOpData *op = ve->op_objs.first_item;
			op != NULL; op = op->next_item) {
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
static bool SAU_VoAlloc_get_id(SAU_VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->root_ev != NULL) {
		*vo_id = e->root_ev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SAU_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			free_nonblank(vas->op_carrs);
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
	if (!e->root_ev)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static void SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		SAU_VoAllocState *vas = &o->a[i];
		free_nonblank(vas->op_carrs);
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
	if (od->on_prev != NULL) {
		*op_id = od->obj->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[op_id].last_pod->op_flags & SAU_SDOP_LATER_USED)
//			&& oa->a[op_id].duration_ms == 0) {
//			oa->a[id] = (SAU_OpAllocState){0};
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
	od->obj->op_id = op_id;
	SAU_OpAllocState *oas = &oa->a[op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static void SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

SAU_DEF_ArrType(OpDataArr, SAU_ProgramOpData, )

typedef struct ParseConv {
	SAU_PtrList ev_list;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	SAU_VoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
} ParseConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 */
static void ParseConv_convert_opdata(ParseConv *restrict o,
		const SAU_ScriptOpData *restrict op, uint32_t op_id) {
	SAU_OpAllocState *oas = &o->oa.a[op_id];
	SAU_ProgramOpData ood = {0};
	ood.id = op_id;
	ood.params = op->params;
	/* ...mods */
	ood.time = op->time;
	ood.silence_ms = op->silence_ms;
	ood.wave = op->wave;
	ood.freq = op->freq;
	ood.freq2 = op->freq2;
	ood.amp = op->amp;
	ood.amp2 = op->amp2;
	ood.pan = op->pan;
	ood.phase = op->phase;
	SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	for (SAU_ScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->next_list) {
		vas->flags |= SAU_VAS_GRAPH;
		SAU_ProgramOpList *list = create_OpList(in_list);
		switch (in_list->use_type) {
		case SAU_POP_FMOD:
			free_nonblank((void*) ood.fmods);
			ood.fmods = oas->fmods = list;
			break;
		case SAU_POP_PMOD:
			free_nonblank((void*) ood.pmods);
			ood.pmods = oas->pmods = list;
			break;
		case SAU_POP_AMOD:
			free_nonblank((void*) ood.amods);
			ood.amods = oas->amods = list;
			break;
		}
	}
	OpDataArr_add(&o->ev_op_data, &ood);
}

/*
 * Visit each operator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * operator data.
 */
static void ParseConv_convert_ops(ParseConv *restrict o,
		SAU_ScriptListData *restrict op_list) {
	if (!op_list)
		return;
	for (SAU_ScriptOpData *op = op_list->first_item;
			op != NULL; op = op->next_item) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id = SAU_OpAlloc_update(&o->oa, op);
		for (SAU_ScriptListData *in_list = op->mods;
				in_list != NULL; in_list = in_list->next_list) {
			ParseConv_convert_ops(o, in_list);
		}
		ParseConv_convert_opdata(o, op, op_id);
	}
}

/*
 * Convert all voice and operator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 */
static void ParseConv_convert_event(ParseConv *restrict o,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id = SAU_VoAlloc_update(&o->va, e);
	SAU_VoAllocState *vas = &o->va.a[vo_id];
	SAU_ProgramEvent *out_ev = calloc(1, sizeof(SAU_ProgramEvent));
	SAU_PtrList_add(&o->ev_list, out_ev);
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	ParseConv_convert_ops(o, &e->op_objs);
	if (o->ev_op_data.count > 0) {
		OpDataArr_memdup(&o->ev_op_data, &out_ev->op_data);
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (!e->root_ev)
		vas->flags |= SAU_VAS_GRAPH;
	if ((vas->flags & SAU_VAS_GRAPH) != 0) {
		SAU_ProgramVoData *ovd = calloc(1, sizeof(SAU_ProgramVoData));
		ovd->params = SAU_PVOP_GRAPH;
		if (!e->root_ev) {
			free_nonblank(vas->op_carrs);
			vas->op_carrs = create_OpList(&e->op_objs);
		}
		out_ev->vo_data = ovd;
		if ((vas->flags & SAU_VAS_GRAPH) != 0) {
			SAU_VoiceGraph_set(&o->ev_vo_graph, out_ev);
		}
	}
}

static void Program_destroy_event_data(SAU_ProgramEvent *restrict e);

static SAU_Program *_ParseConv_copy_out(ParseConv *restrict o,
		SAU_Script *restrict parse) {
	SAU_Program *prg = NULL;
	SAU_ProgramEvent *events = NULL;
	size_t i, ev_count;
	ev_count = o->ev_list.count;
	if (ev_count > 0) {
		SAU_ProgramEvent **in_events;
		in_events = (SAU_ProgramEvent**) SAU_PtrList_ITEMS(&o->ev_list);
		events = calloc(ev_count, sizeof(SAU_ProgramEvent));
		if (!events) goto ERROR;
		for (i = 0; i < ev_count; ++i) {
			events[i] = *in_events[i];
			free(in_events[i]);
		}
		o->ev_list.count = 0; // items used, don't destroy them
	}
	prg = calloc(1, sizeof(SAU_Program));
	if (!prg) goto ERROR;
	prg->events = events;
	prg->ev_count = ev_count;
	if (!(parse->sopt.set & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			parse->name, SAU_PVO_MAX_ID);
		goto ERROR;
	}
	prg->vo_count = o->va.count;
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			parse->name, SAU_POP_MAX_ID);
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
	return prg;
ERROR:
	if (events != NULL) free(events);
	if (prg != NULL) free(prg);
	return NULL;
}

static void _ParseConv_cleanup(ParseConv *restrict o) {
	size_t i;
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
	SAU_fini_VoiceGraph(&o->ev_vo_graph);
	OpDataArr_clear(&o->ev_op_data);
	if (o->ev_list.count > 0) {
		SAU_ProgramEvent **in_events;
		in_events = (SAU_ProgramEvent**) SAU_PtrList_ITEMS(&o->ev_list);
		for (i = 0; i < o->ev_list.count; ++i) {
			Program_destroy_event_data(in_events[i]);
			free(in_events[i]);
		}
	}
	SAU_PtrList_clear(&o->ev_list);
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program *ParseConv_convert(ParseConv *restrict o,
		SAU_Script *restrict parse) {
	SAU_Program *prg;
	SAU_ScriptEvData *e;
	size_t i;
	uint32_t remaining_ms = 0;

	SAU_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa);
	for (e = parse->events; e; e = e->next) {
		ParseConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
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
SAU_Program* SAU_build_Program(SAU_Script *restrict sd) {
	ParseConv pc = (ParseConv){0};
	SAU_Program *o = ParseConv_convert(&pc, sd);
	return o;
}

/*
 * Destroy data stored for event. Does not free the event itself.
 */
static void Program_destroy_event_data(SAU_ProgramEvent *restrict e) {
	if (e->vo_data != NULL) {
		free((void*)e->vo_data->graph);
		free((void*)e->vo_data);
	}
	if (e->op_data != NULL) {
		for (size_t i = 0; i < e->op_data_count; ++i) {
			free_nonblank((void*)e->op_data[i].fmods);
			free_nonblank((void*)e->op_data[i].pmods);
			free_nonblank((void*)e->op_data[i].amods);
		}
		free((void*)e->op_data);
	}
}

/**
 * Destroy instance.
 */
void SAU_discard_Program(SAU_Program *restrict o) {
	if (!o)
		return;
	if (o->events != NULL) {
		for (size_t i = 0; i < o->ev_count; ++i) {
			SAU_ProgramEvent *e = &o->events[i];
			Program_destroy_event_data(e);
		}
		free(o->events);
	}
	free(o);
}

static sauNoinline void print_linked(const char *restrict header,
		const char *restrict footer,
		const SAU_ProgramOpList *restrict list) {
	if (!list || !list->count)
		return;
	fprintf(stdout, "%s%u", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %u", list->ids[i]);
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
		fprintf(stdout, "%6u:  ", graph[i].id);
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
	if (od->time.flags & SAU_TIMEP_LINKED) {
		fprintf(stdout,
			"\n\top %u \tt=INF   \t", od->id);
	} else {
		fprintf(stdout,
			"\n\top %u \tt=%-6u\t", od->id, od->time.v_ms);
	}
	if ((od->freq.flags & SAU_RAMPP_STATE) != 0) {
		if ((od->freq.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f=%-6.1f->%-6.1f", od->freq.v0, od->freq.vt);
		else
			fprintf(stdout,
				"f=%-6.1f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f->%-6.1f\t", od->freq.vt);
		else
			fprintf(stdout,
				"\t\t");
	}
	if ((od->amp.flags & SAU_RAMPP_STATE) != 0) {
		if ((od->amp.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"\ta=%-6.1f->%-6.1f", od->amp.v0, od->amp.vt);
		else
			fprintf(stdout,
				"\ta=%-6.1f", od->amp.v0);
	} else if ((od->amp.flags & SAU_RAMPP_GOAL) != 0) {
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
		"\tDuration: \t%u ms\n"
		"\tEvents:   \t%zu\n"
		"\tVoices:   \t%hu\n"
		"\tOperators:\t%u\n",
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const SAU_ProgramEvent *ev = &o->events[ev_id];
		const SAU_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"\\%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			fprintf(stdout,
				"\n\tvo %u", ev->vo_id);
			print_graph(vd->graph, vd->op_count);
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
