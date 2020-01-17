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
  int ref; /* for set nodes, id of node set; for nested, id of parent */
} IndexNode;

typedef struct SoundNode {
  uint32_t time;
  uint8_t type, attr, mode;
  float freq, dynfreq;
  struct SoundNode *fmodchain;
  struct SoundNode *pmodchain;
  MGS_Osc osc;
  float amp, dynampdiff;
  struct SoundNode *amodchain;
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

#define BUF_LEN 256
typedef Data Buf[BUF_LEN];

#define MGS_GEN_TIME_OFFS (1<<0)
struct MGS_Generator {
  uint32_t srate;
  Buf *bufs;
  uint32_t bufc;
  int delay_offs;
  int time_flags;
  uint32_t node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
};

static int calc_bufs_waveenv(SoundNode *n);

static int calc_bufs(SoundNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_waveenv(n->fmodchain);
  ++count, --i;
  if (n->amodchain) {j = calc_bufs_waveenv(n->amodchain); if (i < j) i = j;}
  if (n->pmodchain) {j = calc_bufs(n->pmodchain); if (i < j) i = j;}
  if (!n->link) return (i > 0 ? count + i : count);
  n = n->link;
  ++count, --i; /* need separate accumulating buf */
  goto BEGIN;
}

static int calc_bufs_waveenv(SoundNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_waveenv(n->fmodchain);
  if (n->pmodchain) {j = calc_bufs(n->pmodchain); if (i < j) i = j;}
  if (!n->link) return (i > 0 ? count + i : count);
  n = n->link;
  ++count, --i; /* need separate multiplying buf */
  goto BEGIN;
}

static void upsize_bufs(MGS_Generator *o, SoundNode *n) {
  uint32_t count = calc_bufs(n);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

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
      MGS_init_Osc(&n->osc, srate);
      n->osc.lut = MGS_Osc_LUT(step->wave);
      n->osc.phase = MGS_Osc_PHASE(step->phase);
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
        o->nodes[id].ref = i;
      }
      if (n->fmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->nodes[id].node;
        o->nodes[id].ref = i;
      }
      if (n->pmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->nodes[id].node;
        o->nodes[id].ref = i;
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
  if (!(o->time_flags & MGS_GEN_TIME_OFFS) || o->delay_offs > pos_offs) {
    o->delay_offs = pos_offs;
    o->time_flags |= MGS_GEN_TIME_OFFS;
  }
}

static void MGS_Generator_enter_node(MGS_Generator *o, IndexNode *in) {
  switch (in->type) {
  case MGS_TYPE_TOP:
    upsize_bufs(o, in->node);
    adjust_time(o, in->node);
  case MGS_TYPE_NESTED:
    break;
  case MGS_TYPE_SETTOP:
  case MGS_TYPE_SETNESTED: {
    IndexNode *refin = &o->nodes[in->ref];
    SoundNode *refn = refin->node;
    SetNode *setn = in->node;
    Data *data = setn->data;
    bool adjtime = false;
    /* set state */
    if (setn->values & MGS_TIME) {
      refn->time = (*data).i; ++data;
      refin->pos = 0;
      if (refn->time) {
        if (refin->type == MGS_TYPE_TOP)
          refin->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        refin->flag &= ~MGS_FLAG_EXEC;
    }
    if (setn->values & MGS_WAVE) {
      uint8_t wave = (*data).i; ++data;
      refn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (setn->values & MGS_FREQ) {
      refn->freq = (*data).f; ++data;
      adjtime = true;
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
    if (refn->type == MGS_TYPE_TOP) {
      upsize_bufs(o, refn);
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, refn);
    } else {
      IndexNode *topin = refin;
      while (topin->ref > -1)
        topin = &o->nodes[topin->ref];
      upsize_bufs(o, topin->node);
    }
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
  if (!o)
    return;
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static void run_block_waveenv(Buf *bufs, uint32_t len, SoundNode *n,
                              Data *parentfreq);

static void run_block(Buf *bufs, uint32_t len, SoundNode *n,
                      Data *parentfreq) {
  bool acc = false;
  uint32_t i;
  Data *sbuf = *bufs, *freq, *amp, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  freq = *(nextbuf++);
  if (n->attr & MGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq);
    fmbuf = *nextbuf;
    if (n->attr & MGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  if (n->amodchain) {
    run_block_waveenv(nextbuf, len, n->amodchain, freq);
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp + amp[i].f * n->dynampdiff;
  } else {
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp;
  }
  pm = 0;
  if (n->pmodchain) {
    run_block(nextbuf, len, n->pmodchain, freq);
    pm = *(nextbuf++);
  }
  for (i = 0; i < len; ++i) {
    int s, spm = 0;
    float sfreq = freq[i].f, samp = amp[i].f;
    if (pm)
      spm = (pm[i].i) << 16;
    s = lrintf(MGS_Osc_run(&n->osc, sfreq, spm) * samp * INT16_MAX);
    if (acc)
      s += sbuf[i].i;
    sbuf[i].i = s;
  }
  if (!n->link) return;
  acc = true;
  n = n->link;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static void run_block_waveenv(Buf *bufs, uint32_t len, SoundNode *n,
                              Data *parentfreq) {
  bool mul = false;
  uint32_t i;
  Data *sbuf = *bufs, *freq, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  freq = *(nextbuf++);
  if (n->attr & MGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq);
    fmbuf = *nextbuf;
    if (n->attr & MGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  pm = 0;
  if (n->pmodchain) {
    run_block(nextbuf, len, n->pmodchain, freq);
    pm = *(nextbuf++);
  }
  for (i = 0; i < len; ++i) {
    float s, sfreq = freq[i].f;
    int spm = 0;
    if (pm)
      spm = (pm[i].i) << 16;
    s = MGS_Osc_run_envo(&n->osc, sfreq, spm);
    if (mul)
      s *= sbuf[i].f;
    sbuf[i].f = s;
  }
  if (!n->link) return;
  mul = true;
  n = n->link;
  nextbuf = bufs+1; /* need separate multiplying buf */
  goto BEGIN;
}

static uint32_t run_node(MGS_Generator *o, SoundNode *n, short *sp, uint32_t pos, uint32_t len) {
  uint32_t i, ret, time = n->time - pos;
  if (time > len)
    time = len;
  ret = time;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0);
    for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i;
      sp[0] += s;
      if (n->mode == MGS_MODE_CENTER)
        sp[1] += s;
    }
  } while (time);
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
      if (o->time_flags & MGS_GEN_TIME_OFFS)
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
      if (o->time_flags & MGS_GEN_TIME_OFFS) {
        in->pos += o->delay_offs; /* delay change == previous time change */
        o->delay_offs = 0;
        o->time_flags &= ~MGS_GEN_TIME_OFFS;
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
      in->pos += run_node(o, n, buf, in->pos, len);
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
