#include "mgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSGeneratorNode {
  int pos;
  uint time;
  uchar type, flag, mode;
  uchar modc;
  short *osctype;
  union MGSGeneratorComponent *component;
  ui16_16 amp;
  float freq;
  struct MGSGeneratorNode *ref;
  struct MGSGeneratorNode *mods[1]; /* sized to modc */
} MGSGeneratorNode;

#define MGSGeneratorNode_NEXT(o) \
  (((MGSGeneratorNode*)(((uchar*)(o)) + \
                        ((o)->modc-1) * sizeof(MGSGeneratorNode*))) + \
   1)

typedef union MGSGeneratorComponent {
  MGSOsc osc;
} MGSGeneratorComponent;

#define NO_DELAY_OFFS (0x80000000)
struct MGSGenerator {
  const struct MGSProgram *program;
  uint srate;
  int delay_offs;
  MGSGeneratorNode *node, *end,
                   *nodes; /* node size varies, so no direct indexing! */
  MGSGeneratorComponent components[1]; /* sized at construction */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o;
  MGSProgramNode *step;
  MGSGeneratorNode *n, *ref;
  uint i, j, size, nodessize, component = 0, componentc = 0;
  size = sizeof(MGSGenerator);
  nodessize = sizeof(MGSGeneratorNode) * prg->stepc;
  for (step = prg->steps; step; step = step->next) {
    nodessize += (step->modc-1) * sizeof(MGSGeneratorNode*);
    if (!step->ref)
      ++componentc;
  }
  size += sizeof(MGSGeneratorComponent) * (componentc-1);
  o = calloc(1, size + nodessize);
  o->program = prg;
  o->srate = srate;
  o->node = o->nodes = (void*)(((uchar*)o) + size);
  o->end = (void*)(((uchar*)o->node) + nodessize);
  MGSOsc_init();
  step = prg->steps;
  for (n = o->nodes; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    float amp = step->amp;
    uint delay = step->delay * srate;
    uint time = step->time * srate;
    float freq = step->freq;
    if (!step->ref) {
      n->ref = 0;
      n->component = &o->components[component++];
    } else {
      ref = o->nodes;
      for (i = step->ref->id; i; --i)
        ref = MGSGeneratorNode_NEXT(ref);
      n->ref = ref;
      n->component = ref->component;
    }
    n->pos = -delay;
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
    n->flag = step->flag;
    n->mode = step->mode;
    SET_I16_162F(n->amp, amp);
    n->freq = freq;
    n->modc = step->modc;
    for (i = 0; i < n->modc; ++i)
      n->mods[i] = (void*)step->mods[i]; /* replace with proper entry below */
    step = step->next;
  }
  for (n = o->nodes, j = 0; n != o->end; n = MGSGeneratorNode_NEXT(n), ++j) {
    if (n->modc) {
      for (i = 0; i < n->modc; ++i) {
        /* Seeking forward from the present node will work as modulator nodes
         * are always placed after the modulated node.
         */
        uint id = j;
        ref = n;
        step = (void*)n->mods[i];
        while (id < step->id) {
          ref = MGSGeneratorNode_NEXT(ref);
          ++id;
        }
        n->mods[i] = ref; /* now given proper entry */
      }
    }
  }
  return o;
}

static void MGSGenerator_enter_node(MGSGenerator *o, MGSGeneratorNode *n) {
  MGSGeneratorComponent *comp;
  int pos_offs;
  comp = n->component;
  switch (n->type) {
  case MGS_TYPE_WAVE:
    if (!n->ref) { /* beginning */
      MGSOsc_SET_COEFF(&comp->osc, n->freq, o->srate);
      MGSOsc_SET_PHASE(&comp->osc, 0);
      MGSOsc_SET_RANGE(&comp->osc, n->amp);
    } else { /* continuation */
      MGSGeneratorNode *ref = n->ref;
      if (!(n->flag & MGS_FLAG_SETTIME)) {
        n->time = ref->time;
        n->pos = ref->pos;
      }
      ref->pos = ref->time;
      if (n->flag & MGS_FLAG_SETAMP)
        MGSOsc_SET_RANGE(&comp->osc, n->amp);
      else
        n->amp = ref->amp;
      if (n->flag & MGS_FLAG_SETFREQ)
        MGSOsc_SET_COEFF(&comp->osc, n->freq, o->srate);
      else
        n->freq = ref->freq;
    }
    /* click reduction: increase time to make it end at wave cycle's end */
    MGSOsc_WAVE_OFFS(&comp->osc, n->time, pos_offs);
    n->time -= pos_offs;
    if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
      o->delay_offs = pos_offs;
    break;
  case MGS_TYPE_ENV:
    break;
  }
  n->flag |= MGS_FLAG_ENTERED;
}

void MGSGenerator_destroy(MGSGenerator *o) {
  free(o->components);
  free(o);
}

/*
 * node sample processing
 */

static uint run_pm(MGSGeneratorNode *n) {
  uint i;
  int s = 0;
  MGSGeneratorComponent *c = n->component;
  for (i = 0; i < n->modc; ++i) {
    MGSGeneratorNode *mn = n->mods[i];
    if (mn->modc)
      s += run_pm(mn);
    else {
      int v;
      MGSGeneratorComponent *c = mn->component;
      MGSOsc_RUN(&c->osc, mn->osctype, v);
      s += v;
    }
  }
  MGSOsc_RUN_PM(&c->osc, n->osctype, s, s);
  return s;
}

/*
 * node block processing
 */

static void run_node(MGSGeneratorNode *n, short *sp, uint len) {
  uint time = n->time - n->pos;
  if (time > len)
    time = len;
  n->pos += time;
  if (n->type != MGS_TYPE_WAVE) return;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  if (n->modc) {
    for (; time; --time, sp += 2) {
      int s;
      s = run_pm(n);
      sp[0] += s;
      if (n->mode == MGS_MODE_CENTER)
        sp[1] += s;
    }
  } else {
    MGSGeneratorComponent *c = n->component;
    for (; time; --time, sp += 2) {
      int s;
      MGSOsc_RUN(&c->osc, n->osctype, s);
      sp[0] += s;
      if (n->mode == MGS_MODE_CENTER)
        sp[1] += s;
    }
  }
  if ((uint)n->pos == n->time)
    n->flag &= ~MGS_FLAG_PLAY;
}

/*
 * main run-function
 */

uchar MGSGenerator_run(MGSGenerator *o, short *buf, uint len) {
  MGSGeneratorNode *n;
  short *sp;
  uint i, skiplen;
  sp = buf;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
PROCESS:
  skiplen = 0;
  for (n = o->node; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    if (n->pos < 0) {
      uint delay = -n->pos;
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
    if (!(n->flag & MGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGSGenerator_enter_node(o, n);
  }
  for (n = o->node; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    if (n->pos < 0) {
      uint delay = -n->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        n->pos += o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (delay >= len) {
        n->pos += len;
        break; /* end for now; delays accumulate across steps */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      n->pos = 0;
    } else
    if (!(n->flag & MGS_FLAG_ENTERED))
      MGSGenerator_enter_node(o, n);
    if (n->flag & MGS_FLAG_PLAY)
      run_node(n, buf, len);
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->end)
      return 0;
    if (o->node->flag & MGS_FLAG_PLAY)
      break;
    o->node = MGSGeneratorNode_NEXT(o->node);
  }
  return 1;
}
