#include "mgensys.h"
#include "osc.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSGeneratorNode {
  int pos;
  uint time;
  uchar mode, active;
  short *osctype;
  union MGSGeneratorComponent *component;
  ui16_16 amp;
  float freq;
} MGSGeneratorNode;

typedef union MGSGeneratorComponent {
  MGSOsc osc;
} MGSGeneratorComponent;

struct MGSGenerator {
  const struct MGSProgram *program;
  MGSGeneratorComponent *components;
  uint srate;
  uint cpos;
  uint node, nodec;
  MGSGeneratorNode nodes[1]; /* sized at construction */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o = calloc(1, sizeof(MGSGenerator) +
                              sizeof(MGSGeneratorNode) * (prg->stepc-1));
  MGSProgramNode *step;
  uint i;
  MGSOsc_init();
  o->program = prg;
  o->srate = srate;
  o->nodec = prg->stepc;
  step = prg->steps;
  for (i = 0; i < o->nodec; ++i) {
    float amp = step->amp;
    uint delay = step->delay * srate;
    uint time = step->time * srate;
    float freq = step->freq;
    o->nodes[i].pos = -delay;
    o->nodes[i].time = time;
    switch (step->wave) {
    case MGS_WAVE_SIN:
      o->nodes[i].osctype = MGSOsc_sin;
      break;
    case MGS_WAVE_SQR:
      o->nodes[i].osctype = MGSOsc_sqr;
      break;
    case MGS_WAVE_TRI:
      o->nodes[i].osctype = MGSOsc_tri;
      break;
    case MGS_WAVE_SAW:
      o->nodes[i].osctype = MGSOsc_saw;
      break;
    }
    o->nodes[i].mode = step->mode;
    SET_I16_162F(o->nodes[i].amp, amp);
    o->nodes[i].freq = freq;
    step = step->next;
  }
  o->components = calloc(prg->componentc, sizeof(MGSGeneratorComponent));
  return o;
}

static void MGSGenerator_enter_node(MGSGenerator *o, uint index) {
  MGSGeneratorComponent *comp;
  MGSGeneratorNode *node = &o->nodes[index];
  comp = node->component = &o->components[o->cpos++];
  MGSOsc_SET_COEFF(&comp->osc, node->freq, o->srate);
  MGSOsc_SET_PHASE(&comp->osc, 0);
  MGSOsc_SET_RANGE(&comp->osc, node->amp);
  node->active = 1;
}

void MGSGenerator_destroy(MGSGenerator *o) {
  free(o->components);
  free(o);
}

/*
 * oscillator block processing
 */

uint run_osc(MGSGeneratorNode *n, short *sp, uint len) {
  MGSGeneratorComponent *c = n->component;
  if (n->mode == MGS_MODE_CENTER) {
    for (; len-- && n->pos++ < (int)n->time; sp += 2) {
      int s;
      MGSOsc_RUN(&c->osc, n->osctype, s);
      sp[0] += s;
      sp[1] += s;
    }
  } else {
    if (n->mode == MGS_MODE_RIGHT) ++sp;
    for (; len-- && n->pos++ < (int)n->time; sp += 2) {
      int s;
      MGSOsc_RUN(&c->osc, n->osctype, s);
      sp[0] += s;
    }
  }
  return len;
}

/*
 * main run-function
 */

uchar MGSGenerator_run(MGSGenerator *o, short *buf, uint len) {
  short *sp = buf;
  uint i;
  MGSGeneratorNode *n;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
  for (i = o->node; i < o->nodec; ++i) {
    n = &o->nodes[i];
    if (n->pos < 0) {
      uint offs = - n->pos;
      if (offs >= len) {
        n->pos += len;
        break; /* end for now; delays accumulate across steps */
      }
      n->pos += offs;
      len -= offs;
      buf += offs+offs; /* doubled due to stereo interleaving */
    } else if (n->pos == (int)n->time)
      continue;
    if (!n->active)
      MGSGenerator_enter_node(o, i);
    run_osc(n, buf, len);
  }
  for(;;) {
    if (o->node == o->nodec) return 0;
    n = &o->nodes[o->node];
    if (n->pos < (int)n->time) break;
    ++o->node;
  }
  return 1;
}
