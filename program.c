/* sgensys: Parsing data to audio program translator module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#include "program.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const SGSProgramIDArr blank_idarr = {0};

static void copy_list_ids(uint32_t **dst, const SGSListNode *list_in) {
  if (list_in->append && list_in->prev)
    copy_list_ids(dst, list_in->prev);
  for (SGSOperatorNode *op = list_in->first_on; op; op = op->next)
    *(*dst)++ = op->operator_id;
}

static const SGSProgramIDArr *
SGS_create_ProgramIDArr(const SGSListNode *list_in,
                        const SGSProgramIDArr *copy) {
  uint32_t count = list_in->count;
  if (!list_in->append) copy = NULL;
  if (!count)
    return copy ? NULL : &blank_idarr; // NULL if !count for non-update
  if (copy) count += copy->count;
  SGSProgramIDArr *idarr = malloc(sizeof(SGSProgramIDArr) +
                                  sizeof(uint32_t) * count);
  if (!idarr)
    return NULL;
  idarr->count = count;
  uint32_t i = 0;
  if (copy) {
    memcpy(idarr->ids, copy->ids, sizeof(uint32_t) * copy->count);
    i = copy->count;
  }
  uint32_t *ids = &idarr->ids[i];
  copy_list_ids(&ids, list_in);
  return idarr;
}

/*
 * Program (event, voice, operator) allocation
 */

typedef struct VoiceAllocData {
  SGSEventNode *last;
  uint32_t duration_ms;
} VoiceAllocData;

typedef struct VoiceAlloc {
  VoiceAllocData *data;
  uint32_t voicec;
  uint32_t alloc;
} VoiceAlloc;

static void voice_alloc_init(VoiceAlloc *va) {
  va->data = calloc(1, sizeof(VoiceAllocData));
  va->voicec = 0;
  va->alloc = 1;
}

static void voice_alloc_fini(VoiceAlloc *va, SGSProgram *prg) {
  prg->voicec = va->voicec;
  free(va->data);
}

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint32_t voice_duration(SGSEventNode *ve) {
  uint32_t duration_ms = 0;
  for (SGSOperatorNode *op = ve->operators.first_on; op; op = op->next) {
    if (op->time_ms > (int32_t)duration_ms)
      duration_ms = op->time_ms;
  }
  return duration_ms;
}

/*
 * Incremental voice allocation - allocate voice for event,
 * returning voice id.
 */
static uint32_t voice_alloc_inc(VoiceAlloc *va, SGSEventNode *e) {
  uint32_t voice;
  for (voice = 0; voice < va->voicec; ++voice) {
    if ((int32_t)va->data[voice].duration_ms < e->wait_ms)
      va->data[voice].duration_ms = 0;
    else
      va->data[voice].duration_ms -= e->wait_ms;
  }
  if (e->voice_prev != NULL) {
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
  SGSOperatorNode *last;
  SGSProgramEvent *out;
  const SGSProgramIDArr *amods, *fmods, *pmods;
  uint32_t duration_ms;
} OperatorAllocData;

typedef struct OperatorAlloc {
  OperatorAllocData *data;
  uint32_t operatorc;
  uint32_t alloc;
} OperatorAlloc;

static void operator_alloc_init(OperatorAlloc *oa) {
  oa->data = calloc(1, sizeof(OperatorAllocData));
  oa->operatorc = 0;
  oa->alloc = 1;
}

static void operator_alloc_fini(OperatorAlloc *oa, SGSProgram *prg) {
  prg->operatorc = oa->operatorc;
  free(oa->data);
}

/*
 * Incremental operator allocation - allocate operator for event,
 * returning operator id.
 *
 * Only valid to call for single-operator nodes.
 */
static uint32_t operator_alloc_inc(OperatorAlloc *oa, SGSOperatorNode *op) {
  SGSEventNode *e = op->event;
  uint32_t operator;
  for (operator = 0; operator < oa->operatorc; ++operator) {
    if ((int32_t)oa->data[operator].duration_ms < e->wait_ms)
      oa->data[operator].duration_ms = 0;
    else
      oa->data[operator].duration_ms -= e->wait_ms;
  }
  if (op->on_prev != NULL) {
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
  SGSProgramEvent *oe, **oevents;
  size_t eventc;
  size_t alloc;
  OperatorAlloc oa;
  VoiceAlloc va;
} ProgramAlloc;

static void program_alloc_init(ProgramAlloc *pa) {
  voice_alloc_init(&(pa)->va);
  operator_alloc_init(&(pa)->oa);
  pa->oe = 0;
  pa->oevents = 0;
  pa->eventc = 0;
  pa->alloc = 0;
}

static void program_alloc_fini(ProgramAlloc *pa, SGSProgram *prg) {
  size_t i;
  /* copy output events to program & cleanup */
  *(SGSProgramEvent**)&prg->events = malloc(sizeof(SGSProgramEvent) *
                                              pa->eventc);
  for (i = 0; i < pa->eventc; ++i) {
    *(SGSProgramEvent*)&prg->events[i] = *pa->oevents[i];
    free(pa->oevents[i]);
  }
  free(pa->oevents);
  prg->eventc = pa->eventc;
  operator_alloc_fini(&pa->oa, prg);
  voice_alloc_fini(&pa->va, prg);
}

static SGSProgramEvent *program_alloc_oevent(ProgramAlloc *pa,
		uint32_t voice_id) {
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
 * Convert data for an operator node to program operator data, setting it for
 * the program event given.
 */
static void program_convert_onode(ProgramAlloc *pa, SGSOperatorNode *op,
                                  uint32_t operator_id) {
  OperatorAllocData *oa = &pa->oa.data[operator_id];
  SGSProgramEvent *oe = oa->out;
  SGSProgramOperatorData *ood = calloc(1, sizeof(SGSProgramOperatorData));
  oe->operator = ood;
  oe->params |= op->operator_params;
  //printf("operator_id == %d | address == %x\n", op->operator_id, op);
  ood->operator_id = operator_id;
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
  if (op->amods)
    ood->amods = oa->amods = SGS_create_ProgramIDArr(op->amods, oa->amods);
  if (op->fmods)
    ood->fmods = oa->fmods = SGS_create_ProgramIDArr(op->fmods, oa->fmods);
  if (op->pmods)
    ood->pmods = oa->pmods = SGS_create_ProgramIDArr(op->pmods, oa->pmods);
}

/*
 * Visit each operator node in the node list and recurse through each node's
 * sublists in turn, following and converting operator data and allocating
 * new output events as needed.
 */
static void program_follow_onodes(ProgramAlloc *pa, SGSListNode *op_list) {
  if (!op_list)
    return;
  if (op_list->append) program_follow_onodes(pa, op_list->prev);
  for (SGSOperatorNode *op = op_list->first_on; op; op = op->next) {
    OperatorAllocData *ad;
    uint32_t operator_id;
    if ((op->on_flags & ON_MULTIPLE_OPERATORS) != 0) continue;
    operator_id = operator_alloc_inc(&pa->oa, op);
    program_follow_onodes(pa, op->fmods);
    program_follow_onodes(pa, op->pmods);
    program_follow_onodes(pa, op->amods);
    ad = &pa->oa.data[operator_id];
    if (pa->oe->operator != NULL) {
      uint32_t voice_id = pa->oe->voice_id;
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
  if (e->voice_params != 0) {
    ovd = calloc(1, sizeof(SGSProgramVoiceData));
    oe->voice = ovd;
    oe->params |= e->voice_params;
    ovd->attr = e->voice_attr;
    ovd->panning = e->panning;
    ovd->valitpanning = e->valitpanning;
    if ((e->voice_params & SGS_P_GRAPH) != 0) {
      ovd->graph = SGS_create_ProgramIDArr(&e->graph, NULL);
    }
  }
}

static void print_linked(const char *header,
		const SGSProgramIDArr *idarr) {
  if (!idarr || !idarr->count) return;
  printf("%s[%d", header, idarr->ids[0]);
  for (uint32_t i = 0; ++i < idarr->count; )
    printf(", %d", idarr->ids[i]);
  printf("]");
}

/*
 * Create program for the given parser output.
 *
 * Return program if successful, NULL on error.
 */
static SGSProgram* build_program(SGSParserResult *pr) {
  //puts("build_program():");
  ProgramAlloc pa;
  SGSProgram *prg = calloc(1, sizeof(SGSProgram));
  SGSEventNode *e;
  size_t event_id;
  /*
   * Output event allocation, voice allocation,
   * parameter data copying.
   */
  program_alloc_init(&pa);
  for (e = pr->events; e; e = e->next) {
    program_convert_enode(&pa, e);
  }
  program_alloc_fini(&pa, prg);
  if (!(pr->sopt.set & SGS_SOPT_AMPMULT)) {
    /*
     * Enable amplitude scaling (division) by voice count,
     * handled by sound generator.
     */
    prg->flags |= SGS_PROG_AMP_DIV_VOICES;
  }
  //puts("/build_program()");
#if 1
  /*
   * Debug printing.
   */
  putchar('\n');
  printf("events: %ld\tvoices: %d\toperators: %d\n", prg->eventc, prg->voicec, prg->operatorc);
  for (event_id = 0; event_id < prg->eventc; ++event_id) {
    const SGSProgramEvent *oe;
    const SGSProgramVoiceData *ovo;
    const SGSProgramOperatorData *oop;
    oe = &prg->events[event_id];
    ovo = oe->voice;
    oop = oe->operator;
    printf("\\%d \tEV %ld \t(VI %d)", oe->wait_ms, event_id, oe->voice_id);
    if (ovo != NULL) {
      printf("\n\tvo %d", oe->voice_id);
      print_linked("\n\t    ", ovo->graph);
    }
    if (oop != NULL) {
      if (oop->time_ms == SGS_TIME_INF)
        printf("\n\top %d \tt=INF \tf=%.f", oop->operator_id, oop->freq);
      else
        printf("\n\top %d \tt=%d \tf=%.f", oop->operator_id, oop->time_ms, oop->freq);
      print_linked("\n\t    a,w", oop->amods);
      print_linked("\n\t    f,w", oop->fmods);
      print_linked("\n\t    p", oop->pmods);
    }
    putchar('\n');
  }
#endif
  return prg;
}

#if TEST_LEXER
#include "lexer.h"
#endif
/**
 * Create program for the given script file. Invokes the parser.
 *
 * Return SGSProgram if successful, NULL on error.
 */
SGSProgram* SGS_open_program(const char *filename) {
#if TEST_LEXER
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
#else // OLD PARSER
  SGSParserResult *pr = SGSParser_parse(filename);
  if (!pr) return NULL;

  SGSProgram *prg = build_program(pr);
  SGSParser_destroy_result(pr);
  return prg;
#endif
}

static void free_nonblank(void *o) {
  if (o != &blank_idarr) free(o);
}

/**
 * Destroy the SGSProgram instance.
 */
void SGS_close_program(SGSProgram *o) {
  size_t i;
  for (i = 0; i < o->eventc; ++i) {
    SGSProgramEvent *e = (void*)&o->events[i];
    if (e->voice != NULL) {
      free_nonblank((void*)e->voice->graph);
      free((void*)e->voice);
    }
    if (e->operator != NULL) {
      free_nonblank((void*)e->operator->amods);
      free_nonblank((void*)e->operator->fmods);
      free_nonblank((void*)e->operator->pmods);
      free((void*)e->operator);
    }
  }
  free((void*)o->events);
}
