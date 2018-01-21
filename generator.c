#include "mgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSGeneratorNode {
  uint time;
  uchar type, attr, mode;
  short *osctype;
  MGSOsc osc;
  float freq, dynfreq, amp, dynampdiff;
  struct MGSGeneratorNode *amodchain, *fmodchain, *pmodchain;
  struct MGSGeneratorNode *link;
} MGSGeneratorNode;

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uchar type, flag;
  int ref; /* if -1, uninitialized; may change to other index if node moved */
} IndexNode;

#define NO_DELAY_OFFS (0x80000000)
struct MGSGenerator {
  uint srate;
  double osc_coeff;
  int delay_offs;
  uint node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o;
  MGSProgramNode *step;
  void *data;
  uint i, j, size, indexsize, nodessize;
  size = sizeof(MGSGenerator) - sizeof(IndexNode);
  indexsize = sizeof(IndexNode) * prg->stepc;
  nodessize = 0;
  for (step = prg->steps; step; step = step->next) {
    if (step->type == MGS_TYPE_SOUND)
      nodessize += sizeof(MGSGeneratorNode);
  }
  o = calloc(1, size + indexsize + nodessize);
  o->srate = srate;
  o->osc_coeff = MGSOsc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->stepc;
  data = (void*)(((uchar*)o) + size + indexsize);
  MGSOsc_init();
  step = prg->steps;
  for (i = 0; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    uint delay = step->delay * srate;
    in->node = data;
    in->pos = -delay;
    in->type = step->type;
    in->flag = step->flag;
    if (step->ref)
      in->ref = step->ref->id;
    else
      in->ref = -1;
    if (step->type == MGS_TYPE_SOUND) {
      MGSGeneratorNode *n = data;
      uint time = step->time * srate;
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
      n->attr = step->attr;
      n->mode = step->mode;
      n->amp = step->amp;
      n->dynampdiff = step->dynamp - step->amp;
      n->freq = step->freq;
      n->dynfreq = step->dynfreq;
      MGSOsc_SET_PHASE(&n->osc, MGSOsc_PHASE(step->phase));
      /* mods init part one - replaced with proper entries below */
      n->amodchain = (void*)step->amod.chain;
      n->fmodchain = (void*)step->fmod.chain;
      n->pmodchain = (void*)step->pmod.chain;
      n->link = (void*)step->link;
      data = (void*)(((uchar*)data) + sizeof(MGSGeneratorNode));
    }
    step = step->next;
  }
  /* mods init part two - give proper entries */
  for (i = 0; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->type == MGS_TYPE_SOUND) {
      MGSGeneratorNode *n = in->node;
      if (n->amodchain) {
        step = (void*)n->amodchain;
        n->amodchain = o->nodes[step->id].node;
        n = n->amodchain;
        while ((step = (void*)n->link)) {
          n->link = o->nodes[step->id].node;
          n = n->link;
        }
      }
      if (n->fmodchain) {
        step = (void*)n->fmodchain;
        n->fmodchain = o->nodes[step->id].node;
        n = n->fmodchain;
        while ((step = (void*)n->link)) {
          n->link = o->nodes[step->id].node;
          n = n->link;
        }
      }
      if (n->pmodchain) {
        step = (void*)n->pmodchain;
        n->pmodchain = o->nodes[step->id].node;
        n = n->pmodchain;
        while ((step = (void*)n->link)) {
          n->link = o->nodes[step->id].node;
          n = n->link;
        }
      }
    }
  }
  return o;
}

static void MGSGenerator_enter_node(MGSGenerator *o, IndexNode *in) {
  switch (in->type) {
  case MGS_TYPE_SOUND: {
    int pos_offs;
    MGSGeneratorNode *n = in->node;
    if (in->ref > 0) { /* node continues tone */
      IndexNode *iref = &o->nodes[in->ref];
      MGSGeneratorNode *ref = iref->node;
      if (in->flag & MGS_FLAG_REFTIME) {
        n->time = ref->time;
        in->pos = iref->pos;
      }
      iref->pos = ref->time;
      if (in->flag & MGS_FLAG_REFPHASE)
        MGSOsc_SET_PHASE(&n->osc, MGSOsc_GET_PHASE(&ref->osc));
    }
    /* click reduction: increase time to make it end at wave cycle's end */
    MGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->freq, n->time, pos_offs);
    n->time -= pos_offs;
    if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
      o->delay_offs = pos_offs;
    break; }
  case MGS_TYPE_ENV:
    break;
  }
  in->flag |= MGS_FLAG_ENTERED;
}

void MGSGenerator_destroy(MGSGenerator *o) {
  free(o);
}

/*
 * node sample processing
 */

static float run_waveenv_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff);

static uint run_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff) {
  uint i;
  int ret = 0, s;
  float freq = n->freq;
  float amp = n->amp;
  int pm = 0;
  do {
    if (n->attr & MGS_ATTR_FREQRATIO)
      freq *= freq_mult;
    if (n->amodchain) {
      float am = n->dynampdiff;
      am *= run_waveenv_sample(n->amodchain, freq, osc_coeff);
      amp += am;
    }
    if (n->fmodchain) {
      float fm = n->dynfreq;
      if (n->attr & MGS_ATTR_DYNFREQRATIO)
        fm *= freq_mult;
      fm -= freq;
      fm *= run_waveenv_sample(n->fmodchain, freq, osc_coeff);
      freq += fm;
    }
    if (n->pmodchain)
      pm += run_sample(n->pmodchain, freq, osc_coeff);
    MGSOsc_RUN_PM(&n->osc, n->osctype, osc_coeff, freq, pm, amp, s);
    ret += s;
  } while ((n = n->link));
  return ret;
}

static float run_waveenv_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff) {
  uint i;
  float ret = 1.f, s;
  float freq = n->freq;
  int pm = 0;
  do {
    if (n->attr & MGS_ATTR_FREQRATIO)
      freq *= freq_mult;
    if (n->fmodchain) {
      float fm = n->dynfreq;
      if (n->attr & MGS_ATTR_DYNFREQRATIO)
        fm *= freq_mult;
      fm -= freq;
      fm *= run_waveenv_sample(n->fmodchain, freq, osc_coeff);
      freq += fm;
    }
    if (n->pmodchain)
      pm += run_sample(n->pmodchain, freq, osc_coeff);
    MGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, osc_coeff, freq, pm, s);
    ret *= s;
  } while ((n = n->link));
  return ret;
}

/*
 * node block processing
 */

static uint run_node(MGSGeneratorNode *n, short *sp, uint pos, uint len, double osc_coeff) {
  uint ret, time = n->time - pos;
  if (time > len)
    time = len;
  ret = time;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  for (; time; --time, sp += 2) {
    int s;
    s = run_sample(n, /* dummy value */0.f, osc_coeff);
    sp[0] += s;
    if (n->mode == MGS_MODE_CENTER)
      sp[1] += s;
  }
  return ret;
}

/*
 * main run-function
 */

uchar MGSGenerator_run(MGSGenerator *o, short *buf, uint len) {
  short *sp;
  uint i, skiplen;
  sp = buf;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
PROCESS:
  skiplen = 0;
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
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
    if (!(in->flag & MGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGSGenerator_enter_node(o, in);
  }
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        in->pos += o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (delay >= len) {
        in->pos += len;
        break; /* end for now; delays accumulate across steps */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      in->pos = 0;
    } else
    if (!(in->flag & MGS_FLAG_ENTERED))
      MGSGenerator_enter_node(o, in);
    if (in->flag & MGS_FLAG_PLAY) {
      MGSGeneratorNode *n = in->node;
      in->pos += run_node(n, buf, in->pos, len, o->osc_coeff);
      if ((uint)in->pos == n->time)
        in->flag &= ~MGS_FLAG_PLAY;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->nodec)
      return 0;
    if (o->nodes[o->node].flag & MGS_FLAG_PLAY)
      break;
    ++o->node;
  }
  return 1;
}
