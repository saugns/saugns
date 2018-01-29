#include "mgensys.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

struct MGSGenerator {
  struct MGSProgram *program;
  struct MGSProgramNode *node;
  uint srate;
  uint cpos;
};

static void node_init(MGSGenerator *s) {
  MGSProgramComponent *comp;
  s->node->pos = 0;
  s->node->len = s->node->time * s->srate;
  switch (s->node->type) {
  case MGS_TYPE_SIN:
    comp = s->node->component = &s->program->components[s->cpos++];
    MGSSinOsc_SET_COEFF(&comp->sinosc, s->node->freq, s->srate);
    MGSSinOsc_SET_RANGE(&comp->sinosc, s->node->amp);
    break;
  case MGS_TYPE_SQR:
    comp = s->node->component = &s->program->components[s->cpos++];
    MGSSqrOsc_SET_COEFF(&comp->sqrosc, s->node->freq, s->srate);
    MGSSqrOsc_SET_RANGE(&comp->sqrosc, s->node->amp);
    break;
  case MGS_TYPE_TRI:
    comp = s->node->component = &s->program->components[s->cpos++];
    MGSTriOsc_SET_COEFF(&comp->triosc, s->node->freq, s->srate);
    MGSTriOsc_SET_RANGE(&comp->triosc, s->node->amp);
    break;
  case MGS_TYPE_SAW:
    comp = s->node->component = &s->program->components[s->cpos++];
    MGSSawOsc_SET_COEFF(&comp->sawosc, s->node->freq, s->srate);
    MGSSawOsc_SET_RANGE(&comp->sawosc, s->node->amp);
    break;
  }
  /* recurse to init other "voices"/parallel operations */
  if (s->node->pnext) {
    s->node = s->node->pnext;
    node_init(s);
    s->node = s->node->pfirst;
  }
}

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o = calloc(1, sizeof(MGSGenerator));
  o->program = prg;
  o->srate = srate;
  o->node = o->program->steps;
  node_init(o);
  return o;
}

void MGSGenerator_destroy(MGSGenerator *o) {
  free(o);
}

uchar MGSGenerator_run(MGSGenerator *s, short *buf, uint len) {
  uint i;
  MGSProgram *program = s->program;
  MGSProgramNode *n;
  short *p;
  for (p = buf; len-- > 0; p += 2) {
    p[0] = 0;
    p[1] = 0;
    if (s->node) {
      if (s->node->pos++ >= s->node->len) {
        s->node = s->node->snext;
        if (!s->node) break;
        node_init(s);
      }
    }
    for (n = s->node; n; n = n->pnext) {
      MGSProgramComponent *comp = n->component;
      switch (s->node->type) {
      case MGS_TYPE_WAIT:
        break;
      case MGS_TYPE_SIN:
        MGSSinOsc_RUN(&comp->sinosc);
        if (n->mode & MGS_MODE_LEFT)
          p[0] += comp->sinosc.sin * 16384;
        if (n->mode & MGS_MODE_RIGHT)
          p[1] += comp->sinosc.sin * 16384;
        break;
      case MGS_TYPE_SQR:
        MGSSqrOsc_RUN(&comp->sqrosc);
        if (n->mode & MGS_MODE_LEFT)
          p[0] += comp->sqrosc.sqr * 16384;
        if (n->mode & MGS_MODE_RIGHT)
          p[1] += comp->sqrosc.sqr * 16384;
        break;
      case MGS_TYPE_TRI:
        MGSTriOsc_RUN(&comp->triosc);
        if (n->mode & MGS_MODE_LEFT)
          p[0] += comp->triosc.tri * 16384;
        if (n->mode & MGS_MODE_RIGHT)
          p[1] += comp->triosc.tri * 16384;
        break;
      case MGS_TYPE_SAW:
        MGSSawOsc_RUN(&comp->sawosc);
        if (n->mode & MGS_MODE_LEFT)
          p[0] += comp->sawosc.saw * 16384;
        if (n->mode & MGS_MODE_RIGHT)
          p[1] += comp->sawosc.saw * 16384;
        break;
      }
    }
  }
  return (s->node != 0);
}
