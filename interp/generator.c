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
  int delay_offs;
  int time_flags;
  uint32_t voice_count;
  MGS_OpNode **op_list;
  MGS_VoiceNode *voice_arr;
  MGS_ModList **mod_lists;
  uint32_t ev_i, ev_count;
  MGS_EventNode *ev_arr;
  MGS_MemPool *mem;
};

static void init_for_nodelist(MGS_Generator *o) {
  MGS_RunAlloc ra;
  const MGS_Program *prg = o->prg;
  MGS_init_RunAlloc(&ra, prg, o->srate, o->mem);
  MGS_RunAlloc_for_nodelist(&ra, prg->node_list);
  o->op_list = ra.op_list;
  o->ev_count = ra.ev_arr.count;
  o->voice_count = ra.voice_arr.count;
  MGS_EventArr_mpmemdup(&ra.ev_arr, &o->ev_arr, o->mem);
  MGS_VoiceArr_mpmemdup(&ra.voice_arr, &o->voice_arr, o->mem);
  MGS_PtrArr_mpmemdup(&ra.mod_lists, (void***) &o->mod_lists, o->mem);
  o->bufs = MGS_MemPool_alloc(o->mem, ra.max_bufs * sizeof(Buf));
  MGS_fini_RunAlloc(&ra);
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_MemPool *mem;
  MGS_Generator *o;
  mem = MGS_create_MemPool(0);
  o = MGS_MemPool_alloc(mem, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->mem = mem;
  init_for_nodelist(o);
  MGS_global_init_Noise();
  MGS_global_init_Wave();
  return o;
}

/*
 * Time adjustment for wave data.
 *
 * Click reduction: decrease time to make it end at wave cycle's end.
 */
static mgsNoinline void adjust_wave_time(MGS_Generator *o, MGS_WaveNode *n) {
  int pos_offs = MGS_Osc_cycle_offs(&n->osc, n->freq, n->op.time);
  n->op.time -= pos_offs;
  if (!(o->time_flags & MGS_GEN_TIME_OFFS) || o->delay_offs > pos_offs) {
    o->delay_offs = pos_offs;
    o->time_flags |= MGS_GEN_TIME_OFFS;
  }
}

static void MGS_Generator_init_op(MGS_Generator *o, MGS_EventNode *ev) {
  MGS_OpNode *opn = ev->opn;
  MGS_VoiceNode *voice = &o->voice_arr[opn->voice_id];
  if (opn == voice->root) {
    ev->status |= MGS_EV_ACTIVE;
  }
  switch (opn->type) {
  case MGS_TYPE_WAVE:
    if (opn == voice->root)
      adjust_wave_time(o, (MGS_WaveNode*) opn);
    break;
  }
}

static void MGS_Generator_update_op(MGS_Generator *o, MGS_EventNode *ev) {
  MGS_EventNode *refev = &o->ev_arr[ev->ref_i];
  MGS_OpNode *refsn = refev->opn;
  MGS_OpNode *updsn = ev->opn;
  MGS_VoiceNode *voice = &o->voice_arr[refsn->voice_id];
  MGS_OpNode *rootsn = voice->root;
  bool adjtime = false;
  refsn->amods_id = updsn->amods_id;
  if (updsn->params & MGS_OPP_TIME) {
    refsn->time = updsn->time;
    refev->pos = 0;
    if (refsn->time) {
      if (refsn == rootsn)
        refev->status |= MGS_EV_ACTIVE;
      adjtime = true;
    } else
      refev->status &= ~MGS_EV_ACTIVE;
  }
  if (updsn->params & MGS_OPP_AMP) {
    refsn->amp = updsn->amp;
  }
  if (updsn->params & MGS_OPP_DYNAMP) {
    refsn->dynamp = updsn->dynamp;
  }
  if (updsn->params & MGS_OPP_PAN) {
    refsn->pan = updsn->pan;
  }
  switch (refsn->type) {
  case MGS_TYPE_NOISE:
    break;
  case MGS_TYPE_WAVE: {
    MGS_WaveNode *refn = (MGS_WaveNode*) refsn;
    MGS_WaveNode *updn = (MGS_WaveNode*) updsn;
    /* set state */
    refn->fmods_id = updn->fmods_id;
    refn->pmods_id = updn->pmods_id;
    if (updn->op.params & MGS_WAVEP_WAVE) {
      refn->osc.lut = updn->osc.lut;
    }
    if (updn->op.params & MGS_WAVEP_FREQ) {
      refn->freq = updn->freq;
      adjtime = true;
    }
    if (updn->op.params & MGS_WAVEP_DYNFREQ) {
      refn->dynfreq = updn->dynfreq;
    }
    if (updn->op.params & MGS_WAVEP_PHASE) {
      refn->osc.phase = updn->osc.phase;
    }
    if (updn->op.params & MGS_WAVEP_ATTR) {
      refn->attr = updn->attr;
    }
    if (refsn == rootsn) {
      if (adjtime) /* here so new freq also used if set */
        adjust_wave_time(o, refn);
    }
    break; }
  }
  /* take over place of ref'd node */
  *ev = *refev;
  refev->status &= ~MGS_EV_ACTIVE;
}

static void MGS_Generator_prepare_node(MGS_Generator *o, MGS_EventNode *ev) {
  switch (ev->base_type) {
  case MGS_BASETYPE_OP:
    if (!(ev->status & MGS_EV_UPDATE)) {
      MGS_Generator_init_op(o, ev);
    } else {
      MGS_Generator_update_op(o, ev);
    }
    break;
  default:
    /* handle unsupported node as no-op */
    break;
  }
  ev->status |= MGS_EV_PREPARED;
}

void MGS_destroy_Generator(MGS_Generator *o) {
  if (!o)
    return;
  MGS_destroy_MemPool(o->mem);
}

/*
 * node block processing
 */

enum {
  BLOCK_WAVEENV = 1<<0,
};

static void run_block_sub(MGS_Generator *o, Buf *bufs_from, uint32_t len,
    uint32_t mods_id, Buf *freq,
    uint32_t flags);

static void run_block_noise(MGS_Generator *o, Buf *bufs_from, uint32_t len,
    MGS_NoiseNode *n,
    uint32_t layer, uint32_t flags) {
  uint32_t i;
  Buf *sbuf = bufs_from++, *amp;
  Buf *next_buf = bufs_from;
  if (n->op.amods_id > 0) {
    run_block_sub(o, next_buf, len,
        n->op.amods_id, NULL,
        BLOCK_WAVEENV);
    amp = next_buf++;
    float dynampdiff = n->op.dynamp - n->op.amp;
    for (i = 0; i < len; ++i)
      amp->f[i] = n->op.amp + amp->f[i] * dynampdiff;
  } else {
    amp = (next_buf++);
    for (i = 0; i < len; ++i)
      amp->f[i] = n->op.amp;
  }
  if (flags & BLOCK_WAVEENV) {
    MGS_NGen_run_env(&n->ngen, sbuf->f, len,
        layer, amp->f);
  } else {
    MGS_NGen_run(&n->ngen, sbuf->f, len,
        layer, amp->f);
  }
}

static void run_block_wave(MGS_Generator *o, Buf *bufs_from, uint32_t len,
    MGS_WaveNode *n, Buf *parentfreq,
    uint32_t layer, uint32_t flags) {
  uint32_t i;
  Buf *sbuf = bufs_from++, *freq, *amp, *pm;
  Buf *next_buf = bufs_from;
  freq = next_buf++;
  if ((n->attr & MGS_ATTR_FREQRATIO) != 0 && parentfreq != NULL) {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq * parentfreq->f[i];
  } else {
    for (i = 0; i < len; ++i)
      freq->f[i] = n->freq;
  }
  if (n->fmods_id > 0) {
    run_block_sub(o, next_buf, len,
        n->fmods_id, freq,
        BLOCK_WAVEENV);
    Buf *fmbuf = next_buf;
    if ((n->attr & MGS_ATTR_FREQRATIO) != 0 && parentfreq != NULL) {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq * parentfreq->f[i] - freq->f[i]) * fmbuf->f[i];
    } else {
      for (i = 0; i < len; ++i)
        freq->f[i] += (n->dynfreq - freq->f[i]) * fmbuf->f[i];
    }
  }
  if (n->op.amods_id > 0) {
    run_block_sub(o, next_buf, len,
        n->op.amods_id, freq,
        BLOCK_WAVEENV);
    amp = next_buf++;
    float dynampdiff = n->op.dynamp - n->op.amp;
    for (i = 0; i < len; ++i)
      amp->f[i] = n->op.amp + amp->f[i] * dynampdiff;
  } else {
    amp = (next_buf++);
    for (i = 0; i < len; ++i)
      amp->f[i] = n->op.amp;
  }
  pm = NULL;
  if (n->pmods_id > 0) {
    run_block_sub(o, next_buf, len,
        n->pmods_id, freq,
        0);
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

static void run_block_sub(MGS_Generator *o, Buf *bufs_from, uint32_t len,
    uint32_t mods_id, Buf *freq,
    uint32_t flags) {
  MGS_ModList *mod_list = o->mod_lists[mods_id];
  for (size_t i = 0; i < mod_list->count; ++i) {
    MGS_OpNode *n = o->op_list[mod_list->ids[i]];
    switch (n->type) {
    case MGS_TYPE_NOISE:
      run_block_noise(o, bufs_from, len,
          (MGS_NoiseNode*) n,
          i, flags);
      break;
    case MGS_TYPE_WAVE:
      run_block_wave(o, bufs_from, len,
          (MGS_WaveNode*) n, freq,
          i, flags);
      break;
    }
  }
}

static uint32_t run_op(MGS_Generator *o,
    MGS_OpNode *opn, short *sp, uint32_t pos, uint32_t len) {
  uint32_t i, ret, time = opn->time - pos;
  if (time > len)
    time = len;
  ret = time;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    switch (opn->type) {
    case MGS_TYPE_NOISE:
      run_block_noise(o, o->bufs, len,
          (MGS_NoiseNode*) opn,
          0, 0);
      break;
    case MGS_TYPE_WAVE:
      run_block_wave(o, o->bufs, len,
          (MGS_WaveNode*) opn, NULL,
          0, 0);
      break;
    }
    float pan = (1.f + opn->pan) * .5f;
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
  for (i = o->ev_i; i < o->ev_count; ++i) {
    MGS_EventNode *ev = &o->ev_arr[i];
    if (ev->pos < 0) {
      uint32_t delay = -ev->pos;
      if (o->time_flags & MGS_GEN_TIME_OFFS)
        delay -= o->delay_offs; /* delay change == previous time change */
      if (delay <= len) {
        /*
         * Split processing so that len is no longer than delay,
         * avoiding cases where the node prior to a node disabling it
         * plays too long.
         */
        skiplen = len - delay;
        len = delay;
      }
      break;
    }
    if (!(ev->status & MGS_EV_PREPARED)) {
      /*
       * Ensures disabling node is initialized
       * before disabled node would otherwise play.
       */
      MGS_Generator_prepare_node(o, ev);
    }
  }
  for (i = o->ev_i; i < o->ev_count; ++i) {
    MGS_EventNode *ev = &o->ev_arr[i];
    if (ev->pos < 0) {
      uint32_t delay = -ev->pos;
      if (o->time_flags & MGS_GEN_TIME_OFFS) {
        ev->pos += o->delay_offs; /* delay change == previous time change */
        o->delay_offs = 0;
        o->time_flags &= ~MGS_GEN_TIME_OFFS;
      }
      if (delay >= len) {
        ev->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      ev->pos = 0;
    }
    if (ev->status & MGS_EV_ACTIVE) {
      MGS_OpNode *opn = ev->opn;
      ev->pos += run_op(o, opn, buf, ev->pos, len);
      if ((uint32_t)ev->pos == opn->time)
        ev->status &= ~MGS_EV_ACTIVE;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  if (gen_len != NULL) *gen_len = totlen;
  for(;;) {
    if (o->ev_i == o->ev_count)
      return false;
    uint8_t cur_status = o->ev_arr[o->ev_i].status;
    if (!(cur_status & MGS_EV_PREPARED) || cur_status & MGS_EV_ACTIVE)
      break;
    ++o->ev_i;
  }
  return true;
}
