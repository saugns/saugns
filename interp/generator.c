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

enum {
  MGS_FLAG_EXEC = 1<<0,
  MGS_FLAG_ENTERED = 1<<1
};

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t type, flag;
  int set_ref;
  int parent_ref;
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

typedef struct UpdateNode {
  uint32_t values;
  Data *data;
} UpdateNode;

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
  const MGS_Program *prg;
  uint32_t srate;
  Buf *bufs;
  uint32_t bufc;
  int delay_offs;
  int time_flags;
  uint32_t indn_i, indn_end;
  IndexNode *index_nodes;
  SoundNode *sound_nodes;
  UpdateNode *update_nodes;
  uint32_t sndn_count, updn_count;
  Data *node_data;
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

static void init_for_nodelist(MGS_Generator *o, MGS_ProgramNode *step,
    size_t from, size_t to, Data **node_data) {
  const MGS_Program *prg = o->prg;
  uint32_t srate = o->srate;
  for (size_t i = from; i < to; ++i) {
    IndexNode *in = &o->index_nodes[i];
    uint32_t delay = step->delay * srate;
    in->pos = -delay;
    in->type = step->type;
    in->set_ref = -1;
    in->parent_ref = -1;
    if (step->type != MGS_TYPE_NESTED)
      in->flag |= MGS_FLAG_EXEC;
    if (!step->ref_prev) {
      SoundNode *sndn = &o->sound_nodes[o->sndn_count++];
      uint32_t time = step->time * srate;
      in->node = sndn;
      sndn->time = time;
      sndn->attr = step->attr;
      sndn->mode = step->mode;
      sndn->amp = step->amp;
      sndn->dynampdiff = step->dynamp - step->amp;
      sndn->freq = step->freq;
      sndn->dynfreq = step->dynfreq;
      MGS_init_Osc(&sndn->osc, srate);
      sndn->osc.lut = MGS_Osc_LUT(step->wave);
      sndn->osc.phase = MGS_Osc_PHASE(step->phase);
      /* mods init part one - replaced with proper entries next loop */
      sndn->amodchain = (void*)step->amod.chain;
      sndn->fmodchain = (void*)step->fmod.chain;
      sndn->pmodchain = (void*)step->pmod.chain;
      sndn->link = (void*)step->nested_next;
    } else {
      UpdateNode *updn = &o->update_nodes[o->updn_count++];
      Data *set = *node_data;
      MGS_ProgramNode *ref = step->ref_prev;
      in->node = updn;
      in->set_ref = ref->id;
      if (ref->type == MGS_TYPE_NESTED)
        in->set_ref += prg->topc;
      updn->values = step->values;
      updn->values &= ~MGS_DYNAMP;
      updn->data = set;
      if (updn->values & MGS_TIME) {
        (*set).i = step->time * srate; ++set;
      }
      if (updn->values & MGS_WAVE) {
        (*set).i = step->wave; ++set;
      }
      if (updn->values & MGS_FREQ) {
        (*set).f = step->freq; ++set;
      }
      if (updn->values & MGS_DYNFREQ) {
        (*set).f = step->dynfreq; ++set;
      }
      if (updn->values & MGS_PHASE) {
        (*set).i = MGS_Osc_PHASE(step->phase); ++set;
      }
      if (updn->values & MGS_AMP) {
        (*set).f = step->amp; ++set;
      }
      if ((step->dynamp - step->amp) != (ref->dynamp - ref->amp)) {
        (*set).f = (step->dynamp - step->amp); ++set;
        updn->values |= MGS_DYNAMP;
      }
      if (updn->values & MGS_ATTR) {
        (*set).i = step->attr; ++set;
      }
      if (updn->values & MGS_AMODS) {
        (*set).i = step->amod.chain->id + prg->topc;
        ++set;
      }
      if (updn->values & MGS_FMODS) {
        (*set).i = step->fmod.chain->id + prg->topc;
        ++set;
      }
      if (updn->values & MGS_PMODS) {
        (*set).i = step->pmod.chain->id + prg->topc;
        ++set;
      }
      *node_data += count_flags(step->values);
    }
    step = step->next;
  }
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  MGS_ProgramNode *step;
  Data *node_data;
  size_t sndn_count = 0; // for allocation, not assigned to field
  size_t updn_count = 0; // for allocation, not assigned to field
  size_t data_count = 0;
  for (step = prg->top_list; step; step = step->next) {
    if (!step->ref_prev)
      ++sndn_count;
    else {
      ++updn_count;
      data_count += count_flags(step->values);
    }
  }
  for (step = prg->nested_list; step; step = step->next) {
    if (!step->ref_prev)
      ++sndn_count;
    else {
      ++updn_count;
      data_count += count_flags(step->values);
    }
  }
  o = calloc(1, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->indn_end = prg->topc; /* only loop top-level nodes */
  o->index_nodes = calloc(prg->nodec, sizeof(IndexNode));
  o->sound_nodes = calloc(sndn_count, sizeof(SoundNode));
  o->update_nodes = calloc(updn_count, sizeof(UpdateNode));
  o->node_data = calloc(data_count, sizeof(Data));
  MGS_global_init_Wave();
  node_data = o->node_data;
  init_for_nodelist(o, prg->top_list, 0, prg->topc, &node_data);
  init_for_nodelist(o, prg->nested_list, prg->topc, prg->nodec, &node_data);
  /* mods init part two - give proper entries */
  for (size_t i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->index_nodes[i];
    if (in->set_ref < 0) {
      SoundNode *n = in->node;
      if (n->amodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->amodchain)->id + prg->topc;
        n->amodchain = o->index_nodes[id].node;
        o->index_nodes[id].parent_ref = i;
      }
      if (n->fmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->index_nodes[id].node;
        o->index_nodes[id].parent_ref = i;
      }
      if (n->pmodchain) {
        uint32_t id = ((MGS_ProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->index_nodes[id].node;
        o->index_nodes[id].parent_ref = i;
      }
      if (n->link) {
        uint32_t id = ((MGS_ProgramNode*)n->link)->id + prg->topc;
        n->link = o->index_nodes[id].node;
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
  if (in->set_ref < 0) {
    switch (in->type) {
    case MGS_TYPE_TOP:
      upsize_bufs(o, in->node);
      adjust_time(o, in->node);
    case MGS_TYPE_NESTED:
      break;
    }
    in->flag |= MGS_FLAG_ENTERED;
    return;
  }
  switch (in->type) {
  case MGS_TYPE_TOP:
  case MGS_TYPE_NESTED: {
    IndexNode *refin = &o->index_nodes[in->set_ref];
    SoundNode *refn = refin->node;
    UpdateNode *updn = in->node;
    Data *data = updn->data;
    bool adjtime = false;
    /* set state */
    if (updn->values & MGS_TIME) {
      refn->time = (*data).i; ++data;
      refin->pos = 0;
      if (refn->time) {
        if (refin->type == MGS_TYPE_TOP)
          refin->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        refin->flag &= ~MGS_FLAG_EXEC;
    }
    if (updn->values & MGS_WAVE) {
      uint8_t wave = (*data).i; ++data;
      refn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (updn->values & MGS_FREQ) {
      refn->freq = (*data).f; ++data;
      adjtime = true;
    }
    if (updn->values & MGS_DYNFREQ) {
      refn->dynfreq = (*data).f; ++data;
    }
    if (updn->values & MGS_PHASE) {
      refn->osc.phase = (uint32_t)(*data).i; ++data;
    }
    if (updn->values & MGS_AMP) {
      refn->amp = (*data).f; ++data;
    }
    if (updn->values & MGS_DYNAMP) {
      refn->dynampdiff = (*data).f; ++data;
    }
    if (updn->values & MGS_ATTR) {
      refn->attr = (uint8_t)(*data).i; ++data;
    }
    if (updn->values & MGS_AMODS) {
      refn->amodchain = o->index_nodes[(*data).i].node; ++data;
    }
    if (updn->values & MGS_FMODS) {
      refn->fmodchain = o->index_nodes[(*data).i].node; ++data;
    }
    if (updn->values & MGS_PMODS) {
      refn->pmodchain = o->index_nodes[(*data).i].node; ++data;
    }
    if (refn->type == MGS_TYPE_TOP) {
      upsize_bufs(o, refn);
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, refn);
    } else {
      IndexNode *topin = refin;
      while (topin->parent_ref > -1)
        topin = &o->index_nodes[topin->parent_ref];
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
  free(o->index_nodes);
  free(o->sound_nodes);
  free(o->update_nodes);
  free(o->node_data);
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
  for (i = o->indn_i; i < o->indn_end; ++i) {
    IndexNode *in = &o->index_nodes[i];
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
  for (i = o->indn_i; i < o->indn_end; ++i) {
    IndexNode *in = &o->index_nodes[i];
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
    if (o->indn_i == o->indn_end)
      return false;
    if (o->index_nodes[o->indn_i].flag & MGS_FLAG_EXEC)
      break;
    ++o->indn_i;
  }
  return true;
}
