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
    graph->ops[i] = nl[i]->operator_id;
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
    *data++ = nl[i]->operator_id;
  nl = SGS_NODE_LIST_GET(&operator_in->pmods);
  for (i = 0; i < adjcs->pmodc; ++i)
    *data++ = nl[i]->operator_id;
  nl = SGS_NODE_LIST_GET(&operator_in->amods);
  for (i = 0; i < adjcs->amodc; ++i)
    *data++ = nl[i]->operator_id;
  *adjcs_out = adjcs;
}

/*
 * Program (event, voice, operator) allocation
 */

typedef struct VoiceAllocData {
  SGSEventNode *last;
  uint duration_ms;
} VoiceAllocData;

typedef struct VoiceAlloc {
  VoiceAllocData *data;
  uint voicec;
  uint alloc;
} VoiceAlloc;

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

typedef struct OperatorAllocData {
  SGSOperatorNode *last;
  SGSProgramEvent *out;
  uint duration_ms;
} OperatorAllocData;

typedef struct OperatorAlloc {
  OperatorAllocData *data;
  uint operatorc;
  uint alloc;
} OperatorAlloc;

#define OPERATOR_ALLOC_INIT(oa) do{ \
  (oa)->data = calloc(1, sizeof(OperatorAllocData)); \
  (oa)->operatorc = 0; \
  (oa)->alloc = 1; \
}while(0)

#define OPERATOR_ALLOC_FINI(oa, prg) do{ \
  (prg)->operatorc = (oa)->operatorc; \
  free((oa)->data); \
}while(0)

/*
 * Incremental operator allocation - allocate operator for event,
 * returning operator id.
 *
 * Only valid to call for single-operator nodes.
 */
static uint operator_alloc_inc(OperatorAlloc *oa, SGSOperatorNode *op) {
  SGSEventNode *e = op->event;
  uint operator;
  for (operator = 0; operator < oa->operatorc; ++operator) {
    if ((int)oa->data[operator].duration_ms < e->wait_ms)
      oa->data[operator].duration_ms = 0;
    else
      oa->data[operator].duration_ms -= e->wait_ms;
  }
  if (op->on_prev) {
    SGSOperatorNode *pop = op->on_prev;
    operator = pop->operator_id;
  } else {
//    for (operator = 0; operator < oa->operatorc; ++operator)
//      if (!(oa->data[operator].last->on_flags & ON_OPERATOR_LATER_USED) &&
//          oa->data[operator].duration_ms == 0) break;
    /*
     * If no unused operator found, allocate new one.
     */
    if (operator == oa->operatorc) {
      ++oa->operatorc;
      if (oa->operatorc > oa->alloc) {
        uint i = oa->alloc;
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
  SGSProgramEvent *oe, **oevents;
  uint eventc;
  uint alloc;
  OperatorAlloc oa;
  VoiceAlloc va;
} ProgramAlloc;

#define PROGRAM_ALLOC_INIT(pa) do{ \
  VOICE_ALLOC_INIT(&(pa)->va); \
  OPERATOR_ALLOC_INIT(&(pa)->oa); \
  (pa)->oe = 0; \
  (pa)->oevents = 0; \
  (pa)->eventc = 0; \
  (pa)->alloc = 0; \
}while(0)

#define PROGRAM_ALLOC_FINI(pa, prg) do{ \
  uint i; \
  /* copy output events to program & cleanup */ \
  *(SGSProgramEvent**)&(prg)->events = malloc(sizeof(SGSProgramEvent) * \
                                              (pa)->eventc); \
  for (i = 0; i < (pa)->eventc; ++i) { \
    *(SGSProgramEvent*)&(prg)->events[i] = *(pa)->oevents[i]; \
    free((pa)->oevents[i]); \
  } \
  free((pa)->oevents); \
  (prg)->eventc = (pa)->eventc; \
  OPERATOR_ALLOC_FINI(&(pa)->oa, (prg)); \
  VOICE_ALLOC_FINI(&(pa)->va, (prg)); \
}while(0)

static SGSProgramEvent *program_alloc_oevent(ProgramAlloc *pa, uint voice_id) {
  ++pa->eventc;
  if (pa->eventc > pa->alloc) {
    pa->alloc = (pa->alloc > 0) ? pa->alloc << 1 : 1;
    pa->oevents = realloc(pa->oevents, sizeof(SGSProgramEvent*) * pa->alloc);
  }
  pa->oevents[pa->eventc - 1] = calloc(1, sizeof(SGSProgramEvent));
  pa->oe = pa->oevents[pa->eventc - 1];
  pa->oe->voice_id = voice_id;
  return pa->oe;
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
 * Convert data for an operator node to program operator data, setting it for
 * the program event given.
 */
static void program_convert_onode(ProgramAlloc *pa, SGSOperatorNode *op,
                                  uint operator_id) {
  SGSProgramEvent *oe = pa->oa.data[operator_id].out;
  SGSProgramOperatorData *ood = calloc(1, sizeof(SGSProgramOperatorData));
  oe->operator = ood;
  oe->params |= op->operator_params;
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
  if (op->operator_params & SGS_ADJCS) {
    build_adjcs(oe, op);
  }
}

/*
 * Visit each operator node in the node list and recurse through each node's
 * sublists in turn, following and converting operator data and allocating
 * new output events as needed.
 */
static void program_follow_onodes(ProgramAlloc *pa, SGSNodeList *nl) {
  uint i;
  SGSOperatorNode **list = SGS_NODE_LIST_GET(nl);
  for (i = nl->inactive_count; i < nl->count; ++i) {
    SGSOperatorNode *op = list[i];
    OperatorAllocData *ad;
    uint operator_id;
    if (op->on_flags & ON_MULTIPLE_OPERATORS) continue;
    operator_id = operator_alloc_inc(&pa->oa, op);
    program_follow_onodes(pa, &op->fmods);
    program_follow_onodes(pa, &op->pmods);
    program_follow_onodes(pa, &op->amods);
    ad = &pa->oa.data[operator_id];
    if (pa->oe->operator) {
      uint voice_id = pa->oe->voice_id;
      program_alloc_oevent(pa, voice_id);
    }
    ad->out = pa->oe;
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
static void program_convert_enode(ProgramAlloc *pa, SGSEventNode *e) {
  SGSProgramEvent *oe;
  SGSProgramVoiceData *ovd;
  /* Add to final output list */
  oe = program_alloc_oevent(pa, voice_alloc_inc(&pa->va, e));
  oe->wait_ms = e->wait_ms;
  program_follow_onodes(pa, &e->operators);
  oe = pa->oe; /* oe may have re(al)located */
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
}

static SGSProgram* build(SGSParser *o) {
  //puts("build():");
  ProgramAlloc pa;
  SGSProgram *prg = calloc(1, sizeof(SGSProgram));
  SGSEventNode *e;
  uint id;
  /*
   * Pass #1 - Output event allocation, voice allocation,
   *           parameter data copying.
   */
  PROGRAM_ALLOC_INIT(&pa);
  for (e = o->events; e; e = e->next) {
    program_convert_enode(&pa, e);
  }
  PROGRAM_ALLOC_FINI(&pa, prg);
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
  printf("events: %d\tvoices: %d\toperators: %d\n", prg->eventc, prg->voicec, prg->operatorc);
  for (id = 0; id < prg->eventc; ++id) {
    const SGSProgramEvent *oe;
    const SGSProgramVoiceData *ovo;
    const SGSProgramOperatorData *oop;
    oe = &prg->events[id];
    ovo = oe->voice;
    oop = oe->operator;
    printf("\\%d \tEV %d \t(VI %d)", oe->wait_ms, id, oe->voice_id);
    if (ovo) {
      const SGSProgramGraph *g = ovo->graph;
      printf("\n\tvo %d", oe->voice_id);
      if (g)
        print_linked("\n\t    {", "}", g->opc, g->ops);
    }
    if (oop) {
      const SGSProgramGraphAdjcs *ga = oop->adjcs;
      if (oop->time_ms == SGS_TIME_INF)
        printf("\n\top %d \tt=INF \tf=%.f", oop->operator_id, oop->freq);
      else
        printf("\n\top %d \tt=%d \tf=%.f", oop->operator_id, oop->time_ms, oop->freq);
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

#include "lexer.h"
SGSProgram* SGS_program_create(const char *filename) {
	SGSSymtab *symtab = SGS_create_symtab();
	SGSLexer *lexer = SGS_create_lexer(filename, symtab);
	if (!lexer) return NULL;
	for (;;) {
		SGSToken *token = SGS_get_token(lexer);
		if (token->type <= 0) break;
	}
	SGS_destroy_lexer(lexer);
	SGS_destroy_symtab(symtab);
	return (SGSProgram*) calloc(1, sizeof(SGSProgram)); //0;
#if 0 // OLD PARSER
  SGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  SGS_parse(&p, f, filename);
  fclose(f);
  return build(&p);
#endif
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
