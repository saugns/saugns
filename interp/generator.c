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

enum {
  MGS_RUN_SOUND = 0,
  MGS_RUN_UPDATE
};

/*
 * Data for mapping program nodes to generator data.
 */
typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint32_t sndn_id;
  uint32_t parent_i;
  uint8_t type;
} IndexNode;

/*
 * Data for iterating updates and sound nodes.
 */
typedef struct RunNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t type, flag;
} RunNode;

typedef struct SoundNode {
  uint32_t time;
  uint32_t indn_id;
  uint8_t type, attr, mode;
  float freq, dynfreq;
  struct SoundNode *fmodchain;
  struct SoundNode *pmodchain;
  MGS_Osc osc;
  float amp, dynamp;
  struct SoundNode *amodchain;
  struct SoundNode *link;
} SoundNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct UpdateNode {
  uint32_t values;
  uint32_t sndn_id;
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
  float osc_coeff;
  int delay_offs;
  int time_flags;
  uint32_t runn_i, runn_end;
  RunNode *run_nodes;
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
  uint32_t srate = o->srate;
  for (size_t i = from; i < to; ++i) {
    IndexNode *indn = &o->index_nodes[i];
    RunNode *runn = &o->run_nodes[i];
    uint32_t delay = step->delay * srate;
    uint32_t sndn_id = o->sndn_count;
    if (!step->ref_prev) {
      SoundNode *sndn = &o->sound_nodes[sndn_id];
      uint32_t time = step->time * srate;
      indn->node = sndn;
      indn->pos = -delay;
      indn->sndn_id = sndn_id;
      indn->type = step->type;
      if (step->type != MGS_TYPE_NESTED)
        runn->flag |= MGS_FLAG_EXEC;
      sndn->time = time;
      sndn->indn_id = i;
      sndn->mode = step->mode; // not included in values
      sndn->osc.coeff = o->osc_coeff;
      /* mods init part one - replaced with proper entries next loop */
      sndn->amodchain = (void*)step->amod.chain;
      sndn->fmodchain = (void*)step->fmod.chain;
      sndn->pmodchain = (void*)step->pmod.chain;
      sndn->link = (void*)step->nested_next;
      ++o->sndn_count;
    } else {
      MGS_ProgramNode *ref = step->ref_prev;
      sndn_id = o->index_nodes[ref->id].sndn_id;
    }
    UpdateNode *updn = &o->update_nodes[o->updn_count];
    Data *set = *node_data;
    updn->values = step->values;
    updn->sndn_id = sndn_id;
    updn->data = set;
    if (updn->values & MGS_TIME) {
      (*set++).i = step->time * srate;
    }
    if (updn->values & MGS_WAVE) {
      (*set++).i = step->wave;
    }
    if (updn->values & MGS_FREQ) {
      (*set++).f = step->freq;
    }
    if (updn->values & MGS_DYNFREQ) {
      (*set++).f = step->dynfreq;
    }
    if (updn->values & MGS_PHASE) {
      (*set++).i = MGS_Osc_PHASE(step->phase);
    }
    if (updn->values & MGS_AMP) {
      (*set++).f = step->amp;
    }
    if (updn->values & MGS_DYNAMP) {
      (*set++).f = step->dynamp;
    }
    if (updn->values & MGS_ATTR) {
      (*set++).i = step->attr;
    }
    if (updn->values & MGS_AMODS) {
      (*set++).i = step->amod.chain->id;
    }
    if (updn->values & MGS_FMODS) {
      (*set++).i = step->fmod.chain->id;
    }
    if (updn->values & MGS_PMODS) {
      (*set++).i = step->pmod.chain->id;
    }
    ++o->updn_count;
    *node_data += count_flags(step->values);
    runn->node = updn;
    runn->pos = -delay;
    runn->type = MGS_RUN_UPDATE;
    step = step->next;
  }
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  MGS_ProgramNode *step;
  Data *node_data;
  size_t runn_count = prg->nodec;
  size_t sndn_count = 0; // for allocation, not assigned to field
  size_t updn_count = prg->nodec; // for allocation, not assigned to field
  size_t data_count = 0;
  for (step = prg->node_list; step; step = step->next) {
    if (!step->ref_prev)
      ++sndn_count;
    data_count += count_flags(step->values);
  }
  o = calloc(1, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->osc_coeff = MGS_Osc_COEFF(srate);
  o->runn_end = runn_count;
  o->run_nodes = calloc(runn_count, sizeof(RunNode));
  o->indn_end = prg->nodec;
  o->index_nodes = calloc(prg->nodec, sizeof(IndexNode));
  o->sound_nodes = calloc(sndn_count, sizeof(SoundNode));
  o->update_nodes = calloc(updn_count, sizeof(UpdateNode));
  o->node_data = calloc(data_count, sizeof(Data));
  MGS_global_init_Wave();
  node_data = o->node_data;
  init_for_nodelist(o, prg->node_list, 0, prg->nodec, &node_data);
  /* mods init part two - give proper entries */
  for (size_t i = 0; i < sndn_count; ++i) {
    SoundNode *sndn = &o->sound_nodes[i];
    uint32_t indn_id = sndn->indn_id;
    if (sndn->amodchain) {
      uint32_t id = ((MGS_ProgramNode*)sndn->amodchain)->id;
      sndn->amodchain = o->index_nodes[id].node;
      o->index_nodes[id].parent_i = indn_id;
    }
    if (sndn->fmodchain) {
      uint32_t id = ((MGS_ProgramNode*)sndn->fmodchain)->id;
      sndn->fmodchain = o->index_nodes[id].node;
      o->index_nodes[id].parent_i = indn_id;
    }
    if (sndn->pmodchain) {
      uint32_t id = ((MGS_ProgramNode*)sndn->pmodchain)->id;
      sndn->pmodchain = o->index_nodes[id].node;
      o->index_nodes[id].parent_i = indn_id;
    }
    if (sndn->link) {
      uint32_t id = ((MGS_ProgramNode*)sndn->link)->id;
      sndn->link = o->index_nodes[id].node;
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

static void MGS_Generator_enter_node(MGS_Generator *o, RunNode *runn) {
  if (runn->type == MGS_RUN_SOUND) {
    SoundNode *sndn = runn->node;
    switch (sndn->type) {
    case MGS_TYPE_TOP:
      upsize_bufs(o, sndn);
      adjust_time(o, sndn);
    case MGS_TYPE_NESTED:
      break;
    }
    runn->flag |= MGS_FLAG_ENTERED;
    return;
  }
  UpdateNode *updn = runn->node;
  SoundNode *sndn = &o->sound_nodes[updn->sndn_id];
  switch (sndn->type) {
  case MGS_TYPE_TOP:
  case MGS_TYPE_NESTED: {
    Data *get = updn->data;
    bool adjtime = false;
    /* set state */
    if (updn->values & MGS_TIME) {
      sndn->time = (*get++).i;
      runn->pos = 0;
      if (sndn->time) {
        if (sndn->type == MGS_TYPE_TOP)
          runn->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        runn->flag &= ~MGS_FLAG_EXEC;
    }
    if (updn->values & MGS_WAVE) {
      uint8_t wave = (*get++).i;
      sndn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (updn->values & MGS_FREQ) {
      sndn->freq = (*get++).f;
      adjtime = true;
    }
    if (updn->values & MGS_DYNFREQ) {
      sndn->dynfreq = (*get++).f;
    }
    if (updn->values & MGS_PHASE) {
      sndn->osc.phase = (uint32_t)(*get++).i;
    }
    if (updn->values & MGS_AMP) {
      sndn->amp = (*get++).f;
    }
    if (updn->values & MGS_DYNAMP) {
      sndn->dynamp = (*get++).f;
    }
    if (updn->values & MGS_ATTR) {
      sndn->attr = (uint8_t)(*get++).i;
    }
    if (updn->values & MGS_AMODS) {
      sndn->amodchain = o->index_nodes[(*get++).i].node;
    }
    if (updn->values & MGS_FMODS) {
      sndn->fmodchain = o->index_nodes[(*get++).i].node;
    }
    if (updn->values & MGS_PMODS) {
      sndn->pmodchain = o->index_nodes[(*get++).i].node;
    }
    if (sndn->type == MGS_TYPE_TOP) {
      upsize_bufs(o, sndn);
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, sndn);
    } else {
      IndexNode *indn = &o->index_nodes[sndn->indn_id];
      IndexNode *top_indn = indn;
      while (top_indn->type == MGS_TYPE_NESTED)
        top_indn = &o->index_nodes[top_indn->parent_i];
      upsize_bufs(o, &o->sound_nodes[top_indn->sndn_id]);
    }
    /* switch to the sound node which is to run */
    runn->node = sndn;
    runn->type = MGS_RUN_SOUND;
    break; }
  case MGS_TYPE_ENV:
    //runn->flag &= ~MGS_FLAG_EXEC;
    break;
  }
// TODO: ensure update node -> sound node
  runn->flag |= MGS_FLAG_ENTERED;
}

void MGS_destroy_Generator(MGS_Generator *o) {
  if (!o)
    return;
  free(o->bufs);
  free(o->run_nodes);
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
    float dynampdiff = n->dynamp - n->amp;
    run_block_waveenv(nextbuf, len, n->amodchain, freq);
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp + amp[i].f * dynampdiff;
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
  for (i = o->runn_i; i < o->runn_end; ++i) {
    RunNode *runn = &o->run_nodes[i];
    if (runn->pos < 0) {
      uint32_t delay = -runn->pos;
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
    if (!(runn->flag & MGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGS_Generator_enter_node(o, runn);
  }
  for (i = o->runn_i; i < o->runn_end; ++i) {
    RunNode *runn = &o->run_nodes[i];
    if (runn->pos < 0) {
      uint32_t delay = -runn->pos;
      if (o->time_flags & MGS_GEN_TIME_OFFS) {
        runn->pos += o->delay_offs; /* delay change == previous time change */
        o->delay_offs = 0;
        o->time_flags &= ~MGS_GEN_TIME_OFFS;
      }
      if (delay >= len) {
        runn->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      runn->pos = 0;
    } else
    if (!(runn->flag & MGS_FLAG_ENTERED))
      MGS_Generator_enter_node(o, runn);
    if (runn->flag & MGS_FLAG_EXEC) {
      SoundNode *n = runn->node;
      runn->pos += run_node(o, n, buf, runn->pos, len);
      if ((uint32_t)runn->pos == n->time)
        runn->flag &= ~MGS_FLAG_EXEC;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  if (gen_len != NULL) *gen_len = totlen;
  for(;;) {
    if (o->runn_i == o->runn_end)
      return false;
    if (o->run_nodes[o->runn_i].flag & MGS_FLAG_EXEC)
      break;
    ++o->runn_i;
  }
  return true;
}
