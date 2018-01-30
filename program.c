/* Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>
 */

#include "sgensys.h"
#include "program.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>

static int count_operators(SGSOperatorNode *op, void *arg) {
  return 1; /* summed for each operator */
}

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
  graph = malloc(sizeof(SGSProgramGraph) + sizeof(int) * (size - 1));
  graph->opc = size;
  for (i = 0; i < size; ++i)
    graph->ops[i] = voice_in->graph.na[i]->operatorid;
  *graph_out = graph;
}

static void build_adjcs(SGSProgramEvent *root,
                        const SGSOperatorNode *operator_in) {
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
  for (i = 0; i < adjcs->fmodc; ++i)
    *data++ = operator_in->fmods.na[i]->operatorid;
  for (i = 0; i < adjcs->pmodc; ++i)
    *data++ = operator_in->pmods.na[i]->operatorid;
  for (i = 0; i < adjcs->amodc; ++i)
    *data++ = operator_in->amods.na[i]->operatorid;
  *adjcs_out = adjcs;
}

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

#define VOICE_ALLOC_FINI(va) \
  free((va)->data)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint voice_duration(SGSEventNode *ve) {
  uint i, duration_ms = 0;
  /* FIXME: node list type? */
  for (i = 0; i < ve->operators.count; ++i) {
    SGSOperatorNode *op = ve->operators.na[i];
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

#define VOICE_ALLOC_COUNT(va) ((uint)((va)->voicec))

/*
 * Called for each operator for an event - inserts operator data, advancing
 * the event pointer (pointed to by arg) and inserting a new output event
 * if the current one already holds operator data.
 */
static int build_oe(SGSOperatorNode *op, void *arg) {
  //puts("build_oe():");
  SGSProgramEvent **oe = (SGSProgramEvent**)arg;
  SGSProgramOperatorData *ood = calloc(1, sizeof(SGSProgramOperatorData));
  if ((*oe)->operator) {
    //puts(">1 operators: next");
    SGSProgramEvent *oe_prev = *oe;
    ++(*oe);
    (*oe)->voiceid = oe_prev->voiceid;
  }
  (*oe)->operator = ood;
  (*oe)->params |= op->operator_params;
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
    build_adjcs((*oe), op);
  }
  //puts("/build_oe()");
  return 0;
}

static SGSProgram* build(SGSParser *o) {
  //puts("build():");
  VoiceAlloc va;
  SGSProgram *prg = calloc(1, sizeof(SGSProgram));
  SGSEventNode *e;
  SGSProgramEvent *oevents, *oe;
  uint count, id;
  for (count = 0, e = o->events; e; e = e->next) {
    uint ops = SGS_node_list_rforeach(&e->operators, count_operators, 0);
    count += (ops > 0) ? ops : 1; /* one voice-only event if none */
  }
  prg->eventc = count;
  /*
   * Pass #2 - Output event allocation, voice allocation,
   *           parameter data copying.
   */
  oevents = calloc(prg->eventc, sizeof(SGSProgramEvent));
  VOICE_ALLOC_INIT(&va);
  for (oe = oevents, e = o->events; e; ) {
    SGSProgramVoiceData *ovd;
    /* Add to final output list */
    oe->wait_ms = e->wait_ms;
    oe->voiceid = voice_alloc_inc(&va, e);
    SGS_node_list_rforeach(&e->operators, build_oe, (void*)&oe);
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
    ++oe;
  }
  prg->voicec = VOICE_ALLOC_COUNT(&va);
  VOICE_ALLOC_FINI(&va);
  *(SGSProgramEvent**)&prg->events = oevents;
  prg->operatorc = o->operatorc;
  /*
   * Pass #3 - Cleanup of parsing data.
   */
  for (oe = oevents, e = o->events; e; ) {
    SGSEventNode *e_next = e->next;
    SGS_event_node_destroy(e);
    e = e_next;
    ++oe;
  }
  //puts("/build()");
#if 1
  /*
   * Debug printing.
   */
  oe = oevents;
  putchar('\n');
  printf("events: %d\tvoices: %d\toperators: %d\n", prg->eventc, prg->voicec, o->operatorc);
  for (id = 0; id < prg->eventc; ++id) {
    const SGSProgramVoiceData *ovo;
    const SGSProgramOperatorData *oop;
    oe = &oevents[id];
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
