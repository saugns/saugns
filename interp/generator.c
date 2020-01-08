/* mgensys: Audio generator.
 * Copyright (c) 2011, 2020 Joel K. Pettersson
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

#include "../mgensys.h"
#include "osc.h"
#include "env.h"
#include "../program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t type, flag;
  int ref; /* if -1, uninitialized; may change to other index if node moved */
} IndexNode;

typedef struct SoundNode {
  uint32_t time;
  uint8_t type, attr, mode;
  MGS_Osc osc;
  float freq, dynfreq, amp, dynampdiff;
  struct SoundNode *amodchain, *fmodchain, *pmodchain;
  struct SoundNode *link;
} SoundNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct SetNode {
  uint8_t values, mods;
  Data data[1]; /* sized for number of things set */
} SetNode;

static uint32_t count_flags(uint32_t flags) {
  uint32_t i, count = 0;
  for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
    if (flags & 1) ++count;
    flags >>= 1;
  }
  return count;
}

#define NO_DELAY_OFFS (0x80000000)
struct MGS_Generator {
  uint32_t srate;
  float osc_coeff;
  int delay_offs;
  uint32_t node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
};

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  MGS_ProgramNode *step;
  void *data;
  uint32_t i, size, indexsize, nodessize;
  size = sizeof(MGS_Generator) - sizeof(IndexNode);
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
  o->osc_coeff = MGS_Osc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->topc; /* only loop top-level nodes */
  data = (void*)(((uint8_t*)o) + size + indexsize);
  MGS_global_init_Wave();
  step = prg->nodelist;
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    uint32_t delay = step->delay * srate;
    in->node = data;
    in->pos = -delay;
    in->type = step->type;
    in->flag = step->flag;
    if (step->type == MGS_TYPE_TOP ||
        step->type == MGS_TYPE_NESTED) {
      SoundNode *n = data;
      uint32_t time = step->time * srate;
      in->ref = -1;
      n->time = time;
      n->attr = step->attr;
      n->mode = step->mode;
      n->amp = step->amp;
      n->dynampdiff = step->dynamp - step->amp;
      n->freq = step->freq;
      n->dynfreq = step->dynfreq;
      n->osc.phase = MGS_Osc_PHASE(step->phase);
      n->osc.coeff = o->osc_coeff;
      n->osc.lut = MGS_Osc_LUT(step->wave);
      /* mods init part one - replaced with proper entries next loop */
      n->amodchain = (void*)step->amod.chain;
      n->fmodchain = (void*)step->fmod.chain;
      n->pmodchain = (void*)step->pmod.chain;
      n->link = (void*)step->spec.nested.link;
      data = (void*)(((uint8_t*)data) + sizeof(SoundNode));
    } else if (step->type == MGS_TYPE_SETTOP ||
               step->type == MGS_TYPE_SETNESTED) {
      SetNode *n = data;
      Data *set = n->data;
      MGS_ProgramNode *ref = step->spec.set.ref;
      in->ref = ref->id;
      if (ref->type == MGS_TYPE_NESTED)
        in->ref += prg->topc;
      n->values = step->spec.set.values;
      n->values &= ~MGS_DYNAMP;
      n->mods = step->spec.set.mods;
      if (n->values & MGS_TIME) {
        (*set).i = step->time * srate; ++set;
      }
      if (n->values & MGS_WAVE) {
        (*set).i = step->wave; ++set;
      }
      if (n->values & MGS_FREQ) {
        (*set).f = step->freq; ++set;
      }
      if (n->values & MGS_DYNFREQ) {
        (*set).f = step->dynfreq; ++set;
      }
      if (n->values & MGS_PHASE) {
        (*set).i = MGS_Osc_PHASE(step->phase); ++set;
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
      data = (void*)(((uint8_t*)data) +
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
        uint32_t id = ((MGS_ProgramNode*)n->amodchain)->id + prg->topc;
        n->amodchain = o->nodes[id].node;
      }
      if (n->fmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->nodes[id].node;
      }
      if (n->pmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->nodes[id].node;
      }
      if (n->link) {
        uint32_t id = ((MGS_ProgramNode*)n->link)->id + prg->topc;
        n->link = o->nodes[id].node;
      }
    }
  }
  return o;
}

/*
 * Click reduction: decrease time to make it end at wave cycle's end.
 */
static void adjust_time(MGS_Generator *o, SoundNode *n) {
  int pos_offs = MGS_Osc_cycle_offs(&n->osc, n->freq, n->time);
  n->time -= pos_offs;
  if ((uint32_t)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
    o->delay_offs = pos_offs;
}

static void MGS_Generator_enter_node(MGS_Generator *o, IndexNode *in) {
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
    uint8_t adjtime = 0;
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
    if (setn->values & MGS_WAVE) {
      uint8_t wave = (*data).i; ++data;
      refn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (setn->values & MGS_FREQ) {
      refn->freq = (*data).f; ++data;
      adjtime = 1;
    }
    if (setn->values & MGS_DYNFREQ) {
      refn->dynfreq = (*data).f; ++data;
    }
    if (setn->values & MGS_PHASE) {
      refn->osc.phase = (uint32_t)(*data).i; ++data;
    }
    if (setn->values & MGS_AMP) {
      refn->amp = (*data).f; ++data;
    }
    if (setn->values & MGS_DYNAMP) {
      refn->dynampdiff = (*data).f; ++data;
    }
    if (setn->values & MGS_ATTR) {
      refn->attr = (uint8_t)(*data).i; ++data;
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

void MGS_destroy_Generator(MGS_Generator *o) {
  free(o);
}

/*
 * node sample processing
 */

static float run_waveenv_sample(SoundNode *n, float freq_mult);

static uint32_t run_sample(SoundNode *n, float freq_mult) {
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
      am *= run_waveenv_sample(n->amodchain, freq);
      amp += am;
    }
    if (n->fmodchain) {
      float fm = n->dynfreq;
      if (n->attr & MGS_ATTR_DYNFREQRATIO)
        fm *= freq_mult;
      fm -= freq;
      fm *= run_waveenv_sample(n->fmodchain, freq);
      freq += fm;
    }
    if (n->pmodchain)
      pm += run_sample(n->pmodchain, freq);
    s = lrintf(MGS_Osc_run(&n->osc, freq, pm << 16) * amp * INT16_MAX);
    ret += s;
  } while ((n = n->link));
  return ret;
}

static float run_waveenv_sample(SoundNode *n, float freq_mult) {
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
      fm *= run_waveenv_sample(n->fmodchain, freq);
      freq += fm;
    }
    if (n->pmodchain)
      pm += run_sample(n->pmodchain, freq);
    s = MGS_Osc_run_envo(&n->osc, freq, pm << 16);
    ret *= s;
  } while ((n = n->link));
  return ret;
}

/*
 * node block processing
 */

static uint32_t run_node(SoundNode *n, int16_t *sp, uint32_t pos, uint32_t len) {
  uint32_t ret, time = n->time - pos;
  if (time > len)
    time = len;
  ret = time;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  for (; time; --time, sp += 2) {
    int s;
    s = run_sample(n, /* dummy value */0.f);
    sp[0] += s;
    if (n->mode == MGS_MODE_CENTER)
      sp[1] += s;
  }
  return ret;
}

/*
 * main run-function
 */

bool MGS_Generator_run(MGS_Generator *o, int16_t *buf,
    uint32_t len, uint32_t *gen_len) {
  int16_t *sp;
  uint32_t i, skiplen, totlen;
  totlen = len;
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
      uint32_t delay = -in->pos;
      if ((uint32_t)o->delay_offs != NO_DELAY_OFFS)
        delay -= o->delay_offs; /* delay change == previous time change */
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
      MGS_Generator_enter_node(o, in);
  }
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint32_t delay = -in->pos;
      if ((uint32_t)o->delay_offs != NO_DELAY_OFFS) {
        in->pos += o->delay_offs; /* delay change == previous time change */
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
      MGS_Generator_enter_node(o, in);
    if (in->flag & MGS_FLAG_EXEC) {
      SoundNode *n = in->node;
      in->pos += run_node(n, buf, in->pos, len);
      if ((uint32_t)in->pos == n->time)
        in->flag &= ~MGS_FLAG_EXEC;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  if (gen_len != NULL) *gen_len = totlen;
  for(;;) {
    if (o->node == o->nodec)
      return false;
    if (o->nodes[o->node].flag & MGS_FLAG_EXEC)
      break;
    ++o->node;
  }
  return true;
}
