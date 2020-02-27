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

#include "runalloc.h"
#include <stdio.h>
#include <stdlib.h>

enum {
  MGS_FLAG_ENTERED = 1<<0,
  MGS_FLAG_UPDATE = 1<<1,
  MGS_FLAG_EXEC = 1<<2
};

typedef struct RunNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uint8_t flag;
  uint32_t ref_i;
  uint32_t first_i;
  uint32_t root_i;
} RunNode;

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
  MGS_SoundNode **sound_list;
  MGS_ModList **mod_lists;
  uint32_t runn_i, runn_end;
  RunNode *run_nodes;
  size_t sndn_count;
  MGS_MemPool *mem;
  bool need_upsize;
};

static size_t calc_bufs_op(MGS_Generator *o,
    size_t count_from, MGS_OpNode *n) {
  ++count_from;
  size_t count = count_from;
  size_t max_count = count;
  ++count;
  if (n->fmods_id > 0) {
    MGS_ModList *fmod_list = o->mod_lists[n->fmods_id];
    for (size_t i = 0; i < fmod_list->count; ++i) {
      size_t sub_count = calc_bufs_op(o, count,
          (MGS_OpNode*) o->sound_list[fmod_list->ids[i]]);
      if (max_count < sub_count) max_count = sub_count;
    }
  }
  if (n->sound.amods_id > 0) {
    MGS_ModList *amod_list = o->mod_lists[n->sound.amods_id];
    for (size_t i = 0; i < amod_list->count; ++i) {
      size_t sub_count = calc_bufs_op(o, count,
          (MGS_OpNode*) o->sound_list[amod_list->ids[i]]);
      if (max_count < sub_count) max_count = sub_count;
    }
    ++count;
  } else {
    ++count;
  }
  if (n->pmods_id > 0) {
    MGS_ModList *pmod_list = o->mod_lists[n->pmods_id];
    for (size_t i = 0; i < pmod_list->count; ++i) {
      size_t sub_count = calc_bufs_op(o, count,
          (MGS_OpNode*) o->sound_list[pmod_list->ids[i]]);
      if (max_count < sub_count) max_count = sub_count;
    }
    ++count;
  }
  if (max_count < count) max_count = count;
  return max_count;
}

static void upsize_bufs(MGS_Generator *o) {
  if (!o->need_upsize)
    return;
  size_t max_count = 0;
  for (size_t i = 0; i < o->sndn_count; ++i) {
    MGS_SoundNode *sndn = o->sound_list[i];
    if (i != sndn->root_base_i) continue; /* only traverse from root */
    size_t count = 0;
    switch (sndn->type) {
    case MGS_TYPE_OP:
      // only works if all linked nodes have type OP
      count = calc_bufs_op(o, 0, (MGS_OpNode*) sndn);
      break;
    }
    if (max_count < count) max_count = count;
  }
  if (max_count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * max_count);
    o->bufc = max_count;
  }
  o->need_upsize = false;
}

static void init_for_nodelist(MGS_Generator *o) {
  MGS_RunAlloc ra;
  const MGS_Program *prg = o->prg;
  const MGS_ProgramNode *step = prg->node_list;
  uint32_t srate = o->srate;
  MGS_init_RunAlloc(&ra, o->prg, o->srate, o->mem);
  for (size_t i = 0; i < prg->node_count; ++i) {
    void *data = MGS_RunAlloc_for_node(&ra, step);
    RunNode *rn = &o->run_nodes[i];
    uint32_t delay = lrintf(step->delay * srate);
    rn->node = data;
    rn->pos = -delay;
    if (step->ref_prev != NULL) {
      const MGS_ProgramNode *ref = step->ref_prev;
      rn->flag |= MGS_FLAG_UPDATE;
      rn->ref_i = ref->id;
    }
    rn->first_i = step->first_id;
    switch (step->type) {
    case MGS_TYPE_OP: {
      const MGS_ProgramOpData *op_data = step->data;
      rn->root_i = op_data->sound.root->first_id;
      if (rn->first_i == rn->root_i)
        rn->flag |= MGS_FLAG_EXEC;
      break; }
    default:
      rn->flag |= MGS_FLAG_EXEC;
      break;
    }
    step = step->next;
  }
  o->sndn_count = ra.sound_list.count;
  MGS_PtrArr_mpmemdup(&ra.sound_list, (void***) &o->sound_list, o->mem);
  MGS_PtrArr_mpmemdup(&ra.mod_lists, (void***) &o->mod_lists, o->mem);
  MGS_fini_RunAlloc(&ra);
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_MemPool *mem;
  MGS_Generator *o;
  mem = MGS_create_MemPool(0);
  o = MGS_MemPool_alloc(mem, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->runn_end = prg->node_count;
  o->run_nodes = calloc(prg->node_count, sizeof(RunNode));
  o->mem = mem;
  init_for_nodelist(o);
  MGS_global_init_Wave();
  return o;
}

/*
 * Time adjustment for operators.
 *
 * Click reduction: decrease time to make it end at wave cycle's end.
 */
static mgsNoinline void adjust_op_time(MGS_Generator *o, MGS_OpNode *n) {
  int pos_offs = MGS_Osc_cycle_offs(&n->osc, n->freq, n->sound.time);
  n->sound.time -= pos_offs;
  if (!(o->time_flags & MGS_GEN_TIME_OFFS) || o->delay_offs > pos_offs) {
    o->delay_offs = pos_offs;
    o->time_flags |= MGS_GEN_TIME_OFFS;
  }
}

static void MGS_Generator_update_node(MGS_Generator *o, RunNode *rn) {
  RunNode *refrn = &o->run_nodes[rn->ref_i];
  MGS_SoundNode *refsn = refrn->node;
  switch (refsn->type) {
  case MGS_TYPE_OP: {
    MGS_OpNode *refn = (MGS_OpNode*) refsn;
    MGS_OpNode *updn = (MGS_OpNode*) rn->node;
    MGS_SoundNode *rootsn = o->sound_list[refsn->root_base_i];
    bool adjtime = false;
    /* set state */
    if (updn->sound.params & MGS_AMODS) {
      refn->sound.amods_id = updn->sound.amods_id;
      o->need_upsize = true;
    }
    if (updn->sound.params & MGS_FMODS) {
      refn->fmods_id = updn->fmods_id;
      o->need_upsize = true;
    }
    if (updn->sound.params & MGS_PMODS) {
      refn->pmods_id = updn->pmods_id;
      o->need_upsize = true;
    }
    if (updn->sound.params & MGS_TIME) {
      refn->sound.time = updn->sound.time;
      refrn->pos = 0;
      if (refn->sound.time) {
        if (refsn == rootsn)
          refrn->flag |= MGS_FLAG_EXEC;
        adjtime = true;
      } else
        refrn->flag &= ~MGS_FLAG_EXEC;
    }
    if (updn->sound.params & MGS_WAVE) {
      refn->osc.lut = updn->osc.lut;
    }
    if (updn->sound.params & MGS_FREQ) {
      refn->freq = updn->freq;
      adjtime = true;
    }
    if (updn->sound.params & MGS_DYNFREQ) {
      refn->dynfreq = updn->dynfreq;
    }
    if (updn->sound.params & MGS_PHASE) {
      refn->osc.phase = updn->osc.phase;
    }
    if (updn->sound.params & MGS_AMP) {
      refn->sound.amp = updn->sound.amp;
    }
    if (updn->sound.params & MGS_DYNAMP) {
      refn->sound.dynamp = updn->sound.dynamp;
    }
    if (updn->sound.params & MGS_PAN) {
      refn->sound.pan = updn->sound.pan;
    }
    if (updn->sound.params & MGS_ATTR) {
      refn->attr = updn->attr;
    }
    if (refsn == rootsn) {
      if (adjtime) /* here so new freq also used if set */
        adjust_op_time(o, refn);
    }
    /* take over place of ref'd node */
    *rn = *refrn;
    refrn->flag &= ~MGS_FLAG_EXEC;
    break; }
  case MGS_TYPE_ENV:
    break;
  }
}

static void MGS_Generator_enter_node(MGS_Generator *o, RunNode *rn) {
  if (rn->flag & MGS_FLAG_UPDATE) {
    MGS_Generator_update_node(o, rn);
    rn->flag |= MGS_FLAG_ENTERED;
    return;
  }
  MGS_SoundNode *sndn = rn->node;
  if (!sndn) {
    /* no-op node */
    rn->flag = MGS_FLAG_ENTERED;
    return;
  }
  if (rn->first_i == rn->root_i)
    o->need_upsize = true;
  switch (sndn->type) {
  case MGS_TYPE_OP:
    if (rn->first_i == rn->root_i)
      adjust_op_time(o, (MGS_OpNode*) sndn);
    break;
  }
  rn->flag |= MGS_FLAG_ENTERED;
}

void MGS_destroy_Generator(MGS_Generator *o) {
  if (!o)
    return;
  free(o->bufs);
  free(o->run_nodes);
  MGS_destroy_MemPool(o->mem);
}

/*
 * node block processing
 */

enum {
  BLOCK_WAVEENV = 1<<0,
};

static void run_block_op(MGS_Generator *o, Buf *bufs_from, uint32_t len,
    MGS_OpNode *n, Buf *parentfreq,
    uint32_t layer, uint32_t flags) {
  uint32_t i;
  Buf *sbuf = bufs_from++, *freq, *amp, *pm;
  Buf *next_buf = bufs_from;
  freq = next_buf++;
  if (n->attr & MGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq * parentfreq->f[i];
  } else {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq;
  }
  if (n->fmods_id > 0) {
    MGS_ModList *fmod_list = o->mod_lists[n->fmods_id];
    Buf *fmbuf;
    for (size_t i = 0; i < fmod_list->count; ++i) {
      run_block_op(o, next_buf, len,
          (MGS_OpNode*) o->sound_list[fmod_list->ids[i]], freq,
          i, BLOCK_WAVEENV);
    }
    fmbuf = next_buf;
    if (n->attr & MGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq * parentfreq->f[i] - freq->f[i]) * fmbuf->f[i];
    } else {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq - freq->f[i]) * fmbuf->f[i];
    }
  }
  if (n->sound.amods_id > 0) {
    MGS_ModList *amod_list = o->mod_lists[n->sound.amods_id];
    for (size_t i = 0; i < amod_list->count; ++i) {
      run_block_op(o, next_buf, len,
          (MGS_OpNode*) o->sound_list[amod_list->ids[i]], freq,
          i, BLOCK_WAVEENV);
    }
    amp = next_buf++;
    float dynampdiff = n->sound.dynamp - n->sound.amp;
    for (i = 0; i < len; ++i)
      amp->f[i] = n->sound.amp + amp->f[i] * dynampdiff;
  } else {
    amp = (next_buf++);
    for (i = 0; i < len; ++i)
      amp->f[i] = n->sound.amp;
  }
  pm = NULL;
  if (n->pmods_id > 0) {
    MGS_ModList *pmod_list = o->mod_lists[n->pmods_id];
    for (size_t i = 0; i < pmod_list->count; ++i) {
      run_block_op(o, next_buf, len,
          (MGS_OpNode*) o->sound_list[pmod_list->ids[i]], freq,
          i, 0);
    }
    pm = next_buf++;
  }
  if (flags & BLOCK_WAVEENV) {
    MGS_Osc_run_env(&n->osc, sbuf->f, len,
        layer, freq->f, amp->f, (pm != NULL) ? pm->f : NULL);
  } else {
    MGS_Osc_run(&n->osc, sbuf->f, len,
        layer, freq->f, amp->f, (pm != NULL) ? pm->f : NULL);
  }
}

static uint32_t run_sound(MGS_Generator *o,
    MGS_SoundNode *sndn, short *sp, uint32_t pos, uint32_t len) {
  uint32_t i, ret, time = sndn->time - pos;
  if (time > len)
    time = len;
  ret = time;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    switch (sndn->type) {
    case MGS_TYPE_OP:
      // only works if all linked nodes have type OP
      run_block_op(o, o->bufs, len,
          (MGS_OpNode*) sndn, NULL,
          0, 0);
      break;
    }
    float pan = (1.f + sndn->pan) * .5f;
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
  upsize_bufs(o);
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
      MGS_SoundNode *sndn = rn->node;
      rn->pos += run_sound(o, sndn, buf, rn->pos, len);
      if ((uint32_t)rn->pos == sndn->time)
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
