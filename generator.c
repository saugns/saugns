/* sgensys: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "generator.h"
#include "osc.h"
#include "mempool.h"
#include <stdio.h>
#include <stdlib.h>

#define BUF_LEN 1024
typedef union Buf {
  int32_t i[BUF_LEN];
  float f[BUF_LEN];
} Buf;

/*
 * Operator node flags.
 */
enum {
  ON_VISITED = 1<<0,
  ON_TIME_INF = 1<<1, /* used for SGS_TIMEP_IMPLICIT */
};

typedef struct OperatorNode {
  SGS_Osc osc;
  uint32_t time;
  uint32_t silence;
  uint8_t flags;
  uint8_t wave;
  const SGS_ProgramIDArr *amods, *fmods, *pmods;
  SGS_Ramp amp, freq;
  uint32_t amp_pos, freq_pos;
  float dynamp, dynfreq;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
  VN_INIT = 1<<0,
};

typedef struct VoiceNode {
  int32_t pos; /* negative for wait time */
  uint32_t duration;
  uint8_t flags;
  const SGS_ProgramOpRef *graph;
  uint32_t op_count;
  SGS_Ramp pan;
  uint32_t pan_pos;
} VoiceNode;

typedef struct EventNode {
  uint32_t wait;
  const SGS_ProgramEvent *prg_event;
} EventNode;

struct SGS_Generator {
  double osc_coeff;
  uint32_t srate;
  uint32_t buf_count;
  Buf *bufs;
  size_t event, ev_count;
  EventNode *events;
  uint32_t event_pos;
  uint16_t voice, vo_count;
  VoiceNode *voices;
  float amp_scale;
  uint32_t op_count;
  OperatorNode *operators;
  SGS_Mempool *mem;
};

// maximum number of buffers needed for op nesting depth
#define COUNT_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 4)

static bool alloc_for_program(SGS_Generator *restrict o,
                              const SGS_Program *restrict prg) {
  size_t i;

  i = prg->ev_count;
  if (i > 0) {
    o->events = SGS_mpalloc(o->mem, i * sizeof(EventNode));
    if (!o->events) goto ERROR;
    o->ev_count = i;
  }
  i = prg->vo_count;
  if (i > 0) {
    o->voices = SGS_mpalloc(o->mem, i * sizeof(VoiceNode));
    if (!o->voices) goto ERROR;
    o->vo_count = i;
  }
  i = prg->op_count;
  if (i > 0) {
    o->operators = SGS_mpalloc(o->mem, i * sizeof(OperatorNode));
    if (!o->operators) goto ERROR;
    o->op_count = i;
  }
  i = COUNT_BUFS(prg->op_nest_depth);
  if (i > 0) {
    o->bufs = calloc(i, sizeof(Buf));
    if (!o->bufs) goto ERROR;
    o->buf_count = i;
  }

  return true;
ERROR:
  return false;
}

static bool convert_program(SGS_Generator *restrict o,
                            const SGS_Program *restrict prg, uint32_t srate) {
  if (!alloc_for_program(o, prg))
    return false;

  int ev_time_carry = 0;
  uint32_t vo_wait_time = 0;
  o->osc_coeff = SGS_Osc_SRATE_COEFF(srate);
  o->srate = srate;
  o->amp_scale = 1.f;
  if ((prg->mode & SGS_PMODE_AMP_DIV_VOICES) != 0)
    o->amp_scale /= o->vo_count;
  for (size_t i = 0; i < prg->ev_count; ++i) {
    const SGS_ProgramEvent *prg_e = &prg->events[i];
    EventNode *e = &o->events[i];
    uint16_t vo_id = prg_e->vo_id;
    e->wait = SGS_ms_in_samples(prg_e->wait_ms, srate, &ev_time_carry);
    e->prg_event = prg_e;
    vo_wait_time += e->wait;
    if (prg_e->vo_data) {
      o->voices[vo_id].pos = -vo_wait_time;
      vo_wait_time = 0;
    }
  }

  return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(const SGS_Program *restrict prg,
                                    uint32_t srate) {
  SGS_Mempool *mem = SGS_create_Mempool(0);
  if (!mem)
    return NULL;
  SGS_Generator *o = SGS_mpalloc(mem, sizeof(SGS_Generator));
  if (!o) {
    SGS_destroy_Mempool(mem);
    return NULL;
  }
  o->mem = mem;
  if (!convert_program(o, prg, srate)) {
    SGS_destroy_Generator(o);
    return NULL;
  }
  SGS_global_init_Wave();
  return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Generator(SGS_Generator *restrict o) {
  if (!o)
    return;
  free(o->bufs);
  SGS_destroy_Mempool(o->mem);
}

static const SGS_ProgramIDArr blank_idarr = {0};

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SGS_Generator *restrict o,
                               VoiceNode *restrict vn) {
  uint32_t time = 0;
  for (uint32_t i = 0; i < vn->op_count; ++i) {
    const SGS_ProgramOpRef *or = &vn->graph[i];
    if (or->use != SGS_POP_CARR) continue;
    OperatorNode *on = &o->operators[or->id];
    if (on->time > time)
      time = on->time;
  }
  vn->duration = time;
}

/*
 * Process an event update for a timed parameter.
 */
static void handle_ramp_update(SGS_Ramp *restrict ramp,
                               uint32_t *restrict ramp_pos,
                               const SGS_Ramp *restrict ramp_src) {
  if ((ramp_src->flags & SGS_RAMPP_GOAL) != 0) {
    *ramp_pos = 0;
  }
  SGS_Ramp_copy(ramp, ramp_src);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *restrict o, EventNode *restrict e) {
  if (1) /* more types to be added in the future */ {
    const SGS_ProgramEvent *pe = e->prg_event;
    const SGS_ProgramVoData *vd = pe->vo_data;
    /*
     * Set state of operator and/or voice.
     *
     * Voice updates must be done last, to take into account
     * updates for their operators.
     */
    VoiceNode *vn = NULL;
    if (pe->vo_id != SGS_PVO_NO_ID)
      vn = &o->voices[pe->vo_id];
    for (size_t i = 0; i < pe->op_data_count; ++i) {
      const SGS_ProgramOpData *od = &pe->op_data[i];
      OperatorNode *on = &o->operators[od->id];
      uint32_t params = od->params;
      if (od->amods) on->amods = od->amods;
      if (od->fmods) on->fmods = od->fmods;
      if (od->pmods) on->pmods = od->pmods;
      if (!on->amods) on->amods = &blank_idarr;
      if (!on->fmods) on->fmods = &blank_idarr;
      if (!on->pmods) on->pmods = &blank_idarr;
      if (params & SGS_POPP_WAVE)
        on->wave = od->wave;
      if (params & SGS_POPP_TIME) {
        const SGS_Time *src = &od->time;
        if (src->flags & SGS_TIMEP_IMPLICIT) {
          on->time = 0;
          on->flags |= ON_TIME_INF;
        } else {
          on->time = SGS_ms_in_samples(src->v_ms, o->srate, NULL);
          on->flags &= ~ON_TIME_INF;
        }
      }
      if (params & SGS_POPP_SILENCE)
        on->silence = SGS_ms_in_samples(od->silence_ms, o->srate, NULL);
      if (params & SGS_POPP_FREQ)
        handle_ramp_update(&on->freq, &on->freq_pos, &od->freq);
      if (params & SGS_POPP_DYNFREQ)
        on->dynfreq = od->dynfreq;
      if (params & SGS_POPP_PHASE)
        SGS_Osc_SET_PHASE(&on->osc, od->phase);
      if (params & SGS_POPP_AMP)
        handle_ramp_update(&on->amp, &on->amp_pos, &od->amp);
      if (params & SGS_POPP_DYNAMP)
        on->dynamp = od->dynamp;
    }
    if (vd) {
      uint32_t params = vd->params;
      if (vd->op_list) {
        vn->graph = vd->op_list;
        vn->op_count = vd->op_count;
      }
      if (params & SGS_PVOP_PAN)
        handle_ramp_update(&vn->pan, &vn->pan_pos, &vd->pan);
    }
    if (vn) {
      vn->flags |= VN_INIT;
      vn->pos = 0;
      if (o->voice > pe->vo_id) /* go back to re-activated node */
        o->voice = pe->vo_id;
      set_voice_duration(o, vn);
    }
  }
}

/*
 * Generate up to buf_len samples for an operator node, the remainder (if any)
 * zero-filled if acc_ind is zero.
 *
 * Recursively visits the subnodes of the operator node in the process, if
 * any.
 *
 * Returns number of samples generated for the node.
 */
static uint32_t run_block(SGS_Generator *restrict o,
                          Buf *restrict bufs, uint32_t buf_len,
                          OperatorNode *restrict n,
                          float *restrict parent_freq,
                          bool wave_env, uint32_t acc_ind) {
  uint32_t i, len;
  int32_t *sbuf = bufs->i, *pm;
  float *freq, *amp;
  Buf *nextbuf = bufs + 1;
  len = buf_len;
  /*
   * If silence, zero-fill and delay processing for duration.
   */
  uint32_t zero_len = 0;
  if (n->silence) {
    zero_len = n->silence;
    if (zero_len > len)
      zero_len = len;
    if (!acc_ind) for (i = 0; i < zero_len; ++i)
      sbuf[i] = 0;
    len -= zero_len;
    if (!(n->flags & ON_TIME_INF)) n->time -= zero_len;
    n->silence -= zero_len;
    if (!len) return zero_len;
    sbuf += zero_len;
  }
  /*
   * Guard against circular references.
   */
  if ((n->flags & ON_VISITED) != 0) {
      for (i = 0; i < len; ++i)
        sbuf[i] = 0;
      return zero_len + len;
  }
  n->flags |= ON_VISITED;
  /*
   * Limit length to time duration of operator.
   */
  uint32_t skip_len = 0;
  if (n->time < len && !(n->flags & ON_TIME_INF)) {
    skip_len = len - n->time;
    len = n->time;
  }
  /*
   * Handle frequency (alternatively ratio) parameter,
   * including frequency modulation if modulators linked.
   */
  freq = (nextbuf++)->f;
  SGS_Ramp_run(&n->freq, freq, len, o->srate, &n->freq_pos, parent_freq);
  if (n->fmods->count) {
    const uint32_t *fmods = n->fmods->ids;
    float *fm_buf;
    for (i = 0; i < n->fmods->count; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, true, i);
    fm_buf = nextbuf->f;
    if ((n->freq.flags & SGS_RAMPP_STATE_RATIO) != 0) {
      for (i = 0; i < len; ++i)
        freq[i] += (n->dynfreq * parent_freq[i] - freq[i]) * fm_buf[i];
    } else {
      for (i = 0; i < len; ++i)
        freq[i] += (n->dynfreq - freq[i]) * fm_buf[i];
    }
  }
  /*
   * If phase modulators linked, get phase offsets for modulation.
   */
  pm = 0;
  if (n->pmods->count) {
    const uint32_t *pmods = n->pmods->ids;
    for (i = 0; i < n->pmods->count; ++i)
      run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, false, i);
    pm = (nextbuf++)->i;
  }
  /*
   * Handle amplitude parameter, including amplitude modulation if
   * modulators linked.
   */
  if (n->amods->count) {
    const uint32_t *amods = n->amods->ids;
    float dynampdiff = n->dynamp - n->amp.v0;
    for (i = 0; i < n->amods->count; ++i)
      run_block(o, nextbuf, len, &o->operators[amods[i]], freq, true, i);
    amp = (nextbuf++)->f;
    for (i = 0; i < len; ++i)
      amp[i] = n->amp.v0 + amp[i] * dynampdiff;
  } else {
    amp = (nextbuf++)->f;
    SGS_Ramp_run(&n->amp, amp, len, o->srate, &n->amp_pos, NULL);
  }
  if (!wave_env) {
    /*
     * Generate integer output - either for voice output or phase modulation
     * input.
     */
    const float *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      int32_t s, spm = 0;
      float sfreq = freq[i];
      float samp = amp[i];
      if (pm) spm = pm[i];
      s = lrintf(SGS_Osc_run(&n->osc, lut, o->osc_coeff, sfreq, spm) * samp *
                 (float) INT16_MAX);
      if (acc_ind) s += sbuf[i];
      sbuf[i] = s;
    }
  } else {
    float *f_sbuf = (float*) sbuf;
    /*
     * Generate float output - used as waveform envelopes for modulating
     * frequency or amplitude.
     */
    const float *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      float s;
      float sfreq = freq[i];
      float samp = amp[i] * 0.5f;
      int32_t spm = 0;
      if (pm) spm = pm[i];
      s = SGS_Osc_run(&n->osc, lut, o->osc_coeff, sfreq, spm);
      s = (s * samp) + fabs(samp);
      if (acc_ind) s *= f_sbuf[i];
      f_sbuf[i] = s;
    }
  }
  /*
   * Update time duration left, zero rest of buffer if unfilled.
   */
  if (!(n->flags & ON_TIME_INF)) {
    if (!acc_ind && skip_len > 0) {
      sbuf += len;
      for (i = 0; i < skip_len; ++i)
        sbuf[i] = 0;
    }
    n->time -= len;
  }
  n->flags &= ~ON_VISITED;
  return zero_len + len;
}

/*
 * Mix output for voice node \p vn into the 16-bit stereo (interleaved)
 * \p st_out buffer from the first generator buffer. Advances the st_out
 * pointer.
 *
 * The second generator buffer is used for panning if dynamic panning
 * is used.
 */
static void mix_output(SGS_Generator *restrict o, VoiceNode *restrict vn,
                       int16_t **restrict st_out, uint32_t len) {
  int32_t *s_buf = o->bufs[0].i;
  float scale = o->amp_scale;
  if (vn->pan.flags & SGS_RAMPP_GOAL) {
    float *pan_buf = o->bufs[1].f;
    SGS_Ramp_run(&vn->pan, pan_buf, len, o->srate, &vn->pan_pos, NULL);
    for (uint32_t i = 0; i < len; ++i) {
      float s = s_buf[i] * scale;
      float p = s * pan_buf[i];
      *(*st_out)++ += lrintf(s - p);
      *(*st_out)++ += lrintf(p);
    }
  } else {
    for (uint32_t i = 0; i < len; ++i) {
      float s = s_buf[i] * scale;
      float p = s * vn->pan.v0;
      *(*st_out)++ += lrintf(s - p);
      *(*st_out)++ += lrintf(p);
    }
  }
}

/*
 * Generate up to buf_len samples for a voice, these mixed into the
 * interleaved output stereo buffer by simple addition.
 *
 * Returns number of samples generated for the voice.
 */
static uint32_t run_voice(SGS_Generator *restrict o, VoiceNode *restrict vn,
                          int16_t *restrict out, uint32_t buf_len) {
  uint32_t out_len = 0;
  const SGS_ProgramOpRef *ops = vn->graph;
  uint32_t opc = vn->op_count;
  if (!ops) goto RETURN;
  uint32_t time;
  uint32_t i;
  time = vn->duration;
  if (time > buf_len) time = buf_len;
  /*
   * Repeatedly generate up to BUF_LEN samples until done.
   */
  int16_t *sp = out;
  while (time) {
    uint32_t acc_ind = 0;
    uint32_t gen_len = 0;
    uint32_t len = (time < BUF_LEN) ? time : BUF_LEN;
    time -= len;
    for (i = 0; i < opc; ++i) {
      uint32_t last_len;
      if (ops[i].use != SGS_POP_CARR) continue; // TODO: finish redesign
      OperatorNode *n = &o->operators[ops[i].id];
      if (n->time == 0) continue;
      last_len = run_block(o, o->bufs, len, n, 0, false, acc_ind++);
      if (last_len > gen_len) gen_len = last_len;
    }
    if (!gen_len) goto RETURN;
    mix_output(o, vn, &sp, gen_len);
    out_len += gen_len;
    vn->duration = (vn->duration > gen_len) ?
      vn->duration - gen_len :
      0;
  }
RETURN:
  vn->pos += out_len;
  return out_len;
}

/*
 * Any error checking following audio generation goes here.
 */
static void check_final_state(SGS_Generator *restrict o) {
  for (uint16_t i = 0; i < o->vo_count; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (!(vn->flags & VN_INIT)) {
      SGS_warning("generator",
"voice %hd left uninitialized (never used)", i);
    }
  }
}

/**
 * Main audio generation/processing function. Call repeatedly to write
 * buf_len new samples into the interleaved stereo buffer buf. Any values
 * after the end of the signal will be zero'd.
 *
 * If supplied, out_len will be set to the precise length generated.
 * for the call, which is buf_len unless the signal ended earlier.
 *
 * Return true as long as there are more samples to generate, false
 * when the end of the signal has been reached.
 */
bool SGS_Generator_run(SGS_Generator *restrict o,
                       int16_t *restrict buf, size_t buf_len,
                       size_t *restrict out_len) {
  int16_t *sp = buf;
  uint32_t i, len = buf_len;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
  uint32_t skip_len, gen_len = 0;
PROCESS:
  skip_len = 0;
  while (o->event < o->ev_count) {
    EventNode *e = &o->events[o->event];
    if (o->event_pos < e->wait) {
      uint32_t waittime = e->wait - o->event_pos;
      if (waittime < len) {
        /*
         * Limit len to waittime, further splitting processing into two
         * blocks; otherwise, voice processing could get ahead of event
         * handling in some cases - which would give undefined results!
         */
        skip_len = len - waittime;
        len = waittime;
      }
      o->event_pos += len;
      break;
    }
    handle_event(o, e);
    ++o->event;
    o->event_pos = 0;
  }
  uint32_t last_len = 0;
  for (i = o->voice; i < o->vo_count; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint32_t waittime = (uint32_t) -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->duration != 0) {
      uint32_t voice_len = run_voice(o, vn, buf, len);
      if (voice_len > last_len) last_len = voice_len; 
    }
  }
  gen_len += last_len;
  if (skip_len) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skip_len;
    goto PROCESS;
  }
  /*
   * Advance starting voice and check for end of signal.
   */
  for(;;) {
    VoiceNode *vn;
    if (o->voice == o->vo_count) {
      if (o->event != o->ev_count) break;
      /*
       * The end.
       */
      if (out_len) *out_len = gen_len;
      check_final_state(o);
      return false;
    }
    vn = &o->voices[o->voice];
    if (vn->duration != 0) break;
    ++o->voice;
  }
  /*
   * Further calls needed to complete signal.
   */
  if (out_len) *out_len = buf_len;
  return true;
}
