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

#include "../script.h"
#include "../program.h"
#include "../ptrlist.h"
#include "../arrtype.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from script data.
 *
 * Allocation of events, voices, operators.
 */

static sauNoinline SAU_ProgramOpList
*create_ProgramOpList(const SAU_NodeList *restrict op_list) {
	uint32_t count = 0;
	for (SAU_NodeRef *ref = op_list->refs; ref != NULL; ref = ref->next)
		++count;
	if (count == 0)
		return NULL;
	SAU_ProgramOpList *o;
	o = malloc(sizeof(SAU_ProgramOpList) + sizeof(int32_t) * count);
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
 * Voice allocation state flags.
 */
enum {
	VA_OPLIST = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct VAState {
	SAU_ScriptEvData *last_ev;
	SAU_ProgramOpList *op_graph;
	uint32_t flags;
	uint32_t duration_ms;
} VAState;

sauArrType(VoAlloc, VAState, _)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SAU_ScriptEvData *restrict ve) {
	uint32_t duration_ms = 0;
	for (SAU_NodeRef *ref = ve->op_graph->refs;
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
static bool VoAlloc_get_id(VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->vo_prev != NULL) {
		*vo_id = e->vo_prev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		VAState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			free(vas->op_graph);
			*vas = (VAState){0};
			*vo_id = id;
			return false;
		}
	}
	*vo_id = va->count;
	_VoAlloc_add(va, NULL);
	return false;
}

/*
 * Update voices for event and return a voice ID for the event.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 */
static uint32_t VoAlloc_update(VoAlloc *restrict va,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id;
	for (vo_id = 0; vo_id < va->count; ++vo_id) {
		if (va->a[vo_id].duration_ms < e->wait_ms)
			va->a[vo_id].duration_ms = 0;
		else
			va->a[vo_id].duration_ms -= e->wait_ms;
	}
	VoAlloc_get_id(va, e, &vo_id);
	e->vo_id = vo_id;
	VAState *vas = &va->a[vo_id];
	vas->last_ev = e;
	vas->flags &= ~VA_OPLIST;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static void VoAlloc_clear(VoAlloc *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		VAState *vas = &o->a[i];
		free(vas->op_graph);
	}
	_VoAlloc_clear(o);
}

/*
 * Operator allocation state flags.
 */
enum {
	OA_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct OAState {
	SAU_ScriptOpData *last_sod;
	SAU_ProgramOpList *fmods;
	SAU_ProgramOpList *pmods;
	SAU_ProgramOpList *amods;
	uint32_t flags;
	//uint32_t duration_ms;
} OAState;

sauArrType(OpAlloc, OAState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true if operator found, false if operator added or recycled
 */
static bool OpAlloc_get_id(OpAlloc *restrict oa,
		const SAU_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->op_prev != NULL) {
		*op_id = od->op_prev->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		OAState *oas = &oa->a[id];
//		if (!(oas->last_sod->op_flags & SAU_SDOP_LATER_USED)
//			&& oas->duration_ms == 0) {
//			//free(oas->fmods);
//			//free(oas->pmods);
//			//free(oas->amods);
//			*oas = (OAState){0};
//			*op_id = id;
//			return false;
//		}
//	}
	*op_id = oa->count;
	_OpAlloc_add(oa, NULL);
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
static uint32_t OpAlloc_update(OpAlloc *restrict oa,
		SAU_ScriptOpData *restrict od) {
//	SAU_ScriptEvData *e = od->event;
	uint32_t op_id;
//	for (op_id = 0; op_id < oa->count; ++op_id) {
//		if (oa->a[op_id].duration_ms < e->wait_ms)
//			oa->a[op_id].duration_ms = 0;
//		else
//			oa->a[op_id].duration_ms -= e->wait_ms;
//	}
	OpAlloc_get_id(oa, od, &op_id);
	od->op_id = op_id;
	OAState *oas = &oa->a[op_id];
	oas->last_sod = od;
//	oas->duration_ms = od->time_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static void OpAlloc_clear(OpAlloc *restrict o) {
	_OpAlloc_clear(o);
}

sauArrType(OpRefArr, SAU_ProgramOpRef, )
sauArrType(OpDataArr, SAU_ProgramOpData, )

typedef struct ScriptConv {
	SAU_PtrList ev_list;
	VoAlloc va;
	OpAlloc oa;
	SAU_ProgramEvent *ev;
	OpRefArr ev_vo_oplist;
	OpDataArr ev_op_data;
	uint32_t op_nest_level;
	uint32_t op_nest_max;
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
static inline bool need_new_oplist(const SAU_NodeList *restrict op_list,
		const SAU_ProgramOpList *restrict prev_pol) {
	if (!op_list)
		return false;
	return (op_list->new_refs != NULL) ||
		(prev_pol != NULL && !op_list->refs);
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
		uint32_t op_id = OpAlloc_update(&o->oa, op);
		ScriptConv_convert_opdata(o, op, op_id);
	}
	for (size_t i = 0; i < o->ev_op_data.count; ++i) {
		SAU_ProgramOpData *od = &o->ev_op_data.a[i];
		VAState *vas = &o->va.a[o->ev->vo_id];
		OAState *oas = &o->oa.a[od->id];
		SAU_ScriptOpData *sod = oas->last_sod;
		if (need_new_oplist(sod->fmods, oas->fmods)) {
			vas->flags |= VA_OPLIST;
			oas->fmods = create_ProgramOpList(sod->fmods);
			od->fmods = oas->fmods;
		}
		if (need_new_oplist(sod->pmods, oas->pmods)) {
			vas->flags |= VA_OPLIST;
			oas->pmods = create_ProgramOpList(sod->pmods);
			od->pmods = oas->pmods;
		}
		if (need_new_oplist(sod->amods, oas->amods)) {
			vas->flags |= VA_OPLIST;
			oas->amods = create_ProgramOpList(sod->amods);
			od->amods = oas->amods;
		}
	}
}

static void ScriptConv_traverse_op_node(ScriptConv *restrict o,
		SAU_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 */
static void ScriptConv_traverse_op_list(ScriptConv *restrict o,
		const SAU_ProgramOpList *restrict op_list, uint8_t mod_use) {
	SAU_ProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		ScriptConv_traverse_op_node(o, &op_ref);
	}
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 */
static void ScriptConv_traverse_op_node(ScriptConv *restrict o,
		SAU_ProgramOpRef *restrict op_ref) {
	OAState *oas = &o->oa.a[op_ref->id];
	if (oas->flags & OA_VISITED) {
		SAU_warning("scriptconv",
"skipping operator %d; circular references unsupported",
			op_ref->id);
		return;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= OA_VISITED;
	if (oas->fmods != NULL)
		ScriptConv_traverse_op_list(o, oas->fmods, SAU_POP_FMOD);
	if (oas->pmods != NULL)
		ScriptConv_traverse_op_list(o, oas->pmods, SAU_POP_PMOD);
	if (oas->amods != NULL)
		ScriptConv_traverse_op_list(o, oas->amods, SAU_POP_AMOD);
	oas->flags &= ~OA_VISITED;
	--o->op_nest_level;
	OpRefArr_add(&o->ev_vo_oplist, op_ref);
}

/*
 * Traverse operator graph for voice built during allocation,
 * assigning an operator reference list to the voice and
 * block IDs to the operators.
 */
static void ScriptConv_traverse_voice(ScriptConv *restrict o,
		const SAU_ProgramEvent *restrict ev) {
	VAState *vas = &o->va.a[ev->vo_id];
	if (!vas->op_graph)
		return;
	ScriptConv_traverse_op_list(o, vas->op_graph, SAU_POP_CARR);
	SAU_ProgramVoData *vd = (SAU_ProgramVoData*) ev->vo_data;
	OpRefArr_memdup(&o->ev_vo_oplist, &vd->op_list);
	vd->op_count = o->ev_vo_oplist.count;
	o->ev_vo_oplist.count = 0; // reuse allocation
}

/*
 * Convert all voice and operator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 */
static void ScriptConv_convert_event(ScriptConv *restrict o,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id = VoAlloc_update(&o->va, e);
	uint32_t vo_params;
	VAState *vas = &o->va.a[vo_id];
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
		vas->flags |= VA_OPLIST;
	if (vas->flags & VA_OPLIST)
		vo_params |= SAU_PVOP_OPLIST;
	if (vo_params != 0) {
		SAU_ProgramVoData *ovd = calloc(1, sizeof(SAU_ProgramVoData));
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
			free(vas->op_graph);
			vas->op_graph = create_ProgramOpList(e->op_graph);
		}
		out_ev->vo_data = ovd;
		if (vas->flags & VA_OPLIST) {
			ScriptConv_traverse_voice(o, out_ev);
		}
	}
}

static void Program_destroy_event_data(SAU_ProgramEvent *restrict e);

static SAU_Program *_ScriptConv_copy_out(ScriptConv *restrict o,
		SAU_Script *restrict parse) {
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
			free(in_events[i]);
		}
		o->ev_list.count = 0; // items used, don't destroy them
	}
	prg = calloc(1, sizeof(SAU_Program));
	if (!prg) goto ERROR;
	prg->events = events;
	prg->ev_count = ev_count;
	if (!(parse->sopt.changed & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			parse->name, SAU_PVO_MAX_ID);
		goto ERROR;
	}
	prg->vo_count = o->va.count;
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			parse->name, SAU_POP_MAX_ID);
		goto ERROR;
	}
	prg->op_count = o->oa.count;
	if (o->op_nest_max > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %d levels, maximum is %d levels\n",
			parse->name, o->op_nest_max, UINT8_MAX);
		goto ERROR;
	}
	prg->op_nest_depth = o->op_nest_max;
	prg->duration_ms = o->duration_ms;
	prg->name = parse->name;
	return prg;
ERROR:
	free(events);
	free(prg);
	return NULL;
}

static void _ScriptConv_cleanup(ScriptConv *restrict o) {
	OpAlloc_clear(&o->oa);
	VoAlloc_clear(&o->va);
	OpRefArr_clear(&o->ev_vo_oplist);
	OpDataArr_clear(&o->ev_op_data);
	if (o->ev_list.count > 0) {
		SAU_ProgramEvent **in_events;
		in_events = (SAU_ProgramEvent**) SAU_PtrList_ITEMS(&o->ev_list);
		for (size_t i = 0; i < o->ev_list.count; ++i) {
			Program_destroy_event_data(in_events[i]);
			free(in_events[i]);
		}
	}
	SAU_PtrList_clear(&o->ev_list);
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program *ScriptConv_convert(ScriptConv *restrict o,
		SAU_Script *restrict parse) {
	SAU_Program *prg;
	uint32_t remaining_ms = 0;
	for (SAU_ScriptEvData *e = parse->events; e; e = e->next) {
		ScriptConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		VAState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;

	prg = _ScriptConv_copy_out(o, parse);
	_ScriptConv_cleanup(o);
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
			free((void*)e->op_data[i].fmods);
			free((void*)e->op_data[i].pmods);
			free((void*)e->op_data[i].amods);
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

static void print_linked(const char *restrict header,
		const char *restrict footer,
		const SAU_ProgramOpList *restrict list) {
	if (!list->count)
		return;
	fprintf(stdout, "%s%d", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %d", list->ids[i]);
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
		const SAU_ProgramEvent *ev = &o->events[ev_id];
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
			print_opline(od);
			if (od->fmods != NULL)
				print_linked("\n\t    f~[", "]", od->fmods);
			if (od->pmods != NULL)
				print_linked("\n\t    p+[", "]", od->pmods);
			if (od->amods != NULL)
				print_linked("\n\t    a~[", "]", od->amods);
		}
		putc('\n', stdout);
	}
}
