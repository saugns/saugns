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
 * <http://www.gnu.org/licenses/>.
 */

#include "imp.h"
#include "parser.h"
#include <string.h>
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

typedef struct VoiceAllocData {
  SGS_ParseEventData *last;
  uint32_t duration_ms;
} VoiceAllocData;

typedef struct VoiceAlloc {
  VoiceAllocData *data;
  uint32_t count;
  uint32_t alloc;
} VoiceAlloc;

static void vo_alloc_init(VoiceAlloc *va) {
  va->data = calloc(1, sizeof(VoiceAllocData));
  va->count = 0;
  va->alloc = 1;
}

static void vo_alloc_fini(VoiceAlloc *va, SGS_IMP *prg) {
	if (va->count > SGS_VO_MAX_ID) {
		/*
		 * Error.
		 */
	}
	prg->vo_count = va->count;
	free(va->data);
}

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
 * Incremental voice allocation - allocate voice for event,
 * returning voice id.
 */
static uint32_t vo_alloc_inc(VoiceAlloc *va, SGS_ParseEventData *e) {
  uint32_t voice;
  for (voice = 0; voice < va->count; ++voice) {
    if (va->data[voice].duration_ms < e->wait_ms)
      va->data[voice].duration_ms = 0;
    else
      va->data[voice].duration_ms -= e->wait_ms;
  }
  if (e->voice_prev) {
    SGS_ParseEventData *prev = e->voice_prev;
    voice = prev->voice_id;
  } else {
    for (voice = 0; voice < va->count; ++voice)
      if (!(va->data[voice].last->ed_flags & SGS_PSED_VOICE_LATER_USED) &&
          va->data[voice].duration_ms == 0) break;
    /*
     * If no unused voice found, allocate new one.
     */
    if (voice == va->count) {
      ++va->count;
      if (va->count > va->alloc) {
        uint32_t i = va->alloc;
        va->alloc <<= 1;
        va->data = realloc(va->data, va->alloc * sizeof(VoiceAllocData));
        while (i < va->alloc) {
          va->data[i].last = 0;
          va->data[i].duration_ms = 0;
          ++i;
        }
      }
    }
  }
  e->voice_id = voice;
  va->data[voice].last = e;
  if (e->voice_params & SGS_P_GRAPH)
    va->data[voice].duration_ms = voice_duration(e);
  return voice;
}

typedef struct OperatorAllocData {
  SGS_ParseOperatorData *last;
  SGS_IMPEvent *out;
  uint32_t duration_ms;
} OperatorAllocData;

typedef struct OperatorAlloc {
  OperatorAllocData *data;
  uint32_t count;
  uint32_t alloc;
} OperatorAlloc;

static void op_alloc_init(OperatorAlloc *oa) {
  oa->data = calloc(1, sizeof(OperatorAllocData));
  oa->count = 0;
  oa->alloc = 1;
}

static void op_alloc_fini(OperatorAlloc *oa, SGS_IMP *prg) {
  prg->op_count = oa->count;
  free(oa->data);
}

/*
 * Incremental operator allocation - allocate operator for event,
 * returning operator id.
 *
 * Only valid to call for single-operator nodes.
 */
static uint32_t op_alloc_inc(OperatorAlloc *oa, SGS_ParseOperatorData *op) {
  SGS_ParseEventData *e = op->event;
  uint32_t operator;
  for (operator = 0; operator < oa->count; ++operator) {
    if (oa->data[operator].duration_ms < e->wait_ms)
      oa->data[operator].duration_ms = 0;
    else
      oa->data[operator].duration_ms -= e->wait_ms;
  }
  if (op->on_prev) {
    SGS_ParseOperatorData *pop = op->on_prev;
    operator = pop->operator_id;
  } else {
//    for (operator = 0; operator < oa->count; ++operator)
//      if (!(oa->data[operator].last->od_flags & SGS_PSOD_OPERATOR_LATER_USED) &&
//          oa->data[operator].duration_ms == 0) break;
    /*
     * If no unused operator found, allocate new one.
     */
    if (operator == oa->count) {
      ++oa->count;
      if (oa->count > oa->alloc) {
        uint32_t i = oa->alloc;
        oa->alloc <<= 1;
        oa->data = realloc(oa->data, oa->alloc * sizeof(OperatorAllocData));
        while (i < oa->alloc) {
          oa->data[i].last = 0;
          oa->data[i].duration_ms = 0;
          ++i;
        }
      }
    }
  }
  op->operator_id = operator;
  oa->data[operator].last = op;
//  oa->data[operator].duration_ms = op->time_ms;
  return operator;
}

typedef struct IMPAlloc {
	VoiceAlloc va;
	OperatorAlloc oa;
	SGS_IMP *program;
	SGS_IMPEvent *event;
} IMPAlloc;

static bool init_IMPAlloc(IMPAlloc *o, SGS_ParseResult *parse) {
	vo_alloc_init(&o->va);
	op_alloc_init(&o->oa);
	o->program = calloc(1, sizeof(SGS_IMP));
	if (!o->program) return false;
	o->program->parse = parse;
	o->event = NULL;
	return true;
}

static void fini_IMPAlloc(IMPAlloc *pa) {
  op_alloc_fini(&pa->oa, pa->program);
  vo_alloc_fini(&pa->va, pa->program);
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
  SGS_IMPEvent *out_ev = pa->oa.data[op_id].out;
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
    out_ev->op_data->adjcs = create_IMPGraphAdjcs(op);
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
    OperatorAllocData *ad;
    uint32_t op_id;
    if (op->od_flags & SGS_PSOD_MULTIPLE_OPERATORS) continue;
    op_id = op_alloc_inc(&pa->oa, op);
    program_follow_onodes(pa, &op->fmods);
    program_follow_onodes(pa, &op->pmods);
    program_follow_onodes(pa, &op->amods);
    ad = &pa->oa.data[op_id];
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
  SGS_IMPEvent *out_ev;
  SGS_IMPVoiceData *ovd;
  /* Add to final output list */
  out_ev = program_add_event(pa, vo_alloc_inc(&pa->va, e));
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
      out_ev->vo_data->graph = create_IMPGraph(e);
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
