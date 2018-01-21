#include "mgensys.h"
#include "osc.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSGeneratorNode {
  uint delay;
  uint time;
  uchar type, mode, active;
  short *osctype;
  union MGSGeneratorComponent *component;
  ui16_16 amp;
  float freq;
  int ref;
} MGSGeneratorNode;

typedef union MGSGeneratorComponent {
  MGSOsc osc;
} MGSGeneratorComponent;

#define NO_DELAY_OFFS (0x80000000)
struct MGSGenerator {
  const struct MGSProgram *program;
  MGSGeneratorComponent *components;
  uint srate;
  uint cpos;
  uint node, nodec;
  int delay_offs;
  MGSGeneratorNode nodes[1]; /* sized at construction */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o = calloc(1, sizeof(MGSGenerator) +
                              sizeof(MGSGeneratorNode) * (prg->stepc-1));
  MGSProgramNode *step;
  uint i, componentc = 0;
  MGSOsc_init();
  o->program = prg;
  o->srate = srate;
  o->nodec = prg->stepc;
  step = prg->steps;
  for (i = 0; i < o->nodec; ++i) {
    MGSGeneratorNode *n = &o->nodes[i];
    float amp = step->amp;
    uint delay = step->delay * srate;
    uint time = step->time * srate;
    float freq = step->freq;
    if (!step->ref)
      ++componentc;
    n->delay = delay;
    n->time = time;
    switch (step->wave) {
    case MGS_WAVE_SIN:
      n->osctype = MGSOsc_sin;
      break;
    case MGS_WAVE_SQR:
      n->osctype = MGSOsc_sqr;
      break;
    case MGS_WAVE_TRI:
      n->osctype = MGSOsc_tri;
      break;
    case MGS_WAVE_SAW:
      n->osctype = MGSOsc_saw;
      break;
    }
    n->type = step->type;
    n->mode = step->mode;
    SET_I16_162F(n->amp, amp);
    n->freq = freq;
    step = step->next;
  }
  o->components = calloc(componentc, sizeof(MGSGeneratorComponent));
  /* step through again to assign components */
  step = prg->steps;
  for (i = 0; i < o->nodec; ++i) {
    MGSGeneratorNode *n = &o->nodes[i];
    if (!step->ref) {
      n->ref = -1;
      n->component = &o->components[o->cpos++];
    } else {
      n->ref = step->ref->id;
      n->component = o->nodes[n->ref].component;
    }
    step = step->next;
  }
  return o;
}

static void MGSGenerator_enter_node(MGSGenerator *o, uint index) {
  MGSGeneratorComponent *comp;
  MGSGeneratorNode *node = &o->nodes[index];
  int pos_offs;
  comp = node->component;
  if (node->ref < 0) { /* beginning */
    MGSOsc_SET_COEFF(&comp->osc, node->freq, o->srate);
    MGSOsc_SET_PHASE(&comp->osc, 0);
    MGSOsc_SET_RANGE(&comp->osc, node->amp);
  } else { /* continuation */
    MGSGeneratorNode *ref = &o->nodes[node->ref];
    if (!(node->type & MGS_TYPE_SETTIME))
      node->time = ref->time;
    ref->time = 0;
    if (node->type & MGS_TYPE_SETAMP)
      MGSOsc_SET_RANGE(&comp->osc, node->amp);
    else
      node->amp = ref->amp;
    if (node->type & MGS_TYPE_SETFREQ)
      MGSOsc_SET_COEFF(&comp->osc, node->freq, o->srate);
    else
      node->freq = ref->freq;
  }
  node->active = 1;
  /* click reduction: increase time to make it end at wave cycle's end */
  MGSOsc_WAVE_OFFS(&comp->osc, node->time, pos_offs);
  node->time -= pos_offs;
  if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
    o->delay_offs = pos_offs;
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
    for (; len && n->time; --len, --n->time, sp += 2) {
      int s;
      MGSOsc_RUN(&c->osc, n->osctype, s);
      sp[0] += s;
      sp[1] += s;
    }
  } else {
    if (n->mode == MGS_MODE_RIGHT) ++sp;
    for (; len && n->time; --len, --n->time, sp += 2) {
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
  short *sp;
  uint i, skiplen;
PROCESS:
  sp = buf;
  skiplen = 0;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
  for (i = o->node; i < o->nodec; ++i) {
    MGSGeneratorNode *n = &o->nodes[i];
    if (n->delay) {
      uint delay = n->delay;
      if ((uint)o->delay_offs != NO_DELAY_OFFS)
        delay -= o->delay_offs; /* delay inc == previous time inc */
      if (delay <= len) {
        /* Split processing so that len is no longer than delay, avoiding
         * cases where the node prior to a node disabling it plays too
         * long.
         */
        skiplen = len - delay;
        len = delay;
      }
      break;
    }
    if (!n->active)
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node plays.
       */
      MGSGenerator_enter_node(o, i);
  }
  for (i = o->node; i < o->nodec; ++i) {
    MGSGeneratorNode *n = &o->nodes[i];
    if (n->delay) {
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        n->delay -= o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (n->delay >= len) {
        n->delay -= len;
        break; /* end for now; delays accumulate across steps */
      }
      len -= n->delay;
      buf += n->delay+n->delay; /* doubled due to stereo interleaving */
      n->delay = 0;
    }
    if (!n->active)
      MGSGenerator_enter_node(o, i);
    if (!n->time)
      continue;
    run_osc(n, buf, len);
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->nodec) return 0;
    if (o->nodes[o->node].time) break;
    ++o->node;
  }
  return 1;
}
