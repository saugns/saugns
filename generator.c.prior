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
  MGSOsc osc;
  float freq;
  struct MGSGeneratorNode *ref;
  struct MGSGeneratorNode *mods[1]; /* sized to modc */
} MGSGeneratorNode;

#define MGSGeneratorNode_NEXT(o) \
  (((MGSGeneratorNode*)(((uchar*)(o)) + \
                        ((o)->modc-1) * sizeof(MGSGeneratorNode*))) + \
   1)

#define NO_DELAY_OFFS (0x80000000)
struct MGSGenerator {
  const struct MGSProgram *program;
  uint srate;
  double osc_coeff;
  int delay_offs;
  MGSGeneratorNode *node, *end,
                   *nodes; /* node size varies, so no direct indexing! */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o;
  MGSProgramNode *step;
  MGSGeneratorNode *n, *ref;
  uint i, j, size, nodessize;
  size = sizeof(MGSGenerator);
  nodessize = sizeof(MGSGeneratorNode) * prg->stepc;
  for (step = prg->steps; step; step = step->next)
    nodessize += (step->modc-1) * sizeof(MGSGeneratorNode*);
  o = calloc(1, size + nodessize);
  o->program = prg;
  o->srate = srate;
  o->osc_coeff = MGSOsc_COEFF(srate);
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
    } else {
      ref = o->nodes;
      for (i = step->ref->id; i; --i)
        ref = MGSGeneratorNode_NEXT(ref);
      n->ref = ref;
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
    MGSOsc_SET_AMP(&n->osc, amp);
    n->freq = freq;
    n->modc = step->modc;
    for (i = 0; i < n->modc; ++i)
      n->mods[i] = (void*)step->mods[i]; /* replace with proper entry below */
    step = step->next;
  }
  for (n = o->nodes, j = 0; n != o->end; n = MGSGeneratorNode_NEXT(n), ++j) {
    if (n->modc) {
      for (i = 0; i < n->modc; ++i) {
        uint id = 0;
        ref = o->nodes;
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
  int pos_offs;
  switch (n->type) {
  case MGS_TYPE_WAVE:
    if (!n->ref) { /* beginning */
      MGSOsc_SET_FREQ(&n->osc, n->freq);
      MGSOsc_SET_PHASE(&n->osc, 0);
    } else { /* continuation */
      MGSGeneratorNode *ref = n->ref;
      if (n->flag & MGS_FLAG_REFTIME) {
        n->time = ref->time;
        n->pos = ref->pos;
      }
      ref->pos = ref->time;
      if (n->flag & MGS_FLAG_REFFREQ)
        n->freq = ref->freq;
      MGSOsc_SET_FREQ(&n->osc, n->freq);
      MGSOsc_SET_PHASE(&n->osc, ref->osc.phase);
      if (n->flag & MGS_FLAG_REFAMP)
        MGSOsc_SET_AMP(&n->osc, ref->osc.amp);
    }
    /* click reduction: increase time to make it end at wave cycle's end */
    MGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->time, pos_offs);
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
  free(o);
}

/*
 * node sample processing
 */

static uint run_pm(MGSGeneratorNode *n, double osc_coeff) {
  uint i;
  int s = 0;
  for (i = 0; i < n->modc; ++i) {
    MGSGeneratorNode *mn = n->mods[i];
    if (mn->flag & MGS_FLAG_FREQRATIO)
      MGSOsc_SET_FREQ(&mn->osc, (n->freq * mn->freq));
    if (mn->modc)
      s += run_pm(mn, osc_coeff);
    else {
      int v;
      MGSOsc_RUN(&mn->osc, mn->osctype, osc_coeff, v);
      s += v;
    }
  }
  MGSOsc_RUN_PM(&n->osc, n->osctype, osc_coeff, s, s);
  return s;
}

/*
 * node block processing
 */

static void run_node(MGSGeneratorNode *n, short *sp, uint len, double osc_coeff) {
  uint time = n->time - n->pos;
  if (time > len)
    time = len;
  n->pos += time;
  if (n->type != MGS_TYPE_WAVE) return;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  if (n->modc) {
    for (; time; --time, sp += 2) {
      int s;
      s = run_pm(n, osc_coeff);
      sp[0] += s;
      if (n->mode == MGS_MODE_CENTER)
        sp[1] += s;
    }
  } else {
    for (; time; --time, sp += 2) {
      int s;
      MGSOsc_RUN(&n->osc, n->osctype, osc_coeff, s);
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
      run_node(n, buf, len, o->osc_coeff);
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
