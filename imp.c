/* sgensys: Intermediate program module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "imp.h"
#include "parser.h"
#include "garr.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static SGS_IMPGraph *create_IMPGraph(const SGS_ParseEventData *vo_in) {
	uint32_t size;
	size = vo_in->graph.count;
	if (!size) return NULL;

	const SGS_ParseOperatorData **ops;
	uint32_t i;
	ops = (const SGS_ParseOperatorData**) SGS_PList_ITEMS(&vo_in->graph);
	SGS_IMPGraph *o;
	o = malloc(sizeof(SGS_IMPGraph) + sizeof(int32_t) * (size - 1));
	if (!o) return NULL;
	o->opc = size;
	for (i = 0; i < size; ++i) {
		o->ops[i] = ops[i]->operator_id;
	}
	return o;
}

static SGS_IMPGraphAdjcs *create_IMPGraphAdjcs(const SGS_ParseOperatorData *op_in) {
	uint32_t size;
	size = op_in->fmods.count +
		op_in->pmods.count +
		op_in->amods.count;
	if (!size) return NULL;

	const SGS_ParseOperatorData **ops;
	uint32_t i;
	uint32_t *data;
	SGS_IMPGraphAdjcs *o;
	o = malloc(sizeof(SGS_IMPGraphAdjcs) + sizeof(int32_t) * (size - 1));
	if (!o) return NULL;
	o->fmodc = op_in->fmods.count;
	o->pmodc = op_in->pmods.count;
	o->amodc = op_in->amods.count;
	data = o->adjcs;
	ops = (const SGS_ParseOperatorData**) SGS_PList_ITEMS(&op_in->fmods);
	for (i = 0; i < o->fmodc; ++i)
		*data++ = ops[i]->operator_id;
	ops = (const SGS_ParseOperatorData**) SGS_PList_ITEMS(&op_in->pmods);
	for (i = 0; i < o->pmodc; ++i)
		*data++ = ops[i]->operator_id;
	ops = (const SGS_ParseOperatorData**) SGS_PList_ITEMS(&op_in->amods);
	for (i = 0; i < o->amodc; ++i)
		*data++ = ops[i]->operator_id;
	return o;
}

typedef struct VoState {
  SGS_ParseEventData *last;
  SGS_IMPGraph *graph;
  uint32_t duration_ms;
} VoState;

SGS_GArr_DEF(VoStateList, VoState)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(SGS_ParseEventData *ve) {
  SGS_ParseOperatorData **ops;
  uint32_t i;
  uint32_t duration_ms = 0;
  /* FIXME: node list type? */
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&ve->operators);
  for (i = 0; i < ve->operators.count; ++i) {
    SGS_ParseOperatorData *op = ops[i];
    if (op->time_ms > duration_ms)
      duration_ms = op->time_ms;
  }
  return duration_ms;
}

/*
 * Get voice for event, returning ID. Use current voice if any;
 * otherwise, either reuse an expired voice, or allocate a new
 * voice if none is free.
 */
static uint32_t voice_alloc_inc(VoStateList *va, SGS_ParseEventData *e) {
  VoState vo = {e, NULL, 0};
  uint32_t vo_id;
  for (vo_id = 0; vo_id < va->count; ++vo_id) {
    if (va->a[vo_id].duration_ms < e->wait_ms)
      va->a[vo_id].duration_ms = 0;
    else
      va->a[vo_id].duration_ms -= e->wait_ms;
  }
  if (e->voice_prev) {
    SGS_ParseEventData *prev = e->voice_prev;
    vo_id = prev->voice_id;
    va->a[vo_id] = vo;
  } else {
    for (vo_id = 0; vo_id < va->count; ++vo_id)
      if (!(va->a[vo_id].last->ed_flags & SGS_PSED_VOICE_LATER_USED) &&
          va->a[vo_id].duration_ms == 0) break;
    /*
     * If no unused voice found, allocate new one.
     */
    VoStateList_add(va, &vo);
  }
  e->voice_id = vo_id;
  if (e->voice_params & SGS_P_GRAPH)
    va->a[vo_id].duration_ms = voice_duration(e);
  return vo_id;
}

typedef struct OpState {
  SGS_ParseOperatorData *last;
  SGS_IMPEvent *out;
  SGS_IMPGraphAdjcs *adjcs;
  //uint32_t duration_ms;
} OpState;

SGS_GArr_DEF(OpStateList, OpState)

/*
 * Get operator for event, returning ID. Use current operator if any;
 * otherwise, allocate a new. (Tracking of expired operators for reuse
 * of their IDs is currently disabled.)
 *
 * Only valid to call for single-operator nodes.
 */
static uint32_t operator_alloc_inc(OpStateList *oa, SGS_ParseOperatorData *od) {
  OpState op = {od, NULL, NULL};
//  SGS_ParseEventData *e = od->event;
  uint32_t op_id;
//  for (op_id = 0; op_id < oa->count; ++op_id) {
//    if (oa->a[op_id].duration_ms < e->wait_ms)
//      oa->a[op_id].duration_ms = 0;
//    else
//      oa->a[op_id].duration_ms -= e->wait_ms;
//  }
  if (od->on_prev) {
    SGS_ParseOperatorData *pod = od->on_prev;
    op_id = pod->operator_id;
    oa->a[op_id] = op;
  } else {
    op_id = oa->count;
//    for (op_id = 0; op_id < oa->count; ++op_id)
//      if (!(oa->a[op_id].last->od_flags & SGS_PSOD_OPERATOR_LATER_USED) &&
//          oa->a[op_id].duration_ms == 0) break;
    /*
     * If no unused operator found, allocate new one.
     */
    OpStateList_add(oa, &op);
  }
  od->operator_id = op_id;
//  oa->a[op_id].duration_ms = od->time_ms;
  return op_id;
}

typedef struct IMPAlloc {
	VoStateList va;
	OpStateList oa;
	SGS_IMP *program;
	SGS_IMPEvent *event;
} IMPAlloc;

static bool init_IMPAlloc(IMPAlloc *o, SGS_ParseResult *parse) {
	o->va = (VoStateList){0};
	o->oa = (OpStateList){0};
	o->program = calloc(1, sizeof(SGS_IMP));
	if (!o->program) return false;
	o->program->parse = parse;
	o->event = NULL;
	return true;
}

static void fini_IMPAlloc(IMPAlloc *pa) {
	SGS_IMP *prg = pa->program;
	prg->op_count = pa->oa.count;
	if (pa->va.count > SGS_VO_MAX_ID) {
		/*
		 * Error.
		 */
	}
	prg->vo_count = pa->va.count;
	OpStateList_clear(&pa->oa);
	VoStateList_clear(&pa->va);
}

static SGS_IMPEvent *program_add_event(IMPAlloc *pa,
		uint32_t vo_id) {
  SGS_IMPEvent *event = calloc(1, sizeof(SGS_IMPEvent));
  event->vo_id = vo_id;
  SGS_PList_add(&pa->program->ev_list, event);
  pa->event = event;
  return event;
}

/*
 * Convert data for an operator node to program operator data, setting it for
 * the program event given.
 */
static void program_convert_onode(IMPAlloc *pa, SGS_ParseOperatorData *op,
                                  uint32_t op_id) {
  OpState *oa_data = &pa->oa.a[op_id];
  SGS_IMPEvent *out_ev = oa_data->out;
  SGS_IMPOperatorData *ood = calloc(1, sizeof(SGS_IMPOperatorData));
  out_ev->op_data = ood;
  out_ev->params |= op->operator_params;
  ++pa->program->odata_count;
  //printf("op_id == %d | address == %x\n", op->op_id, op);
  ood->op_id = op_id;
  ood->adjcs = 0;
  ood->attr = op->attr;
  ood->wave = op->wave;
  ood->time_ms = op->time_ms;
  ood->silence_ms = op->silence_ms;
  ood->freq = op->freq;
  ood->dynfreq = op->dynfreq;
  ood->phase = op->phase;
  ood->amp = op->amp;
  ood->dynamp = op->dynamp;
  ood->valitfreq = op->valitfreq;
  ood->valitamp = op->valitamp;
  if (op->operator_params & SGS_P_ADJCS) {
    oa_data->adjcs = create_IMPGraphAdjcs(op);
    out_ev->op_data->adjcs = oa_data->adjcs;
  }
}

/*
 * Visit each operator node in the node list and recurse through each node's
 * sublists in turn, following and converting operator data and allocating
 * new output events as needed.
 */
static void program_follow_onodes(IMPAlloc *pa, SGS_PList *op_list) {
  SGS_ParseOperatorData **ops;
  uint32_t i;
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(op_list);
  for (i = op_list->copy_count; i < op_list->count; ++i) {
    SGS_ParseOperatorData *op = ops[i];
    OpState *ad;
    uint32_t op_id;
    if (op->od_flags & SGS_PSOD_MULTIPLE_OPERATORS) continue;
    op_id = operator_alloc_inc(&pa->oa, op);
    program_follow_onodes(pa, &op->fmods);
    program_follow_onodes(pa, &op->pmods);
    program_follow_onodes(pa, &op->amods);
    ad = &pa->oa.a[op_id];
    if (pa->event->op_data) {
      uint32_t vo_id = pa->event->vo_id;
      program_add_event(pa, vo_id);
    }
    ad->out = pa->event;
    program_convert_onode(pa, op, op_id);
  }
}

/*
 * Convert voice and operator data for an event node into a (series of) output
 * events.
 *
 * This is the "main" parser data conversion function, to be called for every
 * event.
 */
static void program_convert_enode(IMPAlloc *pa, SGS_ParseEventData *e) {
  VoState *va_data;
  SGS_IMPEvent *out_ev;
  SGS_IMPVoiceData *ovd;
  /* Add to final output list */
  uint32_t vo_id = voice_alloc_inc(&pa->va, e);
  va_data = &pa->va.a[vo_id];
  out_ev = program_add_event(pa, vo_id);
  out_ev->wait_ms = e->wait_ms;
  program_follow_onodes(pa, &e->operators);
  out_ev = pa->event; /* event field may have changed */
  if (e->voice_params) {
    ovd = calloc(1, sizeof(SGS_IMPVoiceData));
    out_ev->vo_data = ovd;
    out_ev->params |= e->voice_params;
    ++pa->program->vdata_count;
    ovd->attr = e->voice_attr;
    ovd->panning = e->panning;
    ovd->valitpanning = e->valitpanning;
    if (e->voice_params & SGS_P_GRAPH) {
      va_data->graph = create_IMPGraph(e);
      out_ev->vo_data->graph = va_data->graph;
    }
  }
}

/*
 * Build intermediate program, allocating events, voices,
 * and operators using the parse result.
 */
static void IMPAlloc_convert(IMPAlloc *o) {
	SGS_ParseResult *parse = o->program->parse;
	SGS_ParseEventData *e;
	o->event = NULL;
	for (e = parse->events; e; e = e->next) {
		program_convert_enode(o, e);
	}
}

/**
 * Create instance for the given parser output.
 *
 * Returns instance if successful, NULL on error.
 */
SGS_IMP *SGS_create_IMP(SGS_ParseResult *parse) {
	IMPAlloc pa;
	if (!init_IMPAlloc(&pa, parse)) return NULL;
	SGS_IMP *o = pa.program;
	IMPAlloc_convert(&pa);
	fini_IMPAlloc(&pa);
#if 1
	SGS_IMP_print_info(o);
#endif
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_IMP(SGS_IMP *o) {
	SGS_IMPEvent **events = (SGS_IMPEvent**) SGS_PList_ITEMS(&o->ev_list);
	for (size_t i = 0; i < o->ev_list.count; ++i) {
		SGS_IMPEvent *e = events[i];
		if (e->vo_data) {
			free((void*)e->vo_data->graph);
			free((void*)e->vo_data);
		}
		if (e->op_data) {
			free((void*)e->op_data->adjcs);
			free((void*)e->op_data);
		}
		free(e);
	}
	SGS_PList_clear(&o->ev_list);
	free(o);
}

static void print_linked(const char *header, const char *footer,
		uint32_t count, const uint32_t *nodes) {
	uint32_t i;
	if (!count) return;
	fprintf(stderr, "%s%d", header, nodes[0]);
	for (i = 0; ++i < count; )
		fprintf(stderr, ", %d", nodes[i]);
	fprintf(stderr, "%s", footer);
}

/**
 * Print information about program contents. Useful for debugging.
 */
void SGS_IMP_print_info(SGS_IMP *o) {
	const SGS_IMPEvent **events;
	fprintf(stderr,
		"Program: \"%s\"\n", o->parse->name);
	fprintf(stderr,
		"\tevents: %ld\tvoices: %hd\toperators: %d\n",
		o->ev_list.count, o->vo_count, o->op_count);
	events = (const SGS_IMPEvent**) SGS_PList_ITEMS(&o->ev_list);
	for (size_t event_id = 0; event_id < o->ev_list.count; ++event_id) {
		const SGS_IMPEvent *oe;
		const SGS_IMPVoiceData *ovo;
		const SGS_IMPOperatorData *oop;
		oe = events[event_id];
		ovo = oe->vo_data;
		oop = oe->op_data;
		fprintf(stderr,
			"\\%d \tEV %ld \t(VI %d)",
			oe->wait_ms, event_id, oe->vo_id);
		if (ovo) {
			const SGS_IMPGraph *g = ovo->graph;
			fprintf(stderr,
				"\n\tvo %d", oe->vo_id);
			if (g)
				print_linked("\n\t    {", "}", g->opc, g->ops);
		}
		if (oop) {
			const SGS_IMPGraphAdjcs *ga = oop->adjcs;
			if (oop->time_ms == SGS_TIME_INF)
				fprintf(stderr,
					"\n\top %d \tt=INF \tf=%.f",
					oop->op_id, oop->freq);
			else
				fprintf(stderr,
					"\n\top %d \tt=%d \tf=%.f",
					oop->op_id, oop->time_ms, oop->freq);
			if (ga) {
				print_linked("\n\t    f!<", ">", ga->fmodc,
					ga->adjcs);
				print_linked("\n\t    p!<", ">", ga->pmodc,
					&ga->adjcs[ga->fmodc]);
				print_linked("\n\t    a!<", ">", ga->amodc,
					&ga->adjcs[ga->fmodc + ga->pmodc]);
			}
		}
		putc('\n', stderr);
	}
}
