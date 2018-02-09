/* sgensys: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "generator.h"
#include "osc.h"
#include <stdio.h>
#include <stdlib.h>

#define MS_TO_SRT(ms, srate) \
  lrintf(((ms) * .001f) * (srate))

typedef union BufValue {
  int32_t i;
  float f;
} BufValue;

#define BUF_LEN 256
typedef BufValue Buf[BUF_LEN];

typedef struct ParameterValit {
  uint32_t time, pos;
  float goal;
  uint8_t type;
} ParameterValit;

/*
 * Operator node flags.
 */
enum {
  ON_VISITED = 1<<0,
};

typedef struct OperatorNode {
  SGS_Osc osc;
  int32_t time;
  uint32_t silence;
  uint8_t flags;
  uint8_t attr;
  uint8_t wave;
  const SGS_ProgramGraphAdjcs *adjcs;
  float amp, dynamp;
  float freq, dynfreq;
  ParameterValit valitamp, valitfreq;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
  VN_INIT = 1<<0,
  VN_EXEC = 1<<1
};

typedef struct VoiceNode {
  int32_t pos; /* negative for wait time */
  uint8_t flags;
  uint8_t attr;
  const SGS_ProgramGraph *graph;
  float panning;
  ParameterValit valitpanning;
} VoiceNode;

typedef union EventValue {
  int32_t i;
  float f;
  void *v;
} EventValue;

typedef struct EventNode {
  EventValue *data;
  uint32_t waittime;
  uint32_t params;
  int32_t voice_id;
  int32_t operator_id;
} EventNode;

static uint32_t count_flags(uint32_t flags) {
  uint32_t i, count = 0;
  for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
    count += flags & 1;
    flags >>= 1;
  }
  return count;
}

struct SGS_Generator {
  double osc_coeff;
  uint32_t srate;
  uint32_t buf_count;
  Buf *bufs;
  size_t event, event_count;
  EventNode *events;
  uint32_t event_pos;
  uint16_t voice, voice_count;
  VoiceNode *voices;
  float amp_scale;
  uint32_t operator_count;
  OperatorNode *operators;
  EventValue *event_values;
};

/*
 * Count buffers needed for operator, including linked operators.
 * TODO: Redesign, do graph traversal before generator.
 */
static uint32_t calc_bufs(SGS_Generator *o, int32_t op_id) {
  uint32_t count = 0, i, res;
  OperatorNode *n = &o->operators[op_id];
  if ((n->flags & ON_VISITED) != 0) {
    SGS_warning("generator",
                "skipping operator %d; circular references unsupported",
                op_id);
    return 0;
  }
  if (n->adjcs) {
    const int32_t *mods = n->adjcs->adjcs;
    const uint32_t modc = n->adjcs->fmodc +
      n->adjcs->pmodc +
      n->adjcs->amodc;
    n->flags |= ON_VISITED;
    for (i = 0; i < modc; ++i) {
      int32_t next_id = mods[i];
//			printf("visit node %d\n", next_id);
      res = calc_bufs(o, next_id);
      if (res > count) count = res;
    }
    n->flags &= ~ON_VISITED;
  }
  return count + 5;
}

/*
 * Check operators for voice and increase the buffer allocation if needed.
 * TODO: Redesign, do graph traversal before generator.
 */
static void upsize_bufs(SGS_Generator *o, VoiceNode *vn) {
  uint32_t count = 0, i, res;
  if (!vn->graph) return;
  for (i = 0; i < vn->graph->opc; ++i) {
//		printf("visit node %d\n", vn->graph->ops[i]);
    res = calc_bufs(o, vn->graph->ops[i]);
    if (res > count) count = res;
  }
//	printf("need %d buffers (have %d)\n", count, o->buf_count);
  if (count > o->buf_count) {
//		printf("new alloc size 0x%lu\n", sizeof(Buf) * count);
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->buf_count = count;
  }
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(SGS_Program *prg, uint32_t srate) {
  SGS_Generator *o = calloc(1, sizeof(SGS_Generator));
  if (!o) return NULL;
  o->osc_coeff = SGS_Osc_SRATE_COEFF(srate);
  o->srate = srate;
  o->amp_scale = 1.f;
  o->event_count = prg->event_count;
  if (o->event_count > 0) {
    o->events = calloc(o->event_count, sizeof(EventNode));
    if (!o->events) {
      SGS_destroy_Generator(o);
      return NULL;
    }
  }
  o->voice_count = prg->voice_count;
  if (o->voice_count > 0) {
    o->voices = calloc(o->voice_count, sizeof(VoiceNode));
    if (!o->voices) {
      SGS_destroy_Generator(o);
      return NULL;
    }
    if ((prg->flags & SGS_PROG_AMP_DIV_VOICES) != 0)
      o->amp_scale /= o->voice_count;
  }
  o->operator_count = prg->operator_count;
  if (o->operator_count > 0) {
    o->operators = calloc(o->operator_count, sizeof(OperatorNode));
    if (!o->operators) {
      SGS_destroy_Generator(o);
      return NULL;
    }
  }
  size_t event_value_count = 0;
  for (size_t i = 0; i < o->event_count; ++i) {
    const SGS_ProgramEvent *ev = prg->events[i];
    event_value_count += count_flags(ev->params) +
      count_flags(ev->params &
        (SGS_P_VALITFREQ |
        SGS_P_VALITAMP |
        SGS_P_VALITPANNING)) * 2;
  }
  if (event_value_count > 0) {
    o->event_values = calloc(event_value_count, sizeof(EventValue));
    if (!o->event_values) {
      SGS_destroy_Generator(o);
      return NULL;
    }
  }
  SGS_global_init_Wave();
  /*
   * Fill in event data according to the SGS_Program, i.e.
   * copy timed state changes for voices and operators.
   */
  EventValue *event_values = o->event_values;
  uint32_t indexwaittime = 0;
  for (size_t i = 0; i < o->event_count; ++i) {
    const SGS_ProgramEvent *prg_e = prg->events[i];
    EventNode *e = &o->events[i];
    EventValue *val = event_values;
    e->data = val;
    e->waittime = MS_TO_SRT(prg_e->wait_ms, srate);
    indexwaittime += e->waittime;
    e->voice_id = -1;
    e->operator_id = -1;
    e->params = prg_e->params;
    if (prg_e->operator) {
      const SGS_ProgramOpData *pod = prg_e->operator;
      e->voice_id = prg_e->voice_id;
      e->operator_id = pod->operator_id;
      if (e->params & SGS_P_ADJCS)
        (*val++).v = (void*)pod->adjcs;
      if (e->params & SGS_P_OPATTR)
        (*val++).i = pod->attr;
      if (e->params & SGS_P_WAVE)
        (*val++).i = pod->wave;
      if (e->params & SGS_P_TIME) {
        (*val++).i = (pod->time_ms == SGS_TIME_INF) ?
          SGS_TIME_INF :
          MS_TO_SRT(pod->time_ms, srate);
      }
      if (e->params & SGS_P_SILENCE)
        (*val++).i = MS_TO_SRT(pod->silence_ms, srate);
      if (e->params & SGS_P_FREQ)
        (*val++).f = pod->freq;
      if (e->params & SGS_P_VALITFREQ) {
        (*val++).i = MS_TO_SRT(pod->valitfreq.time_ms, srate);
        (*val++).f = pod->valitfreq.goal;
        (*val++).i = pod->valitfreq.type;
      }
      if (e->params & SGS_P_DYNFREQ)
        (*val++).f = pod->dynfreq;
      if (e->params & SGS_P_PHASE)
        (*val++).i = SGS_Osc_PHASE(pod->phase);
      if (e->params & SGS_P_AMP)
        (*val++).f = pod->amp;
      if (e->params & SGS_P_VALITAMP) {
        (*val++).i = MS_TO_SRT(pod->valitamp.time_ms, srate);
        (*val++).f = pod->valitamp.goal;
        (*val++).i = pod->valitamp.type;
      }
      if (e->params & SGS_P_DYNAMP)
        (*val++).f = pod->dynamp;
    }
    if (prg_e->voice) {
      const SGS_ProgramVoData *pvd = prg_e->voice;
      e->voice_id = prg_e->voice_id;
      if (e->params & SGS_P_GRAPH)
        (*val++).v = (void*)pvd->graph;
      if (e->params & SGS_P_VOATTR)
        (*val++).i = pvd->attr;
      if (e->params & SGS_P_PANNING)
        (*val++).f = pvd->panning;
      if (e->params & SGS_P_VALITPANNING) {
        (*val++).i = MS_TO_SRT(pvd->valitpanning.time_ms, srate);
        (*val++).f = pvd->valitpanning.goal;
        (*val++).i = pvd->valitpanning.type;
      }
      o->voices[e->voice_id].pos = -indexwaittime;
      indexwaittime = 0;
    }
    event_values = val;
  }
  return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Generator(SGS_Generator *o) {
  if (o->bufs) free(o->bufs);
  if (o->events) free(o->events);
  if (o->voices) free(o->voices);
  if (o->operators) free(o->operators);
  if (o->event_values) free(o->event_values);
  free(o);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *o, EventNode *e) {
  if (1) /* more types to be added in the future */ {
    const EventValue *val = e->data;
    /*
     * Set state of operator and/or voice. Voice updates must be done last,
     * as operator updates may change node adjacents and buffer recalculation
     * is currently done during voice updates.
     */
    if (e->operator_id >= 0) {
      OperatorNode *on = &o->operators[e->operator_id];
      if (e->params & SGS_P_ADJCS)
        on->adjcs = (*val++).v;
      if (e->params & SGS_P_OPATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        if (!(e->params & SGS_P_FREQ)) {
          /* May change during processing; preserve state of FREQRATIO flag */
          attr &= ~SGS_ATTR_FREQRATIO;
          attr |= on->attr & SGS_ATTR_FREQRATIO;
        }
        on->attr = attr;
      }
      if (e->params & SGS_P_WAVE)
        on->wave = (*val++).i;
      if (e->params & SGS_P_TIME)
        on->time = (*val++).i;
      if (e->params & SGS_P_SILENCE)
        on->silence = (*val++).i;
      if (e->params & SGS_P_FREQ)
        on->freq = (*val++).f;
      if (e->params & SGS_P_VALITFREQ) {
        on->valitfreq.time = (*val++).i;
        on->valitfreq.pos = 0;
        on->valitfreq.goal = (*val++).f;
        on->valitfreq.type = (*val++).i;
      }
      if (e->params & SGS_P_DYNFREQ)
        on->dynfreq = (*val++).f;
      if (e->params & SGS_P_PHASE)
        SGS_Osc_SET_PHASE(&on->osc, (uint32_t)(*val++).i);
      if (e->params & SGS_P_AMP)
        on->amp = (*val++).f;
      if (e->params & SGS_P_VALITAMP) {
        on->valitamp.time = (*val++).i;
        on->valitamp.pos = 0;
        on->valitamp.goal = (*val++).f;
        on->valitamp.type = (*val++).i;
      }
      if (e->params & SGS_P_DYNAMP)
        on->dynamp = (*val++).f;
    }
    if (e->voice_id >= 0) {
      VoiceNode *vn = &o->voices[e->voice_id];
      if (e->params & SGS_P_GRAPH)
        vn->graph = (*val++).v;
      if (e->params & SGS_P_VOATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        vn->attr = attr;
      }
      if (e->params & SGS_P_PANNING)
        vn->panning = (*val++).f;
      if (e->params & SGS_P_VALITPANNING) {
        vn->valitpanning.time = (*val++).i;
        vn->valitpanning.pos = 0;
        vn->valitpanning.goal = (*val++).f;
        vn->valitpanning.type = (*val++).i;
      }
      upsize_bufs(o, vn);
      vn->flags |= VN_INIT | VN_EXEC;
      vn->pos = 0;
      if ((int32_t)o->voice > e->voice_id) /* go back to re-activated node */
        o->voice = e->voice_id;
    }
  }
}

/*
 * Fill buffer with buf_len float values for a parameter; these may
 * either simply be a copy of the supplied state, or modified.
 *
 * If a parameter valit (VALue ITeration) is supplied, the values
 * are shaped according to its timing, target value and curve
 * selection. Once elapsed, the state will also be set to its final
 * value.
 *
 * Passing a modifier buffer will accordingly multiply each output
 * value, done to get absolute values from ratios.
 */
static bool run_param(BufValue *buf, uint32_t buf_len, ParameterValit *vi,
                      float *state, const BufValue *modbuf) {
  uint32_t i, end, len, filllen;
  double coeff;
  float s0 = *state;
  if (!vi) {
    filllen = buf_len;
    goto FILL;
  }
  coeff = 1.f / vi->time;
  len = vi->time - vi->pos;
  if (len > buf_len) {
    len = buf_len;
    filllen = 0;
  } else {
    filllen = buf_len - len;
  }
  switch (vi->type) {
  case SGS_VALIT_LIN:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      (*buf++).f = s0 + (vi->goal - s0) * (i * coeff);
    }
    break;
  case SGS_VALIT_EXP:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = 1.f - i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = vi->goal + (s0 - vi->goal) * mod;
    }
    break;
  case SGS_VALIT_LOG:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = s0 + (vi->goal - s0) * mod;
    }
    break;
  }
  if (modbuf) {
    buf -= len;
    for (i = 0; i < len; ++i) {
      (*buf++).f *= (*modbuf++).f;
    }
  }
  vi->pos += len;
  if (vi->time == vi->pos) {
    s0 = *state = vi->goal; /* when reached, valit target becomes new state */
  FILL:
    /*
     * Set the remaining values, if any, using the state.
     */
    if (modbuf) {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0 * modbuf[i].f;
    } else {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0;
    }
    return (vi != 0);
  }
  return 0;
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
static uint32_t run_block(SGS_Generator *o, Buf *bufs, uint32_t buf_len,
                          OperatorNode *n, BufValue *parent_freq,
                          bool wave_env, uint32_t acc_ind) {
  uint32_t i, len;
  BufValue *sbuf, *freq, *freqmod, *pm, *amp;
  Buf *nextbuf = bufs + 1;
  ParameterValit *vi;
  uint32_t fmodc = 0, pmodc = 0, amodc = 0;
  if (n->adjcs) {
    fmodc = n->adjcs->fmodc;
    pmodc = n->adjcs->pmodc;
    amodc = n->adjcs->amodc;
  }
  sbuf = *bufs;
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
      sbuf[i].i = 0;
    len -= zero_len;
    if (n->time != SGS_TIME_INF) n->time -= zero_len;
    n->silence -= zero_len;
    if (!len) return zero_len;
    sbuf += zero_len;
  }
  /*
   * Guard against circular references.
   */
  if ((n->flags & ON_VISITED) != 0) {
      for (i = 0; i < len; ++i)
        sbuf[i].i = 0;
      return zero_len + len;
  }
  n->flags |= ON_VISITED;
  /*
   * Limit length to time duration of operator.
   */
  uint32_t skip_len = 0;
  if (n->time < (int32_t)len && n->time != SGS_TIME_INF) {
    skip_len = len - n->time;
    len = n->time;
  }
  /*
   * Handle frequency (alternatively ratio) parameter, including frequency
   * modulation if modulators linked.
   */
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_VALITFREQ) {
    vi = &n->valitfreq;
    if (n->attr & SGS_ATTR_VALITFREQRATIO) {
      freqmod = parent_freq;
      if (!(n->attr & SGS_ATTR_FREQRATIO)) {
        n->attr |= SGS_ATTR_FREQRATIO;
        n->freq /= parent_freq[0].f;
      }
    } else {
      freqmod = 0;
      if (n->attr & SGS_ATTR_FREQRATIO) {
        n->attr &= ~SGS_ATTR_FREQRATIO;
        n->freq *= parent_freq[0].f;
      }
    }
  } else {
    vi = 0;
    freqmod = (n->attr & SGS_ATTR_FREQRATIO) ? parent_freq : 0;
  }
  if (run_param(freq, len, vi, &n->freq, freqmod))
    n->attr &= ~(SGS_ATTR_VALITFREQ|SGS_ATTR_VALITFREQRATIO);
  if (fmodc) {
    const int32_t *fmods = n->adjcs->adjcs;
    BufValue *fmbuf;
    for (i = 0; i < fmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, true, i);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parent_freq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  /*
   * If phase modulators linked, get phase offsets for modulation.
   */
  pm = 0;
  if (pmodc) {
    const int32_t *pmods = &n->adjcs->adjcs[fmodc];
    for (i = 0; i < pmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, false, i);
    pm = *(nextbuf++);
  }
  /*
   * Handle amplitude parameter, including amplitude modulation if
   * modulators linked.
   */
  if (amodc) {
    const int32_t *amods = &n->adjcs->adjcs[fmodc+pmodc];
    float dynampdiff = n->dynamp - n->amp;
    for (i = 0; i < amodc; ++i)
      run_block(o, nextbuf, len, &o->operators[amods[i]], freq, true, i);
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp + amp[i].f * dynampdiff;
  } else {
    amp = *(nextbuf++);
    vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
    if (run_param(amp, len, vi, &n->amp, 0))
      n->attr &= ~SGS_ATTR_VALITAMP;
  }
  if (!wave_env) {
    /*
     * Generate integer output - either for voice output or phase modulation
     * input.
     */
    const int16_t *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      int32_t s, spm = 0;
      float sfreq = freq[i].f;
      float samp = amp[i].f;
      if (pm) spm = pm[i].i;
      SGS_Osc_RUN_S16(&n->osc, lut, o->osc_coeff, sfreq, spm, samp, s);
      if (acc_ind) s += sbuf[i].i;
      sbuf[i].i = s;
    }
  } else {
    /*
     * Generate float output - used as waveform envelopes for modulating
     * frequency or amplitude.
     */
    const int16_t *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      float s;
      float sfreq = freq[i].f;
      float samp = amp[i].f * 0.5f;
      int32_t spm = 0;
      if (pm) spm = pm[i].i;
      SGS_Osc_RUN_SF(&n->osc, lut, o->osc_coeff, sfreq, spm, s);
      s = (s * samp) + fabs(samp);
      if (acc_ind) s *= sbuf[i].f;
      sbuf[i].f = s;
    }
  }
  /*
   * Update time duration left, zero rest of buffer if unfilled.
   */
  if (n->time != SGS_TIME_INF) {
    if (!acc_ind && skip_len > 0) {
      sbuf += len;
      for (i = 0; i < skip_len; ++i)
        sbuf[i].i = 0;
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
static void mix_output(SGS_Generator *o, VoiceNode *vn, int16_t **st_out,
    uint32_t len) {
  BufValue *s_buf = o->bufs[0];
  float scale = o->amp_scale;
  if (vn->attr & SGS_ATTR_VALITPANNING) {
    BufValue *pan_buf = o->bufs[1];
    if (run_param(pan_buf, len, &vn->valitpanning,
        &vn->panning, 0)) {
      vn->attr &= ~SGS_ATTR_VALITPANNING;
    }
    for (uint32_t i = 0; i < len; ++i) {
      float s = s_buf[i].i * scale;
      float p = s * pan_buf[i].f;
      *(*st_out)++ += lrintf(s - p);
      *(*st_out)++ += lrintf(p);
    }
  } else {
    for (uint32_t i = 0; i < len; ++i) {
      float s = s_buf[i].i * scale;
      float p = s * vn->panning;
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
static uint32_t run_voice(SGS_Generator *o, VoiceNode *vn, int16_t *out,
                          uint32_t buf_len) {
  uint32_t ret_len = 0;
  bool finished = true;
  if (!vn->graph) goto RETURN;
  uint32_t opc = vn->graph->opc;
  const int32_t *ops = vn->graph->ops;
  int32_t time = 0;
  uint32_t i;
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    if (n->time == 0) continue;
    if (n->time > time && n->time != SGS_TIME_INF) time = n->time;
  }
  if (time > (int32_t)buf_len) time = buf_len;
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
      OperatorNode *n = &o->operators[ops[i]];
      if (n->time == 0) continue;
      last_len = run_block(o, o->bufs, len, n, 0, false, acc_ind++);
      if (last_len > gen_len) gen_len = last_len;
    }
    if (!gen_len) goto RETURN;
    mix_output(o, vn, &sp, gen_len);
    ret_len += gen_len;
  }
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    if (n->time != 0) {
      finished = false;
      break;
    }
  }
RETURN:
  vn->pos += ret_len;
  if (finished)
    vn->flags &= ~VN_EXEC;
  return ret_len;
}

/**
 * Main audio generation/processing function. Call repeatedly to fill
 * interleaved stereo buffer buf with up to len new samples, the
 * remainder (if any, which may occur at end of signal) zero-filled.
 *
 * If supplied, gen_len will be set to the precise length generated
 * for the call, which is buf_len unless the signal ended earlier.
 *
 * Return true as long as there are more samples to generate, false
 * when the end of the signal has been reached.
 */
bool SGS_Generator_run(SGS_Generator *o, int16_t *buf, size_t buf_len,
                       size_t *gen_len) {
  int16_t *sp = buf;
  uint32_t i, len = buf_len;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
  uint32_t skip_len, ret_len = 0;
PROCESS:
  skip_len = 0;
  while (o->event < o->event_count) {
    EventNode *e = &o->events[o->event];
    if (o->event_pos < e->waittime) {
      uint32_t waittime = e->waittime - o->event_pos;
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
  for (i = o->voice; i < o->voice_count; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint32_t waittime = -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->flags & VN_EXEC) {
      uint32_t voice_len = run_voice(o, vn, buf, len);
      if (voice_len > last_len) last_len = voice_len; 
    }
  }
  ret_len += last_len;
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
    if (o->voice == o->voice_count) {
      if (o->event != o->event_count) break;
      /*
       * The end.
       */
      if (gen_len) *gen_len = ret_len;
      return false;
    }
    vn = &o->voices[o->voice];
    if (!(vn->flags & VN_INIT) || vn->flags & VN_EXEC) break;
    ++o->voice;
  }
  /*
   * Further calls needed to complete signal.
   */
  if (gen_len) *gen_len = buf_len;
  return true;
}
