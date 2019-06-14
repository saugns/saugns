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

#include "../script.h"
#include "../program.h"
#include "../arrtype.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static SGS_ProgramOpGraph
*create_OpGraph(const SGS_ScriptEvData *restrict vo_in) {
	uint32_t size;
	size = vo_in->op_graph.count;
	if (!size) return NULL;

	const SGS_ScriptOpData **ops;
	uint32_t i;
	ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&vo_in->op_graph);
	SGS_ProgramOpGraph *o;
	o = malloc(sizeof(SGS_ProgramOpGraph) + sizeof(int32_t) * (size - 1));
	if (!o) return NULL;
	o->opc = size;
	for (i = 0; i < size; ++i) {
		o->ops[i] = ops[i]->op_id;
	}
	return o;
}

static SGS_ProgramOpAdjcs
*create_OpAdjcs(const SGS_ScriptOpData *restrict op_in) {
	uint32_t size;
	size = op_in->fmods.count +
		op_in->pmods.count +
		op_in->amods.count;
	if (!size) return NULL;

	const SGS_ScriptOpData **ops;
	uint32_t i;
	uint32_t *data;
	SGS_ProgramOpAdjcs *o;
	o = malloc(sizeof(SGS_ProgramOpAdjcs) + sizeof(int32_t) * (size - 1));
	if (!o) return NULL;
	o->fmodc = op_in->fmods.count;
	o->pmodc = op_in->pmods.count;
	o->amodc = op_in->amods.count;
	data = o->adjcs;
	ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op_in->fmods);
	for (i = 0; i < o->fmodc; ++i)
		*data++ = ops[i]->op_id;
	ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op_in->pmods);
	for (i = 0; i < o->pmodc; ++i)
		*data++ = ops[i]->op_id;
	ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op_in->amods);
	for (i = 0; i < o->amodc; ++i)
		*data++ = ops[i]->op_id;
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
	SGS_ScriptEvData *last_ev;
	SGS_ProgramOpGraph *op_graph;
	uint32_t flags;
	uint32_t duration_ms;
} VAState;

SGS_DEF_ArrType(VoAlloc, VAState, _)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(const SGS_ScriptEvData *restrict ve) {
	SGS_ScriptOpData **ops;
	uint32_t duration_ms = 0;
	/* FIXME: node list type? */
	ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&ve->operators);
	for (size_t i = 0; i < ve->operators.count; ++i) {
		SGS_ScriptOpData *op = ops[i];
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
static bool VoAlloc_get_id(VoAlloc *restrict va,
		const SGS_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->voice_prev != NULL) {
		*vo_id = e->voice_prev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		VAState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SGS_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			if (vas->op_graph != NULL) free(vas->op_graph);
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
		SGS_ScriptEvData *restrict e) {
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
	if ((e->ev_flags & SGS_SDEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = voice_duration(e);
	return vo_id;
}

/*
 * Clear voice allocator.
 */
static void VoAlloc_clear(VoAlloc *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		VAState *vas = &o->a[i];
		if (vas->op_graph != NULL) free(vas->op_graph);
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
	SGS_ScriptOpData *last_pod;
	SGS_ProgramOpAdjcs *adjcs;
	uint32_t flags;
	//uint32_t duration_ms;
} OAState;

SGS_DEF_ArrType(OpAlloc, OAState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true if operator found, false if operator added or recycled
 */
static bool OpAlloc_get_id(OpAlloc *restrict oa,
		const SGS_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->on_prev != NULL) {
		*op_id = od->on_prev->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[op_id].last_pod->op_flags & SGS_SDOP_LATER_USED)
//			&& oa->a[op_id].duration_ms == 0) {
//			oa->a[id] = (OAState){0};
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
		SGS_ScriptOpData *restrict od) {
//	SGS_ScriptEvData *e = od->event;
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
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return op_id;
}

/*
 * Clear operator allocator.
 */
static void OpAlloc_clear(OpAlloc *restrict o) {
	_OpAlloc_clear(o);
}

SGS_DEF_ArrType(OpRefArr, SGS_ProgramOpRef, )
SGS_DEF_ArrType(OpDataArr, SGS_ProgramOpData, )

typedef struct ParseConv {
	SGS_PtrList ev_list;
	VoAlloc va;
	OpAlloc oa;
	SGS_ProgramEvent *ev;
	OpRefArr ev_vo_oplist;
	OpDataArr ev_op_data;
	uint32_t op_nest_depth;
	uint32_t duration_ms;
} ParseConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 */
static void ParseConv_convert_opdata(ParseConv *restrict o,
		const SGS_ScriptOpData *restrict op, uint32_t op_id) {
	OAState *oas = &o->oa.a[op_id];
	SGS_ProgramOpData ood = {0};
	ood.id = op_id;
	ood.params = op->op_params;
	ood.adjcs = NULL;
	ood.attr = op->attr;
	ood.wave = op->wave;
	ood.time = op->time;
	ood.silence_ms = op->silence_ms;
	ood.freq = op->freq;
	ood.dynfreq = op->dynfreq;
	ood.phase = op->phase;
	ood.amp = op->amp;
	ood.dynamp = op->dynamp;
	ood.ramp_freq = op->ramp_freq;
	ood.ramp_amp = op->ramp_amp;
	if ((op->op_params & SGS_POPP_ADJCS) != 0) {
		VAState *vas = &o->va.a[o->ev->vo_id];
		vas->flags |= VA_OPLIST;
		oas->adjcs = create_OpAdjcs(op);
		ood.adjcs = oas->adjcs;
	}
	OpDataArr_add(&o->ev_op_data, &ood);
}

/*
 * Visit each operator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * operator data.
 */
static void ParseConv_convert_ops(ParseConv *restrict o,
		SGS_PtrList *restrict op_list) {
	SGS_ScriptOpData **ops;
	uint32_t i;
	ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(op_list);
	for (i = op_list->old_count; i < op_list->count; ++i) {
		SGS_ScriptOpData *op = ops[i];
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SGS_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id = OpAlloc_update(&o->oa, op);
		ParseConv_convert_ops(o, &op->fmods);
		ParseConv_convert_ops(o, &op->pmods);
		ParseConv_convert_ops(o, &op->amods);
		ParseConv_convert_opdata(o, op, op_id);
	}
}

/*
 * Traverse voice operator graph built during allocation,
 * assigning block IDs.
 */
static void ParseConv_traverse_ops(ParseConv *restrict o,
		SGS_ProgramOpRef *restrict op_ref, uint32_t level) {
	OAState *oas = &o->oa.a[op_ref->id];
	uint32_t i;
	if ((oas->flags & OA_VISITED) != 0) {
		SGS_warning("parseconv",
"skipping operator %d; circular references unsupported",
			op_ref->id);
		return;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	op_ref->level = level++;
	if (oas->adjcs != NULL) {
		SGS_ProgramOpRef mod_op_ref;
		const SGS_ProgramOpAdjcs *adjcs = oas->adjcs;
		const uint32_t *mods = oas->adjcs->adjcs;
		uint32_t modc = 0;
		oas->flags |= OA_VISITED;
		i = 0;
		modc += adjcs->fmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_FMOD;
//			fprintf(stderr, "visit fmod node %d\n", mod_op_ref.id);
			ParseConv_traverse_ops(o, &mod_op_ref, level);
		}
		modc += adjcs->pmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_PMOD;
//			fprintf(stderr, "visit pmod node %d\n", mod_op_ref.id);
			ParseConv_traverse_ops(o, &mod_op_ref, level);
		}
		modc += adjcs->amodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_AMOD;
//			fprintf(stderr, "visit amod node %d\n", mod_op_ref.id);
			ParseConv_traverse_ops(o, &mod_op_ref, level);
		}
		oas->flags &= ~OA_VISITED;
	}
	OpRefArr_add(&o->ev_vo_oplist, op_ref);
}

/*
 * Traverse operator graph for voice built during allocation,
 * assigning an operator reference list to the voice and
 * block IDs to the operators.
 */
static void ParseConv_traverse_voice(ParseConv *restrict o,
		const SGS_ProgramEvent *restrict ev) {
	SGS_ProgramOpRef op_ref = {0, SGS_POP_CARR, 0};
	VAState *vas = &o->va.a[ev->vo_id];
	SGS_ProgramVoData *vd = (SGS_ProgramVoData*) ev->vo_data;
	const SGS_ProgramOpGraph *graph = vas->op_graph;
	uint32_t i;
	if (!graph) return;
	for (i = 0; i < graph->opc; ++i) {
		op_ref.id = graph->ops[i];
//		fprintf(stderr, "visit node %d\n", op_ref.id);
		ParseConv_traverse_ops(o, &op_ref, 0);
	}
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
static void ParseConv_convert_event(ParseConv *restrict o,
		SGS_ScriptEvData *restrict e) {
	uint32_t vo_id = VoAlloc_update(&o->va, e);
	uint32_t vo_params;
	VAState *vas = &o->va.a[vo_id];
	SGS_ProgramEvent *out_ev = calloc(1, sizeof(SGS_ProgramEvent));
	SGS_PtrList_add(&o->ev_list, out_ev);
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	ParseConv_convert_ops(o, &e->operators);
	if (o->ev_op_data.count > 0) {
		OpDataArr_memdup(&o->ev_op_data, &out_ev->op_data);
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if ((e->ev_flags & SGS_SDEV_NEW_OPGRAPH) != 0)
		vas->flags |= VA_OPLIST;
	if ((vas->flags & VA_OPLIST) != 0)
		vo_params |= SGS_PVOP_OPLIST;
	if (vo_params != 0) {
		SGS_ProgramVoData *ovd = calloc(1, sizeof(SGS_ProgramVoData));
		ovd->params = vo_params;
		ovd->attr = e->vo_attr;
		ovd->pan = e->pan;
		ovd->ramp_pan = e->ramp_pan;
		if ((e->ev_flags & SGS_SDEV_NEW_OPGRAPH) != 0) {
			if (vas->op_graph != NULL) free(vas->op_graph);
			vas->op_graph = create_OpGraph(e);
		}
		out_ev->vo_data = ovd;
		if ((vas->flags & VA_OPLIST) != 0) {
			ParseConv_traverse_voice(o, out_ev);
		}
	}
}

static void Program_destroy_event_data(SGS_ProgramEvent *restrict e);

static SGS_Program *_ParseConv_copy_out(ParseConv *restrict o,
		SGS_Script *restrict parse) {
	SGS_Program *prg = NULL;
	SGS_ProgramEvent *events = NULL;
	size_t i, ev_count;
	ev_count = o->ev_list.count;
	if (ev_count > 0) {
		SGS_ProgramEvent **in_events;
		in_events = (SGS_ProgramEvent**) SGS_PtrList_ITEMS(&o->ev_list);
		events = calloc(ev_count, sizeof(SGS_ProgramEvent));
		if (!events) goto ERROR;
		for (i = 0; i < ev_count; ++i) {
			events[i] = *in_events[i];
			free(in_events[i]);
		}
		o->ev_list.count = 0; // items used, don't destroy them
	}
	prg = calloc(1, sizeof(SGS_Program));
	if (!prg) goto ERROR;
	prg->events = events;
	prg->ev_count = ev_count;
	if (!(parse->sopt.changed & SGS_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SGS_PMODE_AMP_DIV_VOICES;
	}
	if (o->va.count > SGS_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			parse->name, SGS_PVO_MAX_ID);
		goto ERROR;
	}
	prg->vo_count = o->va.count;
	if (o->oa.count > SGS_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			parse->name, SGS_POP_MAX_ID);
		goto ERROR;
	}
	prg->op_count = o->oa.count;
	if (o->op_nest_depth > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %d levels, maximum is %d levels\n",
			parse->name, o->op_nest_depth, UINT8_MAX);
		goto ERROR;
	}
	prg->op_nest_depth = o->op_nest_depth;
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
	OpAlloc_clear(&o->oa);
	VoAlloc_clear(&o->va);
	OpRefArr_clear(&o->ev_vo_oplist);
	OpDataArr_clear(&o->ev_op_data);
	if (o->ev_list.count > 0) {
		SGS_ProgramEvent **in_events;
		in_events = (SGS_ProgramEvent**) SGS_PtrList_ITEMS(&o->ev_list);
		for (i = 0; i < o->ev_list.count; ++i) {
			Program_destroy_event_data(in_events[i]);
			free(in_events[i]);
		}
	}
	SGS_PtrList_clear(&o->ev_list);
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

	for (e = parse->events; e; e = e->next) {
		ParseConv_convert_event(o, e);
		o->duration_ms += e->wait_ms;
	}
	for (i = 0; i < o->va.count; ++i) {
		VAState *vas = &o->va.a[i];
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

/*
 * Destroy data stored for event. Does not free the event itself.
 */
static void Program_destroy_event_data(SGS_ProgramEvent *restrict e) {
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
}

/**
 * Destroy instance.
 */
void SGS_discard_Program(SGS_Program *restrict o) {
	if (o->events != NULL) {
		for (size_t i = 0; i < o->ev_count; ++i) {
			SGS_ProgramEvent *e = &o->events[i];
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
	if (!count) return;
	fprintf(stdout, "%s%d", header, nodes[0]);
	for (i = 0; ++i < count; )
		fprintf(stdout, ", %d", nodes[i]);
	fprintf(stdout, "%s", footer);
}

static void print_oplist(const SGS_ProgramOpRef *restrict list,
		uint32_t count) {
	static const char *const uses[SGS_POP_USES] = {
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

/**
 * Print information about program contents. Useful for debugging.
 */
void SGS_Program_print_info(const SGS_Program *restrict o) {
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
		const SGS_ProgramEvent *ev = &o->events[ev_id];
		const SGS_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"\\%d \tEV %zd \t(VO %hd)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			const SGS_ProgramOpRef *ol = vd->op_list;
			fprintf(stdout,
				"\n\tvo %d", ev->vo_id);
			if (ol != NULL)
				print_oplist(ol, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SGS_ProgramOpData *od = &ev->op_data[i];
			const SGS_ProgramOpAdjcs *ga = od->adjcs;
			if (od->time.flags & SGS_TIMEP_IMPLICIT)
				fprintf(stdout,
					"\n\top %d \tt=IMPL\tf=%.f",
					od->id, od->freq);
			else
				fprintf(stdout,
					"\n\top %d \tt=%d \tf=%.f",
					od->id, od->time.v_ms, od->freq);
			if (ga != NULL) {
				print_linked("\n\t    f![", "]", ga->fmodc,
					ga->adjcs);
				print_linked("\n\t    p+[", "]", ga->pmodc,
					&ga->adjcs[ga->fmodc]);
				print_linked("\n\t    a![", "]", ga->amodc,
					&ga->adjcs[ga->fmodc + ga->pmodc]);
			}
		}
		putc('\n', stdout);
	}
}
