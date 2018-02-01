/* sgensys parsing data to program data translator module.
 * Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "program.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>

static void print_linked(const char *header, const char *footer, uint count,
                         const int *nodes) {
  uint i;
  if (!count) return;
  printf("%s%d", header, nodes[0]);
  for (i = 0; ++i < count; )
    printf(", %d", nodes[i]);
  printf("%s", footer);
}

static void build_graph(SGSProgramEvent *root,
                        const SGSEventNode *voice_in) {
  SGSOperatorNode **nl;
  SGSProgramGraph *graph, **graph_out;
  uint i;
  uint size;
  if (!voice_in->voice_params & SGS_GRAPH)
    return;
  size = voice_in->graph.count;
  graph_out = (SGSProgramGraph**)&root->voice->graph;
  if (!size) {
    *graph_out = graph;
    return;
  }
  nl = SGS_NODE_LIST_GET(&voice_in->graph);
  graph = malloc(sizeof(SGSProgramGraph) + sizeof(int) * (size - 1));
  graph->opc = size;
  for (i = 0; i < size; ++i)
    graph->ops[i] = nl[i]->operatorid;
  *graph_out = graph;
}

static void build_adjcs(SGSProgramEvent *root,
                        const SGSOperatorNode *operator_in) {
  SGSOperatorNode **nl;
  SGSProgramGraphAdjcs *adjcs, **adjcs_out;
  int *data;
  uint i;
  uint size;
  if (!operator_in || !(operator_in->operator_params & SGS_ADJCS))
    return;
  size = operator_in->fmods.count +
         operator_in->pmods.count +
         operator_in->amods.count;
  adjcs_out = (SGSProgramGraphAdjcs**)&root->operator->adjcs;
  if (!size) {
    *adjcs_out = 0;
    return;
  }
  adjcs = malloc(sizeof(SGSProgramGraphAdjcs) + sizeof(int) * (size - 1));
  adjcs->fmodc = operator_in->fmods.count;
  adjcs->pmodc = operator_in->pmods.count;
  adjcs->amodc = operator_in->amods.count;
  data = adjcs->adjcs;
  nl = SGS_NODE_LIST_GET(&operator_in->fmods);
  for (i = 0; i < adjcs->fmodc; ++i)
    *data++ = nl[i]->operatorid;
  nl = SGS_NODE_LIST_GET(&operator_in->pmods);
  for (i = 0; i < adjcs->pmodc; ++i)
    *data++ = nl[i]->operatorid;
  nl = SGS_NODE_LIST_GET(&operator_in->amods);
  for (i = 0; i < adjcs->amodc; ++i)
    *data++ = nl[i]->operatorid;
  *adjcs_out = adjcs;
}

/*
 * Voice allocation
 */

typedef struct VoiceAlloc {
  struct VoiceAllocData {
    SGSEventNode *last;
    uint duration_ms;
  } *data;
  uint voicec;
  uint alloc;
} VoiceAlloc;

typedef struct VoiceAllocData VoiceAllocData;

#define VOICE_ALLOC_INIT(va) do{ \
  (va)->data = calloc(1, sizeof(VoiceAllocData)); \
  (va)->voicec = 0; \
  (va)->alloc = 1; \
}while(0)

#define VOICE_ALLOC_FINI(va, prg) do{ \
  (prg)->voicec = (va)->voicec; \
  free((va)->data); \
}while(0)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint voice_duration(SGSEventNode *ve) {
  SGSOperatorNode **nl = SGS_NODE_LIST_GET(&ve->operators);
  uint i, duration_ms = 0;
  /* FIXME: node list type? */
  for (i = 0; i < ve->operators.count; ++i) {
    SGSOperatorNode *op = nl[i];
    if (op->time_ms > (int)duration_ms)
      duration_ms = op->time_ms;
  }
  return duration_ms;
}

/*
 * Incremental voice allocation - allocate voice for event,
 * returning voice id.
 */
static uint voice_alloc_inc(VoiceAlloc *va, SGSEventNode *e) {
  uint voice;
  for (voice = 0; voice < va->voicec; ++voice) {
    if ((int)va->data[voice].duration_ms < e->wait_ms)
      va->data[voice].duration_ms = 0;
    else
      va->data[voice].duration_ms -= e->wait_ms;
  }
  if (e->voice_prev) {
    SGSEventNode *prev = e->voice_prev;
    voice = prev->voice_id;
  } else {
    for (voice = 0; voice < va->voicec; ++voice)
      if (!(va->data[voice].last->en_flags & EN_VOICE_LATER_USED) &&
          va->data[voice].duration_ms == 0) break;
    /*
     * If no unused voice found, allocate new one.
     */
    if (voice == va->voicec) {
      ++va->voicec;
      if (va->voicec > va->alloc) {
        uint i = va->alloc;
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
  if (e->voice_params & SGS_GRAPH)
    va->data[voice].duration_ms = voice_duration(e);
  return voice;
}

/*
 * Output event allocation
 */

typedef struct OEventAlloc {
  SGSProgramEvent *oe, *oevents;
  uint eventc;
  uint alloc;
} OEventAlloc;

#define OEVENT_ALLOC_INIT(oea) do{ \
  (oea)->oe = 0; \
  (oea)->oevents = 0; \
  (oea)->eventc = 0; \
  (oea)->alloc = 0; \
}while(0)

#define OEVENT_ALLOC_FINI(oea, prg) do{ \
  /* output events passed on to program, not freed */ \
  *(SGSProgramEvent**)&(prg)->events = (oea)->oevents; \
  (prg)->eventc = (oea)->eventc; \
}while(0)

static SGSProgramEvent *oevent_alloc(OEventAlloc *oea, uint voice_id) {
  SGSProgramEvent *oe;
  ++oea->eventc;
  if (oea->eventc > oea->alloc) {
    oea->alloc = (oea->alloc > 0) ? oea->alloc << 1 : 1;
    oea->oevents = realloc(oea->oevents, oea->alloc * sizeof(SGSProgramEvent));
    memset(&oea->oevents[oea->eventc - 1], 0,
           sizeof(SGSProgramEvent) * (oea->alloc - (oea->eventc - 1)));
  }
  oea->oe = &oea->oevents[oea->eventc - 1];
  oe = oea->oe;
  oe->voiceid = voice_id;
  return oe;
}

/*
 * Overwrite parameters in dst that have values in src.
 */
static void copy_params(SGSOperatorNode *dst, const SGSOperatorNode *src) {
  if (src->operator_params & SGS_AMP) dst->amp = src->amp;
}

static void expand_operator(SGSOperatorNode *op) {
  SGSOperatorNode *pop;
  if (!(op->on_flags & ON_MULTIPLE_OPERATORS)) return;
  pop = op->on_prev;
  do {
    copy_params(pop, op);
    expand_operator(pop);
  } while ((pop = pop->next_bound));
  SGS_node_list_clear(&op->fmods);
  SGS_node_list_clear(&op->pmods);
  SGS_node_list_clear(&op->amods);
  op->operator_params = 0;
}

/*
 * Convert data for an operator to program operator data, setting it for the
 * program event given.
 */
static void convert_operator(SGSProgramEvent *oe, const SGSOperatorNode *op) {
  SGSProgramOperatorData *ood = calloc(1, sizeof(SGSProgramOperatorData));
  oe->operator = ood;
  oe->params |= op->operator_params;
  //printf("operatorid == %d | address == %x\n", op->operatorid, op);
  ood->operatorid = op->operatorid;
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
  if (op->operator_params & SGS_ADJCS) {
    build_adjcs(oe, op);
  }
}

/*
 * Called for each operator for an event - inserts operator data, allocating
 * a new output event if the current one already holds operator data.
 */
static int build_oe(SGSOperatorNode *op, void *arg) {
  OEventAlloc *oea = arg;
  SGSProgramEvent *oe = oea->oe;
  //puts("build_oe():");
  if (oe->operator) {
    uint voice_id = oe->voiceid;
    oe = oevent_alloc(oea, voice_id);
  }
  convert_operator(oe, op);
  //puts("/build_oe()");
  return 0;
}

static SGSProgram* build(SGSParser *o) {
  //puts("build():");
  VoiceAlloc va;
  OEventAlloc oea;
  SGSProgram *prg = calloc(1, sizeof(SGSProgram));
  SGSEventNode *e;
  uint id;
  /*
   * Pass #1 - Output event allocation, voice allocation,
   *           parameter data copying.
   */
  VOICE_ALLOC_INIT(&va);
  OEVENT_ALLOC_INIT(&oea);
  for (e = o->events; e; ) {
    SGSProgramEvent *oe;
    SGSProgramVoiceData *ovd;
    /* Add to final output list */
    oe = oevent_alloc(&oea, voice_alloc_inc(&va, e));
    oe->wait_ms = e->wait_ms;
    SGS_node_list_rforeach(&e->operators, build_oe, (void*)&oea);
    oe = oea.oe; /* oe may have re(al)located */
    if (e->voice_params) {
      ovd = calloc(1, sizeof(SGSProgramVoiceData));
      oe->voice = ovd;
      oe->params |= e->voice_params;
      ovd->attr = e->voice_attr;
      ovd->panning = e->panning;
      ovd->valitpanning = e->valitpanning;
      if (e->voice_params & SGS_GRAPH) {
        build_graph(oe, e);
      }
    }
    e = e->next;
  }
  OEVENT_ALLOC_FINI(&oea, prg);
  VOICE_ALLOC_FINI(&va, prg);
  prg->operatorc = o->operatorc;
  /*
   * Pass #2 - Cleanup of parsing data.
   */
  for (e = o->events; e; ) {
    SGSEventNode *e_next = e->next;
    SGS_event_node_destroy(e);
    e = e_next;
  }
  //puts("/build()");
#if 1
  /*
   * Debug printing.
   */
  putchar('\n');
  printf("events: %d\tvoices: %d\toperators: %d\n", prg->eventc, prg->voicec, o->operatorc);
  for (id = 0; id < prg->eventc; ++id) {
    const SGSProgramEvent *oe;
    const SGSProgramVoiceData *ovo;
    const SGSProgramOperatorData *oop;
    oe = &prg->events[id];
    ovo = oe->voice;
    oop = oe->operator;
    printf("\\%d \tEV %d \t(VI %d)", oe->wait_ms, id, oe->voiceid);
    if (ovo) {
      const SGSProgramGraph *g = ovo->graph;
      printf("\n\tvo %d", oe->voiceid);
      if (g)
        print_linked("\n\t    {", "}", g->opc, g->ops);
    }
    if (oop) {
      const SGSProgramGraphAdjcs *ga = oop->adjcs;
      if (oop->time_ms == SGS_TIME_INF)
        printf("\n\top %d \tt=INF \tf=%.f", oop->operatorid, oop->freq);
      else
        printf("\n\top %d \tt=%d \tf=%.f", oop->operatorid, oop->time_ms, oop->freq);
      if (ga) {
        print_linked("\n\t    f!<", ">", ga->fmodc, ga->adjcs);
        print_linked("\n\t    p!<", ">", ga->pmodc, &ga->adjcs[ga->fmodc]);
        print_linked("\n\t    a!<", ">", ga->amodc, &ga->adjcs[ga->fmodc +
                                                               ga->pmodc]);
      }
    }
    putchar('\n');
  }
#endif
  return prg;
}

SGSProgram* SGS_program_create(const char *filename) {
  SGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  SGS_parse(&p, f, filename);
  fclose(f);
  return build(&p);
}

void SGS_program_destroy(SGSProgram *o) {
  uint i;
  for (i = 0; i < o->eventc; ++i) {
    SGSProgramEvent *e = (void*)&o->events[i];
    if (e->voice) {
      free((void*)e->voice->graph);
      free((void*)e->voice);
    }
    if (e->operator) {
      free((void*)e->operator->adjcs);
      free((void*)e->operator);
    }
  }
  free((void*)o->events);
}
