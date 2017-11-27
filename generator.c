/* sgensys: sound generator module.
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
#include <stdio.h>
#include <stdlib.h>

enum {
  SGS_FLAG_INIT = 1<<0,
  SGS_FLAG_EXEC = 1<<1
};

typedef union BufData {
  int32_t i;
  float f;
} BufData;

#define BUF_LEN 256
typedef BufData Buf[BUF_LEN];

typedef struct ParameterValit {
  uint32_t time, pos;
  float goal;
  uint8_t type;
} ParameterValit;

typedef struct OperatorNode {
  SGSOsc osc;
  int32_t time;
  uint32_t silence;
  uint8_t wave, attr;
  const SGSProgramGraphAdjcs *adjcs;
  float amp, dynamp;
  float freq, dynfreq;
  ParameterValit valitamp, valitfreq;
} OperatorNode;

typedef struct VoiceNode {
  int32_t pos; /* negative for wait time */
  uint8_t flag, attr;
  const SGSProgramGraph *graph;
  float panning;
  ParameterValit valitpanning;
} VoiceNode;

typedef union Data {
  int32_t i;
  float f;
  void *v;
} Data;

typedef struct EventNode {
  void *node;
  uint32_t waittime;
} EventNode;

typedef struct SetNode {
  int32_t voice_id, operator_id;
  uint32_t params;
  Data data[1]; /* sized for number of parameters set */
} SetNode;

#define GET_SET_NODE_SIZE(param_count) \
	(offsetof(SetNode, data) + (param_count) * sizeof(Data))

static uint32_t count_flags(uint32_t flags) {
  uint32_t i, count = 0;
  for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
    count += flags & 1;
    flags >>= 1;
  }
  return count;
}

struct SGSGenerator {
  double osc_coeff;
  uint32_t srate;
  uint32_t bufc;
  Buf *bufs;
  size_t event, eventc;
  uint32_t eventpos;
  EventNode *events;
  uint32_t voice, voicec;
  VoiceNode *voices;
  OperatorNode operators[1]; /* sized to number of nodes */
};

/*
 * Count buffers needed for operator, including linked operators.
 * TODO: Verify, remove debug printing when parser module done.
 */
static uint32_t calc_bufs(SGSGenerator *o, OperatorNode *n) {
  uint32_t count = 0, i, res;
  if (n->adjcs) {
    const int32_t *mods = n->adjcs->adjcs;
    const uint32_t modc = n->adjcs->fmodc + n->adjcs->pmodc + n->adjcs->amodc;
    for (i = 0; i < modc; ++i) {
  printf("visit node %d\n", mods[i]);
      res = calc_bufs(o, &o->operators[mods[i]]);
      if (res > count) count = res;
    }
  }
  return count + 5;
}

/*
 * Check operators for voice and increase the buffer allocation if needed.
 * TODO: Verify, remove debug printing when parser module done.
 */
static void upsize_bufs(SGSGenerator *o, VoiceNode *vn) {
  uint32_t count = 0, i, res;
  if (!vn->graph) return;
  for (i = 0; i < vn->graph->opc; ++i) {
  printf("visit node %d\n", vn->graph->ops[i]);
    res = calc_bufs(o, &o->operators[vn->graph->ops[i]]);
    if (res > count) count = res;
  }
  printf("need %d buffers (have %d)\n", count, o->bufc);
  if (count > o->bufc) {
    printf("new alloc size 0x%lu\n", sizeof(Buf) * count);
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

/**
 * Create SGSGenerator using the given program and sample rate.
 */
SGSGenerator* SGS_create_generator(SGSProgram *prg, uint32_t srate) {
  SGSGenerator *o;
  const SGSProgramEvent *step;
  void *data;
  size_t i;
  /*
   * Establish allocation sizes.
   */
  size_t size = sizeof(SGSGenerator) - sizeof(OperatorNode);
  size_t eventssize = sizeof(EventNode) * prg->eventc;
  size_t voicessize = sizeof(VoiceNode) * prg->voicec;
  size_t operatorssize = sizeof(OperatorNode) * prg->operatorc;
  size_t setssize = 0;
  for (i = 0; i < prg->eventc; ++i) {
    step = &prg->events[i];
    size_t setnode_size = GET_SET_NODE_SIZE(count_flags(step->params)
                            + count_flags(step->params & (SGS_VALITFREQ |
                                              SGS_VALITAMP |
                                              SGS_VALITPANNING))*2);
    setssize += setnode_size;
  }
  /*
   * Allocate & initialize.
   */
  o = calloc(1, size + operatorssize + eventssize + voicessize + setssize);
  if (!o) return NULL;
  o->srate = srate;
  o->osc_coeff = SGSOsc_SRATE_COEFF(srate);
  o->eventc = prg->eventc;
  o->events = (void*)(((uint8_t*)o) + size + operatorssize);
  o->voicec = prg->voicec;
  o->voices = (void*)(((uint8_t*)o) + size + operatorssize + eventssize);
  data      = (void*)(((uint8_t*)o) + size + operatorssize + eventssize + voicessize);
  SGSOsc_global_init();
  /*
   * Fill in events according to the SGSProgram, ie. copy timed state
   * changes for voices and operators.
   */
  uint32_t indexwaittime = 0;
  for (i = 0; i < prg->eventc; ++i) {
    EventNode *e;
    SetNode *s;
    Data *set;
    step = &prg->events[i];
    e = &o->events[i];
    s = data;
    set = s->data;
    e->node = s;
    e->waittime = ((float)step->wait_ms) * srate * .001f;
    indexwaittime += e->waittime;
    s->voice_id = -1;
    s->operator_id = -1;
    s->params = step->params;
    if (step->operator) {
      const SGSProgramOperatorData *od = step->operator;
      s->operator_id = od->operator_id;
      s->voice_id = step->voice_id;
      if (s->params & SGS_ADJCS)
        (*set++).v = (void*)od->adjcs;
      if (s->params & SGS_OPATTR)
        (*set++).i = od->attr;
      if (s->params & SGS_WAVE)
        (*set++).i = od->wave;
      if (s->params & SGS_TIME) {
        (*set++).i = (od->time_ms == SGS_TIME_INF) ?
                     SGS_TIME_INF :
                     (int32_t) ((float)od->time_ms) * srate * .001f;
      }
      if (s->params & SGS_SILENCE)
        (*set++).i = ((float)od->silence_ms) * srate * .001f;
      if (s->params & SGS_FREQ)
        (*set++).f = od->freq;
      if (s->params & SGS_VALITFREQ) {
        (*set++).i = ((float)od->valitfreq.time_ms) * srate * .001f;
        (*set++).f = od->valitfreq.goal;
        (*set++).i = od->valitfreq.type;
      }
      if (s->params & SGS_DYNFREQ)
        (*set++).f = od->dynfreq;
      if (s->params & SGS_PHASE)
        (*set++).i = SGSOsc_PHASE(od->phase);
      if (s->params & SGS_AMP)
        (*set++).f = od->amp;
      if (s->params & SGS_VALITAMP) {
        (*set++).i = ((float)od->valitamp.time_ms) * srate * .001f;
        (*set++).f = od->valitamp.goal;
        (*set++).i = od->valitamp.type;
      }
      if (s->params & SGS_DYNAMP)
        (*set++).f = od->dynamp;
    }
    if (step->voice) {
      const SGSProgramVoiceData *vd = step->voice;
      s->voice_id = step->voice_id;
      if (s->params & SGS_GRAPH)
        (*set++).v = (void*)vd->graph;
      if (s->params & SGS_VOATTR)
        (*set++).i = vd->attr;
      if (s->params & SGS_PANNING)
        (*set++).f = vd->panning;
      if (s->params & SGS_VALITPANNING) {
        (*set++).i = ((float)vd->valitpanning.time_ms) * srate * .001f;
        (*set++).f = vd->valitpanning.goal;
        (*set++).i = vd->valitpanning.type;
      }
      o->voices[s->voice_id].pos = -indexwaittime;
      indexwaittime = 0;
    }
    data = (void*)(((uint8_t*)data) +
                   (sizeof(SetNode) - sizeof(Data)) +
                   (((uint8_t*)set) - ((uint8_t*)s->data)));
  }
  return o;
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGSGenerator *o, EventNode *e) {
  if (1) /* more types to be added in the future */ {
    const SetNode *s = e->node;
    VoiceNode *vn;
    OperatorNode *on;
    const Data *data = s->data;
    /*
     * Set state of operator and/or voice. Voice updates must be done last,
     * as operator updates may change node adjacents and buffer recalculation
     * is currently done during voice updates.
     */
    if (s->operator_id >= 0) {
      on = &o->operators[s->operator_id];
      if (s->params & SGS_ADJCS)
        on->adjcs = (*data++).v;
      if (s->params & SGS_OPATTR) {
        uint8_t attr = (uint8_t)(*data++).i;
        if (!(s->params & SGS_FREQ)) {
          /* May change during processing; preserve state of FREQRATIO flag */
          attr &= ~SGS_ATTR_FREQRATIO;
          attr |= on->attr & SGS_ATTR_FREQRATIO;
        }
        on->attr = attr;
      }
      if (s->params & SGS_WAVE)
        on->wave = (*data++).i;
      if (s->params & SGS_TIME)
        on->time = (*data++).i;
      if (s->params & SGS_SILENCE)
        on->silence = (*data++).i;
      if (s->params & SGS_FREQ)
        on->freq = (*data++).f;
      if (s->params & SGS_VALITFREQ) {
        on->valitfreq.time = (*data++).i;
        on->valitfreq.pos = 0;
        on->valitfreq.goal = (*data++).f;
        on->valitfreq.type = (*data++).i;
      }
      if (s->params & SGS_DYNFREQ)
        on->dynfreq = (*data++).f;
      if (s->params & SGS_PHASE)
        SGSOsc_SET_PHASE(&on->osc, (uint32_t)(*data++).i);
      if (s->params & SGS_AMP)
        on->amp = (*data++).f;
      if (s->params & SGS_VALITAMP) {
        on->valitamp.time = (*data++).i;
        on->valitamp.pos = 0;
        on->valitamp.goal = (*data++).f;
        on->valitamp.type = (*data++).i;
      }
      if (s->params & SGS_DYNAMP)
        on->dynamp = (*data++).f;
    }
    if (s->voice_id >= 0) {
      vn = &o->voices[s->voice_id];
      if (s->params & SGS_GRAPH)
        vn->graph = (*data++).v;
      if (s->params & SGS_VOATTR) {
        uint8_t attr = (uint8_t)(*data++).i;
        vn->attr = attr;
      }
      if (s->params & SGS_PANNING)
        vn->panning = (*data++).f;
      if (s->params & SGS_VALITPANNING) {
        vn->valitpanning.time = (*data++).i;
        vn->valitpanning.pos = 0;
        vn->valitpanning.goal = (*data++).f;
        vn->valitpanning.type = (*data++).i;
      }
      upsize_bufs(o, vn);
      vn->flag |= SGS_FLAG_INIT | SGS_FLAG_EXEC;
      vn->pos = 0;
      if ((int32_t)o->voice > s->voice_id) /* go back to re-activated node */
        o->voice = s->voice_id;
    }
  }
}

/**
 * Destroy the SGSGenerator instance.
 */
void SGS_destroy_generator(SGSGenerator *o) {
  free(o->bufs);
  free(o);
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
static bool run_param(BufData *buf, uint32_t buf_len, ParameterValit *vi,
                      float *state, const BufData *modbuf) {
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
static uint32_t run_block(SGSGenerator *o, Buf *bufs, uint32_t buf_len,
                          OperatorNode *n, BufData *parent_freq,
                          uint8_t wave_env, uint32_t acc_ind) {
  uint32_t i, len;
  BufData *sbuf, *freq, *freqmod, *pm, *amp;
  Buf *nextbuf = bufs + 1;
  ParameterValit *vi;
  uint32_t fmodc, pmodc, amodc;
  fmodc = pmodc = amodc = 0;
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
    BufData *fmbuf;
    for (i = 0; i < fmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, 1, i);
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
      run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, 0, i);
    pm = *(nextbuf++);
  }
  if (!wave_env) {
    /*
     * Handle amplitude parameter, including amplitude modulation if
     * modulators linked.
     */
    if (amodc) {
      const int32_t *amods = &n->adjcs->adjcs[fmodc+pmodc];
      float dynampdiff = n->dynamp - n->amp;
      for (i = 0; i < amodc; ++i)
        run_block(o, nextbuf, len, &o->operators[amods[i]], freq, 1, i);
      amp = *(nextbuf++);
      for (i = 0; i < len; ++i)
        amp[i].f = n->amp + amp[i].f * dynampdiff;
    } else {
      amp = *(nextbuf++);
      vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
      if (run_param(amp, len, vi, &n->amp, 0))
        n->attr &= ~SGS_ATTR_VALITAMP;
    }
    /*
     * Generate integer output - either for voice output or phase modulation
     * input.
     */
    const int16_t *lut = SGSOsc_luts[n->wave];
    for (i = 0; i < len; ++i) {
      int32_t s, spm = 0;
      float sfreq = freq[i].f, samp = amp[i].f;
      if (pm) spm = pm[i].i;
      SGSOsc_RUN_S16(&n->osc, lut, o->osc_coeff, sfreq, spm, samp, s);
      if (acc_ind) s += sbuf[i].i;
      sbuf[i].i = s;
    }
  } else {
    /*
     * Generate float output - used as waveform envelopes for modulating
     * frequency or amplitude.
     */
    const int16_t *lut = SGSOsc_luts[n->wave];
    for (i = 0; i < len; ++i) {
      float s, sfreq = freq[i].f;
      int32_t spm = 0;
      if (pm) spm = pm[i].i;
      SGSOsc_RUN_SF(&n->osc, lut, o->osc_coeff, sfreq, spm, s);
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
  return zero_len + len;
}

/*
 * Generate up to buf_len samples for a voice, these mixed into the
 * interleaved output stereo buffer by simple addition.
 *
 * Returns number of samples generated for the voice.
 */
static uint32_t run_voice(SGSGenerator *o, VoiceNode *vn, int16_t *out,
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
      last_len = run_block(o, o->bufs, len, n, 0, 0, acc_ind++);
      if (last_len > gen_len) gen_len = last_len;
    }
    if (!gen_len) goto RETURN;
    if (vn->attr & SGS_ATTR_VALITPANNING) {
      BufData *buf = o->bufs[1];
      if (run_param(buf, gen_len, &vn->valitpanning, &vn->panning, 0))
        vn->attr &= ~SGS_ATTR_VALITPANNING;
      for (i = 0; i < gen_len; ++i) {
        int32_t s = (*o->bufs)[i].i;
        int32_t p = lrintf(s * buf[i].f);
        *sp++ += s - p;
        *sp++ += p;
      }
    } else {
      for (i = 0; i < gen_len; ++i) {
        int32_t s = (*o->bufs)[i].i;
        int32_t p = lrintf(s * vn->panning);
        *sp++ += s - p;
        *sp++ += p;
      }
    }
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
    vn->flag &= ~SGS_FLAG_EXEC;
  return ret_len;
}

/**
 * Main sound generation/processing function. Call repeatedly to fill
 * interleaved stereo buffer buf with up to len new samples, the
 * remainder (if any, which may occur at end of signal) zero-filled.
 *
 * If supplied, gen_len will be set to the precise length generated.
 *
 * Return true as long as there are more samples to generate, false
 * when the end of the signal has been reached.
 */
bool SGS_generator_run(SGSGenerator *o, int16_t *buf, size_t buf_len,
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
  while (o->event < o->eventc) {
    EventNode *e = &o->events[o->event];
    if (o->eventpos < e->waittime) {
      uint32_t waittime = e->waittime - o->eventpos;
      if (waittime < len) {
        /*
         * Limit len to waittime, further splitting processing into two
         * blocks; otherwise, voice processing could get ahead of event
         * handling in some cases - which would give undefined results!
         */
        skip_len = len - waittime;
        len = waittime;
      }
      o->eventpos += len;
      break;
    }
    handle_event(o, e);
    ++o->event;
    o->eventpos = 0;
  }
  uint32_t last_len = 0;
  for (i = o->voice; i < o->voicec; ++i) {
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
    if (vn->flag & SGS_FLAG_EXEC) {
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
    if (o->voice == o->voicec) {
      if (o->event != o->eventc) break;
      /*
       * The end.
       */
      if (gen_len) *gen_len = ret_len;
      return false;
    }
    vn = &o->voices[o->voice];
    if (!(vn->flag & SGS_FLAG_INIT) || vn->flag & SGS_FLAG_EXEC) break;
    ++o->voice;
  }
  /*
   * Further calls needed to complete signal.
   */
  if (gen_len) *gen_len = buf_len;
  return true;
}
