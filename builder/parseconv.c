/* sgensys: Parse result to audio program converter.
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

#include "../program.h"
#include "../script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void build_graph(SGS_ProgramEvent *root,
    const SGS_ScriptEvData *voice_in) {
  const SGS_ScriptOpData **ops;
  SGS_ProgramGraph *graph, **graph_out;
  uint32_t i;
  uint32_t size;
  size = voice_in->graph.count;
  graph_out = (SGS_ProgramGraph**)&root->voice->graph;
  if (!size) {
    *graph_out = NULL;
    return;
  }
  ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&voice_in->graph);
  graph = malloc(sizeof(SGS_ProgramGraph) + sizeof(int32_t) * (size - 1));
  graph->opc = size;
  for (i = 0; i < size; ++i)
    graph->ops[i] = ops[i]->operator_id;
  *graph_out = graph;
}

static void build_adjcs(SGS_ProgramEvent *root,
    const SGS_ScriptOpData *operator_in) {
  const SGS_ScriptOpData **ops;
  SGS_ProgramGraphAdjcs *adjcs, **adjcs_out;
  int32_t *data;
  uint32_t i;
  uint32_t size;
  size = operator_in->fmods.count +
         operator_in->pmods.count +
         operator_in->amods.count;
  adjcs_out = (SGS_ProgramGraphAdjcs**)&root->operator->adjcs;
  if (!size) {
    *adjcs_out = NULL;
    return;
  }
  adjcs = malloc(sizeof(SGS_ProgramGraphAdjcs) + sizeof(int32_t) * (size - 1));
  adjcs->fmodc = operator_in->fmods.count;
  adjcs->pmodc = operator_in->pmods.count;
  adjcs->amodc = operator_in->amods.count;
  data = adjcs->adjcs;
  ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&operator_in->fmods);
  for (i = 0; i < adjcs->fmodc; ++i)
    *data++ = ops[i]->operator_id;
  ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&operator_in->pmods);
  for (i = 0; i < adjcs->pmodc; ++i)
    *data++ = ops[i]->operator_id;
  ops = (const SGS_ScriptOpData**) SGS_PtrList_ITEMS(&operator_in->amods);
  for (i = 0; i < adjcs->amodc; ++i)
    *data++ = ops[i]->operator_id;
  *adjcs_out = adjcs;
}

/*
 * Program (event, voice, operator) allocation
 */

typedef struct VoiceAllocData {
  SGS_ScriptEvData *last;
  uint32_t duration_ms;
} VoiceAllocData;

typedef struct VoiceAlloc {
  VoiceAllocData *data;
  uint32_t count;
  uint32_t alloc;
} VoiceAlloc;

static void voice_alloc_init(VoiceAlloc *va) {
  va->data = calloc(1, sizeof(VoiceAllocData));
  va->count = 0;
  va->alloc = 1;
}

static void voice_alloc_fini(VoiceAlloc *va, SGS_Program *prg) {
  if (va->count > INT16_MAX) {
    /*
     * Error.
     */
  }
  prg->voice_count = va->count;
  free(va->data);
}

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(SGS_ScriptEvData *ve) {
  SGS_ScriptOpData **ops;
  uint32_t i;
  uint32_t duration_ms = 0;
  /* FIXME: node list type? */
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&ve->operators);
  for (i = 0; i < ve->operators.count; ++i) {
    SGS_ScriptOpData *op = ops[i];
    if (op->time_ms > (int32_t)duration_ms)
      duration_ms = op->time_ms;
  }
  return duration_ms;
}

/*
 * Incremental voice allocation - allocate voice for event,
 * returning voice id.
 */
static uint32_t voice_alloc_inc(VoiceAlloc *va, SGS_ScriptEvData *e) {
  uint32_t voice;
  for (voice = 0; voice < va->count; ++voice) {
    if ((int32_t)va->data[voice].duration_ms < e->wait_ms)
      va->data[voice].duration_ms = 0;
    else
      va->data[voice].duration_ms -= e->wait_ms;
  }
  if (e->voice_prev != NULL) {
    SGS_ScriptEvData *prev = e->voice_prev;
    voice = prev->voice_id;
  } else {
    for (voice = 0; voice < va->count; ++voice)
      if (!(va->data[voice].last->ev_flags & SGS_SDEV_VOICE_LATER_USED) &&
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
  if ((e->voice_params & SGS_P_GRAPH) != 0)
    va->data[voice].duration_ms = voice_duration(e);
  return voice;
}

typedef struct OperatorAllocData {
  SGS_ScriptOpData *last;
  SGS_ProgramEvent *out;
  uint32_t duration_ms;
} OperatorAllocData;

typedef struct OperatorAlloc {
  OperatorAllocData *data;
  uint32_t count;
  uint32_t alloc;
} OperatorAlloc;

static void operator_alloc_init(OperatorAlloc *oa) {
  oa->data = calloc(1, sizeof(OperatorAllocData));
  oa->count = 0;
  oa->alloc = 1;
}

static void operator_alloc_fini(OperatorAlloc *oa, SGS_Program *prg) {
  prg->operator_count = oa->count;
  free(oa->data);
}

/*
 * Incremental operator allocation - allocate operator for event,
 * returning operator id.
 *
 * Only valid to call for single-operator nodes.
 */
static uint32_t operator_alloc_inc(OperatorAlloc *oa, SGS_ScriptOpData *op) {
  SGS_ScriptEvData *e = op->event;
  uint32_t operator;
  for (operator = 0; operator < oa->count; ++operator) {
    if ((int32_t)oa->data[operator].duration_ms < e->wait_ms)
      oa->data[operator].duration_ms = 0;
    else
      oa->data[operator].duration_ms -= e->wait_ms;
  }
  if (op->on_prev != NULL) {
    SGS_ScriptOpData *pop = op->on_prev;
    operator = pop->operator_id;
  } else {
//    for (operator = 0; operator < oa->count; ++operator)
//      if (!(oa->data[operator].last->op_flags & SGS_SDOP_LATER_USED) &&
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

typedef struct ProgramAlloc {
  VoiceAlloc va;
  OperatorAlloc oa;
  SGS_PtrList ev_list;
  SGS_ProgramEvent *event;
} ProgramAlloc;

static void program_alloc_init(ProgramAlloc *pa) {
  voice_alloc_init(&(pa)->va);
  operator_alloc_init(&(pa)->oa);
  pa->ev_list = (SGS_PtrList){0};
  pa->event = NULL;
}

static void program_alloc_fini(ProgramAlloc *pa, SGS_Program *prg) {
  prg->event_count = pa->ev_list.count;
  SGS_PtrList_memdup(&pa->ev_list, &prg->events);
  SGS_PtrList_clear(&pa->ev_list);
  operator_alloc_fini(&pa->oa, prg);
  voice_alloc_fini(&pa->va, prg);
}

static SGS_ProgramEvent *program_add_event(ProgramAlloc *pa,
    uint32_t voice_id) {
  SGS_ProgramEvent *event = calloc(1, sizeof(SGS_ProgramEvent));
  event->voice_id = voice_id;
  SGS_PtrList_add(&pa->ev_list, event);
  pa->event = event;
  return event;
}

/*
 * Convert data for an operator node to program operator data, setting it for
 * the program event given.
 */
static void program_convert_onode(ProgramAlloc *pa, SGS_ScriptOpData *op,
                                  uint32_t operator_id) {
  SGS_ProgramEvent *out_ev = pa->oa.data[operator_id].out;
  SGS_ProgramOpData *ood = calloc(1, sizeof(SGS_ProgramOpData));
  out_ev->operator = ood;
  out_ev->params |= op->operator_params;
  //printf("operator_id == %d | address == %x\n", op->operator_id, op);
  ood->operator_id = operator_id;
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
  if ((op->operator_params & SGS_P_ADJCS) != 0) {
    build_adjcs(out_ev, op);
  }
}

/*
 * Visit each operator node in the node list and recurse through each node's
 * sublists in turn, following and converting operator data and allocating
 * new output events as needed.
 */
static void program_follow_onodes(struct ProgramAlloc *pa, SGS_PtrList *op_list) {
  SGS_ScriptOpData **ops;
  uint32_t i;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(op_list);
  for (i = op_list->old_count; i < op_list->count; ++i) {
    SGS_ScriptOpData *op = ops[i];
    OperatorAllocData *ad;
    uint32_t operator_id;
    if ((op->op_flags & SGS_SDOP_MULTIPLE) != 0) continue;
    operator_id = operator_alloc_inc(&pa->oa, op);
    program_follow_onodes(pa, &op->fmods);
    program_follow_onodes(pa, &op->pmods);
    program_follow_onodes(pa, &op->amods);
    ad = &pa->oa.data[operator_id];
    if (pa->event->operator != NULL) {
      uint32_t voice_id = pa->event->voice_id;
      program_add_event(pa, voice_id);
    }
    ad->out = pa->event;
    program_convert_onode(pa, op, operator_id);
  }
}

/*
 * Convert voice and operator data for an event node into a (series of) output
 * events.
 *
 * This is the "main" parser data conversion function, to be called for every
 * event.
 */
static void program_convert_enode(ProgramAlloc *pa, SGS_ScriptEvData *e) {
  SGS_ProgramEvent *out_ev;
  SGS_ProgramVoData *ovd;
  /* Add to final output list */
  out_ev = program_add_event(pa, voice_alloc_inc(&pa->va, e));
  out_ev->wait_ms = e->wait_ms;
  program_follow_onodes(pa, &e->operators);
  out_ev = pa->event; /* event field may have changed */
  if (e->voice_params != 0) {
    ovd = calloc(1, sizeof(SGS_ProgramVoData));
    out_ev->voice = ovd;
    out_ev->params |= e->voice_params;
    ovd->attr = e->voice_attr;
    ovd->panning = e->panning;
    ovd->valitpanning = e->valitpanning;
    if ((e->voice_params & SGS_P_GRAPH) != 0) {
      build_graph(out_ev, e);
    }
  }
}

/**
 * Create program for the given parser output.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build_Program(SGS_Script *sd) {
  ProgramAlloc pa;
  SGS_Program *o = calloc(1, sizeof(SGS_Program));
  SGS_ScriptEvData *e;
  /*
   * Build program, allocating events, voices, and operators
   * using the parse result.
   */
  program_alloc_init(&pa);
  for (e = sd->events; e; e = e->next) {
    program_convert_enode(&pa, e);
  }
  o->name = sd->name;
  if (!(sd->sopt.changed & SGS_SOPT_AMPMULT)) {
    /*
     * Enable amplitude scaling (division) by voice count,
     * handled by sound generator.
     */
    o->flags |= SGS_PROG_AMP_DIV_VOICES;
  }
  program_alloc_fini(&pa, o);
  return o;
}

static void print_linked(const char *header, const char *footer,
    uint32_t count, const int32_t *nodes) {
  uint32_t i;
  if (!count) return;
  printf("%s%d", header, nodes[0]);
  for (i = 0; ++i < count; )
    printf(", %d", nodes[i]);
  printf("%s", footer);
}

/**
 * Print information about program contents. Useful for debugging.
 */
void SGS_Program_print_info(SGS_Program *o) {
  printf("Program: \"%s\"\n", o->name);
  printf("\tevents: %ld\tvoices: %hd\toperators: %d\n",
    o->event_count, o->voice_count, o->operator_count);
  for (size_t event_id = 0; event_id < o->event_count; ++event_id) {
    const SGS_ProgramEvent *oe;
    const SGS_ProgramVoData *ovo;
    const SGS_ProgramOpData *oop;
    oe = o->events[event_id];
    ovo = oe->voice;
    oop = oe->operator;
    printf("\\%d \tEV %ld \t(VI %d)",
      oe->wait_ms, event_id, oe->voice_id);
    if (ovo != NULL) {
      const SGS_ProgramGraph *g = ovo->graph;
      printf("\n\tvo %d", oe->voice_id);
      if (g != NULL)
        print_linked("\n\t    {", "}", g->opc, g->ops);
    }
    if (oop != NULL) {
      const SGS_ProgramGraphAdjcs *ga = oop->adjcs;
      if (oop->time_ms == SGS_TIME_INF)
        printf("\n\top %d \tt=INF \tf=%.f",
          oop->operator_id, oop->freq);
      else
        printf("\n\top %d \tt=%d \tf=%.f",
          oop->operator_id, oop->time_ms, oop->freq);
      if (ga != NULL) {
        print_linked("\n\t    f!<", ">", ga->fmodc,
          ga->adjcs);
        print_linked("\n\t    p!<", ">", ga->pmodc,
          &ga->adjcs[ga->fmodc]);
        print_linked("\n\t    a!<", ">", ga->amodc,
          &ga->adjcs[ga->fmodc + ga->pmodc]);
      }
    }
    putchar('\n');
  }
}

/**
 * Destroy instance.
 */
void SGS_discard_Program(SGS_Program *o) {
  for (size_t i = 0; i < o->event_count; ++i) {
    SGS_ProgramEvent *e = (SGS_ProgramEvent*) o->events[i];
    if (e->voice != NULL) {
      free((void*)e->voice->graph);
      free((void*)e->voice);
    }
    if (e->operator != NULL) {
      free((void*)e->operator->adjcs);
      free((void*)e->operator);
    }
    free(e);
  }
  free((void*)o->events);
}
