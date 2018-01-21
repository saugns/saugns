#include "mgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uchar type, flag;
  int ref; /* if -1, uninitialized; may change to other index if node moved */
} IndexNode;

typedef struct SoundNode {
  uint time;
  uchar type, attr, mode;
  short *osctype;
  MGSOsc osc;
  float freq, dynfreq, amp, dynampdiff;
  struct SoundNode *amodchain, *fmodchain, *pmodchain;
  struct SoundNode *link;
} SoundNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct SetNode {
  uchar values, mods;
  Data data[1]; /* sized for number of things set */
} SetNode;

static uint count_flags(uint flags) {
  uint i, count = 0;
  for (i = 0; i < (8 * sizeof(uint)); ++i) {
    if (flags & 1) ++count;
    flags >>= 1;
  }
  return count;
}

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
  uint i, size, indexsize, nodessize;
  size = sizeof(MGSGenerator) - sizeof(IndexNode);
  indexsize = sizeof(IndexNode) * prg->nodec;
  nodessize = 0;
  for (step = prg->nodelist; step; step = step->next) {
    if (step->type == MGS_TYPE_TOP ||
        step->type == MGS_TYPE_NESTED)
      nodessize += sizeof(SoundNode);
    else if (step->type == MGS_TYPE_SETTOP ||
             step->type == MGS_TYPE_SETNESTED)
      nodessize += sizeof(SetNode) +
                   (sizeof(Data) *
                    (count_flags((step->spec.set.values << 8) |
                                 step->spec.set.mods) - 1));
  }
  o = calloc(1, size + indexsize + nodessize);
  o->srate = srate;
  o->osc_coeff = MGSOsc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->topc; /* only loop top-level nodes */
  data = (void*)(((uchar*)o) + size + indexsize);
  MGSOsc_init();
  step = prg->nodelist;
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    uint delay = step->delay * srate;
    in->node = data;
    in->pos = -delay;
    in->type = step->type;
    in->flag = step->flag;
    if (step->type == MGS_TYPE_TOP ||
        step->type == MGS_TYPE_NESTED) {
      SoundNode *n = data;
      uint time = step->time * srate;
      in->ref = -1;
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
      /* mods init part one - replaced with proper entries next loop */
      n->amodchain = (void*)step->amod.chain;
      n->fmodchain = (void*)step->fmod.chain;
      n->pmodchain = (void*)step->pmod.chain;
      n->link = (void*)step->spec.nested.link;
      data = (void*)(((uchar*)data) + sizeof(SoundNode));
    } else if (step->type == MGS_TYPE_SETTOP ||
               step->type == MGS_TYPE_SETNESTED) {
      SetNode *n = data;
      Data *set = n->data;
      MGSProgramNode *ref = step->spec.set.ref;
      in->ref = ref->id;
      if (ref->type == MGS_TYPE_NESTED)
        in->ref += prg->topc;
      n->values = step->spec.set.values;
      n->values &= ~MGS_DYNAMP;
      n->mods = step->spec.set.mods;
      if (n->values & MGS_TIME) {
        (*set).i = step->time * srate; ++set;
      }
      if (n->values & MGS_FREQ) {
        (*set).f = step->freq; ++set;
      }
      if (n->values & MGS_DYNFREQ) {
        (*set).f = step->dynfreq; ++set;
      }
      if (n->values & MGS_PHASE) {
        (*set).i = MGSOsc_PHASE(step->phase); ++set;
      }
      if (n->values & MGS_AMP) {
        (*set).f = step->amp; ++set;
      }
      if ((step->dynamp - step->amp) != (ref->dynamp - ref->amp)) {
        (*set).f = (step->dynamp - step->amp); ++set;
        n->values |= MGS_DYNAMP;
      }
      if (n->values & MGS_ATTR) {
        (*set).i = step->attr; ++set;
      }
      if (n->mods & MGS_AMODS) {
        (*set).i = step->amod.chain->id + prg->topc;
        ++set;
      }
      if (n->mods & MGS_FMODS) {
        (*set).i = step->fmod.chain->id + prg->topc;
        ++set;
      }
      if (n->mods & MGS_PMODS) {
        (*set).i = step->pmod.chain->id + prg->topc;
        ++set;
      }
      data = (void*)(((uchar*)data) +
                     sizeof(SetNode) +
                     (sizeof(Data) *
                      (count_flags((step->spec.set.values << 8) |
                                   step->spec.set.mods) - 1)));
    }
    step = step->next;
  }
  /* mods init part two - give proper entries */
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->type == MGS_TYPE_TOP ||
        in->type == MGS_TYPE_NESTED) {
      SoundNode *n = in->node;
      if (n->amodchain) {
        uint id = ((MGSProgramNode*)n->amodchain)->id + prg->topc;
        n->amodchain = o->nodes[id].node;
      }
      if (n->fmodchain) {
        uint id = ((MGSProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->nodes[id].node;
      }
      if (n->pmodchain) {
        uint id = ((MGSProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->nodes[id].node;
      }
      if (n->link) {
        uint id = ((MGSProgramNode*)n->link)->id + prg->topc;
        n->link = o->nodes[id].node;
      }
    }
  }
  return o;
}

static void adjust_time(MGSGenerator *o, SoundNode *n) {
  int pos_offs;
  /* click reduction: increase time to make it end at wave cycle's end */
  MGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->freq, n->time, pos_offs);
  n->time -= pos_offs;
  if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
    o->delay_offs = pos_offs;
}

static void MGSGenerator_enter_node(MGSGenerator *o, IndexNode *in) {
  switch (in->type) {
  case MGS_TYPE_TOP:
    adjust_time(o, in->node);
  case MGS_TYPE_NESTED:
    break;
  case MGS_TYPE_SETTOP:
  case MGS_TYPE_SETNESTED: {
    IndexNode *refin = &o->nodes[in->ref];
    SoundNode *refn = refin->node;
    SetNode *setn = in->node;
    Data *data = setn->data;
    uchar adjtime = 0;
    /* set state */
    if (setn->values & MGS_TIME) {
      refn->time = (*data).i; ++data;
      refin->pos = 0;
      if (refn->time) {
        if (refin->type == MGS_TYPE_TOP)
          refin->flag |= MGS_FLAG_EXEC;
        adjtime = 1;
      } else
        refin->flag &= ~MGS_FLAG_EXEC;
    }
    if (setn->values & MGS_FREQ) {
      refn->freq = (*data).f; ++data;
      adjtime = 1;
    }
    if (setn->values & MGS_DYNFREQ) {
      refn->dynfreq = (*data).f; ++data;
    }
    if (setn->values & MGS_PHASE) {
      MGSOsc_SET_PHASE(&refn->osc, (uint)(*data).i); ++data;
    }
    if (setn->values & MGS_AMP) {
      refn->amp = (*data).f; ++data;
    }
    if (setn->values & MGS_DYNAMP) {
      refn->dynampdiff = (*data).f; ++data;
    }
    if (setn->values & MGS_ATTR) {
      refn->attr = (uchar)(*data).i; ++data;
    }
    if (setn->mods & MGS_AMODS) {
      refn->amodchain = o->nodes[(*data).i].node; ++data;
    }
    if (setn->mods & MGS_FMODS) {
      refn->fmodchain = o->nodes[(*data).i].node; ++data;
    }
    if (setn->mods & MGS_PMODS) {
      refn->pmodchain = o->nodes[(*data).i].node; ++data;
    }
    if (adjtime && refn->type == MGS_TYPE_TOP)
      /* here so new freq also used if set */
      adjust_time(o, refn);
    /* take over place of ref'd node */
    *in = *refin;
    refin->flag &= ~MGS_FLAG_EXEC;
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

static float run_waveenv_sample(SoundNode *n, float freq_mult, double osc_coeff);

static uint run_sample(SoundNode *n, float freq_mult, double osc_coeff) {
  int ret = 0, s;
  float freq;
  float amp;
  int pm = 0;
  do {
    freq = n->freq;
    if (n->attr & MGS_ATTR_FREQRATIO)
      freq *= freq_mult;
    amp = n->amp;
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

static float run_waveenv_sample(SoundNode *n, float freq_mult, double osc_coeff) {
  float ret = 1.f, s;
  float freq;
  int pm = 0;
  do {
    freq = n->freq;
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

static uint run_node(SoundNode *n, short *sp, uint pos, uint len, double osc_coeff) {
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
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      in->pos = 0;
    } else
    if (!(in->flag & MGS_FLAG_ENTERED))
      MGSGenerator_enter_node(o, in);
    if (in->flag & MGS_FLAG_EXEC) {
      SoundNode *n = in->node;
      in->pos += run_node(n, buf, in->pos, len, o->osc_coeff);
      if ((uint)in->pos == n->time)
        in->flag &= ~MGS_FLAG_EXEC;
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
    if (o->nodes[o->node].flag & MGS_FLAG_EXEC)
      break;
    ++o->node;
  }
  return 1;
}
