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

/* holds allocations for nodes of differing types */
typedef struct IndexNode {
  void *node;
  uint8_t type;
} IndexNode;

typedef struct RunNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t flag;
  uint32_t first_i;
  uint32_t root_i;
} RunNode;

typedef struct SoundNode {
  uint32_t time;
  float amp, dynamp;
  float pan;
  IndexNode *amodchain;
  IndexNode *link;
} SoundNode;

typedef struct OpNode {
  SoundNode sound;
  uint8_t attr;
  MGS_Osc osc;
  float freq, dynfreq;
  IndexNode *fmodchain;
  IndexNode *pmodchain;
} OpNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct UpdateNode {
  uint32_t ref_runn_i;
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
typedef union Buf {
  float f[BUF_LEN];
  int32_t i[BUF_LEN];
} Buf;

#define MGS_GEN_TIME_OFFS (1<<0)
struct MGS_Generator {
  const MGS_Program *prg;
  uint32_t srate;
  Buf *bufs;
  uint32_t bufc;
  int delay_offs;
  int time_flags;
  IndexNode *sound_table;
  uint32_t runn_i, runn_end;
  RunNode *run_nodes;
  UpdateNode *update_nodes;
  size_t sndn_count, updn_count;
  Data *node_data;
};

static int calc_bufs_op_waveenv(OpNode *n);

static int calc_bufs_op(OpNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_op_waveenv(n->fmodchain->node);
  ++count, --i;
  if (n->sound.amodchain) {
    j = calc_bufs_op_waveenv(n->sound.amodchain->node); if (i < j) i = j;
  }
  if (n->pmodchain) {j = calc_bufs_op(n->pmodchain->node); if (i < j) i = j;}
  if (!n->sound.link) return (i > 0 ? count + i : count);
  n = n->sound.link->node;
  ++count, --i; /* need separate accumulating buf */
  goto BEGIN;
}

static int calc_bufs_op_waveenv(OpNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_op_waveenv(n->fmodchain->node);
  if (n->pmodchain) {j = calc_bufs_op(n->pmodchain->node); if (i < j) i = j;}
  if (!n->sound.link) return (i > 0 ? count + i : count);
  n = n->sound.link->node;
  ++count, --i; /* need separate multiplying buf */
  goto BEGIN;
}

static void upsize_bufs(MGS_Generator *o, IndexNode *in) {
  uint32_t count;
  switch (in->type) {
  case MGS_TYPE_OP:
    // only works if all linked nodes have type OP
    count = calc_bufs_op(in->node);
    break;
  default:
    count = 0;
  }
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

static void init_for_opdata(MGS_Generator *o,
    const MGS_ProgramNode *step, RunNode *rn, Data **node_data) {
  const MGS_ProgramOpData *op_data = step->data;
  uint32_t sndn_id = step->base_id;
  uint32_t srate = o->srate;
  if (!step->ref_prev) {
    IndexNode *in = &o->sound_table[sndn_id];
    OpNode *opn = calloc(1, sizeof(OpNode));
    in->node = opn;
    in->type = step->type;
    rn->node = in;
    uint32_t time = op_data->sound.time.v * srate;
    if (op_data->sound.amod.chain != NULL)
      opn->sound.amodchain = &o->sound_table[op_data->sound.amod.chain->base_id];
    if (op_data->fmod.chain != NULL)
      opn->fmodchain = &o->sound_table[op_data->fmod.chain->base_id];
    if (op_data->pmod.chain != NULL)
      opn->pmodchain = &o->sound_table[op_data->pmod.chain->base_id];
    opn->sound.time = time;
    opn->attr = op_data->attr;
    opn->freq = op_data->freq;
    opn->dynfreq = op_data->dynfreq;
    MGS_init_Osc(&opn->osc, srate);
    opn->osc.lut = MGS_Osc_LUT(op_data->wave);
    opn->osc.phase = MGS_Osc_PHASE(op_data->phase);
    opn->sound.amp = op_data->sound.amp;
    opn->sound.dynamp = op_data->sound.dynamp;
    opn->sound.pan = op_data->sound.pan;
    if (step->nested_next != NULL)
      opn->sound.link = &o->sound_table[step->nested_next->base_id];
  } else {
    UpdateNode *updn = &o->update_nodes[o->updn_count++];
    Data *set = *node_data;
    const MGS_ProgramNode *ref = step->ref_prev;
    rn->node = updn;
    rn->flag |= MGS_FLAG_UPDATE;
    updn->ref_runn_i = ref->id;
    updn->params = op_data->sound.params;
    updn->data = set;
    if (updn->params & MGS_AMODS) {
      (*set++).i = op_data->sound.amod.chain->base_id;
    }
    if (updn->params & MGS_FMODS) {
      (*set++).i = op_data->fmod.chain->base_id;
    }
    if (updn->params & MGS_PMODS) {
      (*set++).i = op_data->pmod.chain->base_id;
    }
    if (updn->params & MGS_TIME) {
      (*set++).i = op_data->sound.time.v * srate;
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
      (*set++).f = op_data->sound.amp;
    }
    if (updn->params & MGS_DYNAMP) {
      (*set++).f = op_data->sound.dynamp;
    }
    if (updn->params & MGS_PAN) {
      (*set++).f = op_data->sound.pan;
    }
    if (updn->params & MGS_ATTR) {
      (*set++).i = op_data->attr;
    }
    *node_data += count_flags(op_data->sound.params);
  }
}

static void init_for_nodelist(MGS_Generator *o) {
  const MGS_Program *prg = o->prg;
  const MGS_ProgramNode *step = prg->node_list;
  Data *node_data = o->node_data;
  uint32_t srate = o->srate;
  for (size_t i = 0; i < prg->node_count; ++i) {
    RunNode *rn = &o->run_nodes[i];
    uint32_t delay = step->delay * srate;
    rn->pos = -delay;
    if (step->first_id == step->root_id)
      rn->flag |= MGS_FLAG_EXEC;
    rn->first_i = step->first_id;
    rn->root_i = step->root_id;
    switch (step->type) {
    case MGS_TYPE_OP:
      init_for_opdata(o, step, rn, &node_data);
      break;
    }
    step = step->next;
  }
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  size_t sndn_count = prg->base_counts[MGS_BASETYPE_SOUND];
  size_t updn_count = 0; // for allocation, not assigned to field
  size_t data_count = 0;
  const MGS_ProgramNode *step;
  for (step = prg->node_list; step; step = step->next) {
    if (!step->ref_prev) continue;
    MGS_ProgramOpData *op_data = MGS_ProgramNode_get_data(step, MGS_TYPE_OP);
    if (!op_data) continue;
    ++updn_count;
    data_count += count_flags(op_data->sound.params);
  }
  o = calloc(1, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->sound_table = calloc(sndn_count, sizeof(IndexNode));
  o->runn_end = prg->node_count;
  o->run_nodes = calloc(prg->node_count, sizeof(RunNode));
  o->update_nodes = calloc(updn_count, sizeof(UpdateNode));
  o->node_data = calloc(data_count, sizeof(Data));
  o->sndn_count = sndn_count;
  init_for_nodelist(o);
  MGS_global_init_Wave();
  return o;
}

/*
 * Time adjustment for operators.
 *
 * Click reduction: decrease time to make it end at wave cycle's end.
 */
static void adjust_op_time(MGS_Generator *o, OpNode *n) {
  int pos_offs = MGS_Osc_cycle_offs(&n->osc, n->freq, n->sound.time);
  n->sound.time -= pos_offs;
  if (!(o->time_flags & MGS_GEN_TIME_OFFS) || o->delay_offs > pos_offs) {
    o->delay_offs = pos_offs;
    o->time_flags |= MGS_GEN_TIME_OFFS;
  }
}

static void MGS_Generator_enter_node(MGS_Generator *o, RunNode *rn) {
  if (!(rn->flag & MGS_FLAG_UPDATE)) {
    IndexNode *in = rn->node;
    if (!in) {
      /* no-op node */
      rn->flag = MGS_FLAG_ENTERED;
      return;
    }
    if (rn->first_i == rn->root_i)
      upsize_bufs(o, in);
    switch (in->type) {
    case MGS_TYPE_OP:
      if (rn->first_i == rn->root_i)
        adjust_op_time(o, in->node);
      break;
    case MGS_TYPE_ENV:
      break;
    }
    rn->flag |= MGS_FLAG_ENTERED;
    return;
  }
  UpdateNode *updn = rn->node;
  RunNode *refrn = &o->run_nodes[updn->ref_runn_i];
  IndexNode *refin = refrn->node;
  switch (refin->type) {
  case MGS_TYPE_OP: {
    OpNode *refn = refin->node;
    RunNode *rootrn = &o->run_nodes[refrn->root_i];
    IndexNode *rootin = rootrn->node;
    Data *get = updn->data;
    bool adjtime = false;
    /* set state */
    if (updn->params & MGS_AMODS) {
      refn->sound.amodchain = &o->sound_table[(*get++).i];
    }
    if (updn->params & MGS_FMODS) {
      refn->fmodchain = &o->sound_table[(*get++).i];
    }
    if (updn->params & MGS_PMODS) {
      refn->pmodchain = &o->sound_table[(*get++).i];
    }
    if (updn->params & MGS_TIME) {
      refn->sound.time = (*get++).i;
      refrn->pos = 0;
      if (refn->sound.time) {
        if (refin == rootin)
          refrn->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        refrn->flag &= ~MGS_FLAG_EXEC;
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
      refn->sound.amp = (*get++).f;
    }
    if (updn->params & MGS_DYNAMP) {
      refn->sound.dynamp = (*get++).f;
    }
    if (updn->params & MGS_PAN) {
      refn->sound.pan = (*get++).f;
    }
    if (updn->params & MGS_ATTR) {
      refn->attr = (uint8_t)(*get++).i;
    }
    if (refin == rootin) {
      if (adjtime) /* here so new freq also used if set */
        adjust_op_time(o, refn);
    }
    upsize_bufs(o, rootin);
    /* take over place of ref'd node */
    *rn = *refrn;
    refrn->flag &= ~MGS_FLAG_EXEC;
    break; }
  case MGS_TYPE_ENV:
    break;
  }
  rn->flag |= MGS_FLAG_ENTERED;
}

void MGS_destroy_Generator(MGS_Generator *o) {
  if (!o)
    return;
  free(o->bufs);
  for (size_t i = 0; i < o->sndn_count; ++i)
    free(o->sound_table[i].node);
  free(o->sound_table);
  free(o->run_nodes);
  free(o->update_nodes);
  free(o->node_data);
  free(o);
}

/*
 * node block processing
 */

static void run_block_op_waveenv(Buf *bufs, uint32_t len, OpNode *n,
    Buf *parentfreq);

static void run_block_op(Buf *bufs, uint32_t len, OpNode *n,
    Buf *parentfreq) {
  bool acc = false;
  uint32_t i;
  Buf *sbuf = bufs, *freq, *amp, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  freq = nextbuf++;
  if (n->attr & MGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq * parentfreq->f[i];
  } else {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq;
  }
  if (n->fmodchain) {
    Buf *fmbuf;
    run_block_op_waveenv(nextbuf, len, n->fmodchain->node, freq);
    fmbuf = nextbuf;
    if (n->attr & MGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq * parentfreq->f[i] - freq->f[i]) * fmbuf->f[i];
    } else {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq - freq->f[i]) * fmbuf->f[i];
    }
  }
  if (n->sound.amodchain) {
    float dynampdiff = n->sound.dynamp - n->sound.amp;
    run_block_op_waveenv(nextbuf, len, n->sound.amodchain->node, freq);
    amp = nextbuf++;
    for (i = 0; i < len; ++i)
      amp->f[i] = n->sound.amp + amp->f[i] * dynampdiff;
  } else {
    amp = nextbuf++;
    for (i = 0; i < len; ++i)
      amp->f[i] = n->sound.amp;
  }
  pm = 0;
  if (n->pmodchain) {
    run_block_op(nextbuf, len, n->pmodchain->node, freq);
    pm = nextbuf++;
  }
  MGS_Osc_run(&n->osc, sbuf->f, len,
      acc, freq->f, amp->f, (pm != NULL) ? pm->f : NULL);
  if (!n->sound.link) return;
  acc = true;
  n = n->sound.link->node;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static void run_block_op_waveenv(Buf *bufs, uint32_t len, OpNode *n,
    Buf *parentfreq) {
  bool mul = false;
  uint32_t i;
  Buf *sbuf = bufs, *freq, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  freq = nextbuf++;
  if (n->attr & MGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq * parentfreq->f[i];
  } else {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq;
  }
  if (n->fmodchain) {
    Buf *fmbuf;
    run_block_op_waveenv(nextbuf, len, n->fmodchain->node, freq);
    fmbuf = nextbuf;
    if (n->attr & MGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq * parentfreq->f[i] - freq->f[i]) * fmbuf->f[i];
    } else {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq - freq->f[i]) * fmbuf->f[i];
    }
  }
  pm = 0;
  if (n->pmodchain) {
    run_block_op(nextbuf, len, n->pmodchain->node, freq);
    pm = nextbuf++;
  }
  MGS_Osc_run_env(&n->osc, sbuf->f, len,
      mul, freq->f, (pm != NULL) ? pm->f : NULL);
  if (!n->sound.link) return;
  mul = true;
  n = n->sound.link->node;
  nextbuf = bufs+1; /* need separate multiplying buf */
  goto BEGIN;
}

static uint32_t run_op(MGS_Generator *o, OpNode *n, short *sp, uint32_t pos,
    uint32_t len) {
  uint32_t i, ret, time = n->sound.time - pos;
  if (time > len)
    time = len;
  ret = time;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block_op(o->bufs, len, n, 0);
    float pan = (1.f + n->sound.pan) * .5f;
    for (i = 0; i < len; ++i, sp += 2) {
      float s = (*o->bufs).f[i];
      float s_p = s * pan;
      float s_l = s - s_p;
      float s_r = s_p;
      sp[0] += lrintf(s_l * INT16_MAX);
      sp[1] += lrintf(s_r * INT16_MAX);
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
    RunNode *rn = &o->run_nodes[i];
    if (rn->pos < 0) {
      uint32_t delay = -rn->pos;
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
    if (!(rn->flag & MGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGS_Generator_enter_node(o, rn);
  }
  for (i = o->runn_i; i < o->runn_end; ++i) {
    RunNode *rn = &o->run_nodes[i];
    if (rn->pos < 0) {
      uint32_t delay = -rn->pos;
      if (o->time_flags & MGS_GEN_TIME_OFFS) {
        rn->pos += o->delay_offs; /* delay change == previous time change */
        o->delay_offs = 0;
        o->time_flags &= ~MGS_GEN_TIME_OFFS;
      }
      if (delay >= len) {
        rn->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      rn->pos = 0;
    } else
    if (!(rn->flag & MGS_FLAG_ENTERED))
      MGS_Generator_enter_node(o, rn);
    if (rn->flag & MGS_FLAG_EXEC) {
      IndexNode *in = rn->node;
      if (in->type != MGS_TYPE_OP) {
        rn->flag &= ~MGS_FLAG_EXEC;
        continue; /* no more yet implemented */
      }
      OpNode *n = in->node;
      // only works if all linked nodes have type OP
      rn->pos += run_op(o, n, buf, rn->pos, len);
      if ((uint32_t)rn->pos == n->sound.time)
        rn->flag &= ~MGS_FLAG_EXEC;
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
