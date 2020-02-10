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
  MGS_FLAG_ENTERED = 1<<0,
  MGS_FLAG_UPDATE = 1<<1,
  MGS_FLAG_EXEC = 1<<2
};

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t type, flag;
  uint32_t ref_i;
  uint32_t first_i;
  uint32_t root_i;
} IndexNode;

typedef struct SoundNode {
  uint32_t time;
  uint8_t type, attr;
  float freq, dynfreq;
  struct SoundNode *fmodchain;
  struct SoundNode *pmodchain;
  MGS_Osc osc;
  float amp, dynamp;
  struct SoundNode *amodchain;
  struct SoundNode *link;
  float pan;
} SoundNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct UpdateNode {
  uint32_t params;
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
  size_t sndn_count, updn_count;
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

static void init_for_opdata(MGS_Generator *o,
    const MGS_ProgramNode *step, IndexNode *in, Data **node_data) {
  const MGS_ProgramOpData *op_data = step->data.op;
  uint32_t sndn_id = step->type_id;
  uint32_t srate = o->srate;
  if (!step->ref_prev) {
    SoundNode *sndn = &o->sound_nodes[sndn_id];
    uint32_t time = op_data->time.v * srate;
    in->node = sndn;
    if (op_data->amod.chain != NULL)
      sndn->amodchain = &o->sound_nodes[op_data->amod.chain->type_id];
    if (op_data->fmod.chain != NULL)
      sndn->fmodchain = &o->sound_nodes[op_data->fmod.chain->type_id];
    if (op_data->pmod.chain != NULL)
      sndn->pmodchain = &o->sound_nodes[op_data->pmod.chain->type_id];
    sndn->time = time;
    sndn->attr = op_data->attr;
    sndn->freq = op_data->freq;
    sndn->dynfreq = op_data->dynfreq;
    MGS_init_Osc(&sndn->osc, srate);
    sndn->osc.lut = MGS_Osc_LUT(op_data->wave);
    sndn->osc.phase = MGS_Osc_PHASE(op_data->phase);
    sndn->amp = op_data->amp;
    sndn->dynamp = op_data->dynamp;
    sndn->pan = op_data->pan;
    if (step->nested_next != NULL)
      sndn->link = &o->sound_nodes[step->nested_next->type_id];
  } else {
    UpdateNode *updn = &o->update_nodes[o->updn_count++];
    Data *set = *node_data;
    const MGS_ProgramNode *ref = step->ref_prev;
    in->node = updn;
    in->flag |= MGS_FLAG_UPDATE;
    in->ref_i = ref->id;
    updn->params = op_data->params;
    updn->data = set;
    if (updn->params & MGS_AMODS) {
      (*set++).i = op_data->amod.chain->type_id;
    }
    if (updn->params & MGS_FMODS) {
      (*set++).i = op_data->fmod.chain->type_id;
    }
    if (updn->params & MGS_PMODS) {
      (*set++).i = op_data->pmod.chain->type_id;
    }
    if (updn->params & MGS_TIME) {
      (*set++).i = op_data->time.v * srate;
    }
    if (updn->params & MGS_WAVE) {
      (*set++).i = op_data->wave;
    }
    if (updn->params & MGS_FREQ) {
      (*set++).f = op_data->freq;
    }
    if (updn->params & MGS_DYNFREQ) {
      (*set++).f = op_data->dynfreq;
    }
    if (updn->params & MGS_PHASE) {
      (*set++).i = MGS_Osc_PHASE(op_data->phase);
    }
    if (updn->params & MGS_AMP) {
      (*set++).f = op_data->amp;
    }
    if (updn->params & MGS_DYNAMP) {
      (*set++).f = op_data->dynamp;
    }
    if (updn->params & MGS_PAN) {
      (*set++).f = op_data->pan;
    }
    if (updn->params & MGS_ATTR) {
      (*set++).i = op_data->attr;
    }
    *node_data += count_flags(op_data->params);
  }
}

static void init_for_nodelist(MGS_Generator *o) {
  const MGS_Program *prg = o->prg;
  const MGS_ProgramNode *step = prg->node_list;
  Data *node_data = o->node_data;
  uint32_t srate = o->srate;
  for (size_t i = 0; i < prg->node_count; ++i) {
    IndexNode *in = &o->index_nodes[i];
    uint32_t delay = step->delay * srate;
    in->pos = -delay;
    in->type = step->type;
    if (step->first_id == step->root_id)
      in->flag |= MGS_FLAG_EXEC;
    in->first_i = step->first_id;
    in->root_i = step->root_id;
    switch (step->type) {
    case MGS_TYPE_OP:
      init_for_opdata(o, step, in, &node_data);
      break;
    }
    step = step->next;
  }
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  size_t updn_count = 0; // for allocation, not assigned to field
  size_t data_count = 0;
  const MGS_ProgramNode *step;
  for (step = prg->node_list; step; step = step->next) {
    if (!step->ref_prev) continue;
    MGS_ProgramOpData *op_data = MGS_ProgramNode_get_data(step, MGS_TYPE_OP);
    if (!op_data) continue;
    ++updn_count;
    data_count += count_flags(op_data->params);
  }
  o = calloc(1, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->indn_end = prg->node_count;
  o->index_nodes = calloc(prg->node_count, sizeof(IndexNode));
  o->sndn_count = prg->type_counts[MGS_TYPE_OP];
  o->sound_nodes = calloc(o->sndn_count, sizeof(SoundNode));
  o->update_nodes = calloc(updn_count, sizeof(UpdateNode));
  o->node_data = calloc(data_count, sizeof(Data));
  init_for_nodelist(o);
  MGS_global_init_Wave();
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
  if (!(in->flag & MGS_FLAG_UPDATE)) {
    switch (in->type) {
    case MGS_TYPE_OP:
      if (in->first_i == in->root_i) {
        upsize_bufs(o, in->node);
        adjust_time(o, in->node);
      }
    case MGS_TYPE_ENV:
      break;
    }
    in->flag |= MGS_FLAG_ENTERED;
    return;
  }
  switch (in->type) {
  case MGS_TYPE_OP: {
    IndexNode *refin = &o->index_nodes[in->ref_i];
    SoundNode *refn = refin->node;
    IndexNode *rootin = &o->index_nodes[refin->root_i];
    SoundNode *rootn = rootin->node;
    UpdateNode *updn = in->node;
    Data *get = updn->data;
    bool adjtime = false;
    /* set state */
    if (updn->params & MGS_AMODS) {
      refn->amodchain = &o->sound_nodes[(*get++).i];
    }
    if (updn->params & MGS_FMODS) {
      refn->fmodchain = &o->sound_nodes[(*get++).i];
    }
    if (updn->params & MGS_PMODS) {
      refn->pmodchain = &o->sound_nodes[(*get++).i];
    }
    if (updn->params & MGS_TIME) {
      refn->time = (*get++).i;
      refin->pos = 0;
      if (refn->time) {
        if (refn == rootn)
          refin->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        refin->flag &= ~MGS_FLAG_EXEC;
    }
    if (updn->params & MGS_WAVE) {
      uint8_t wave = (*get++).i;
      refn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (updn->params & MGS_FREQ) {
      refn->freq = (*get++).f;
      adjtime = true;
    }
    if (updn->params & MGS_DYNFREQ) {
      refn->dynfreq = (*get++).f;
    }
    if (updn->params & MGS_PHASE) {
      refn->osc.phase = (uint32_t)(*get++).i;
    }
    if (updn->params & MGS_AMP) {
      refn->amp = (*get++).f;
    }
    if (updn->params & MGS_DYNAMP) {
      refn->dynamp = (*get++).f;
    }
    if (updn->params & MGS_PAN) {
      refn->pan = (*get++).f;
    }
    if (updn->params & MGS_ATTR) {
      refn->attr = (uint8_t)(*get++).i;
    }
    if (refn == rootn) {
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, refn);
    }
    upsize_bufs(o, rootn);
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
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0);
    float pan = (1.f + n->pan) * .5f;
    for (i = 0; i < len; ++i, sp += 2) {
      float s = (*o->bufs)[i].i;
      float s_p = s * pan;
      float s_l = s - s_p;
      float s_r = s_p;
      sp[0] += lrintf(s_l);
      sp[1] += lrintf(s_r);
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
