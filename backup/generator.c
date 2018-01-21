#include "sgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

enum {
  SGS_FLAG_INIT = 1<<0,
  SGS_FLAG_EXEC = 1<<1
};

typedef struct ParameterValit {
  uint time, pos;
  float goal;
  uchar type;
} ParameterValit;

typedef struct OperatorNode {
  uint time, silence;
  uchar type, attr;
  float freq, dynfreq;
  struct OperatorNode *fmodchain;
  struct OperatorNode *pmodchain;
  SGSOscLuv *osctype;
  SGSOsc osc;
  float amp, dynamp;
  struct OperatorNode *amodchain;
  struct OperatorNode *link;
  ParameterValit valitamp, valitfreq;
} OperatorNode;

typedef struct VoiceNode {
  int pos; /* negative for wait time */
  uchar flag;
  OperatorNode *o;
  float panning;
  ParameterValit valitpanning;
} VoiceNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct EventNode {
  void *node;
  uint waittime;
} EventNode;

typedef struct SetNode {
  uint setid;
  uint params;
  Data data[1]; /* sized for number of parameters set */
} SetNode;

static uint count_flags(uint flags) {
  uint i, count = 0;
  for (i = 0; i < (8 * sizeof(uint)); ++i) {
    if (flags & 1) ++count;
    flags >>= 1;
  }
  return count;
}

#define BUF_LEN 256
typedef Data Buf[BUF_LEN];

#define NO_DELAY_OFFS (0x80000000)
struct SGSGenerator {
  uint srate;
  Buf *bufs;
  uint bufc;
  double osc_coeff;
  uint event, eventc;
  uint eventpos;
  EventNode *events;
  uint voice, voicec;
  VoiceNode *voices;
  OperatorNode ops[1]; /* sized to total number of nodes */
  /* actual nodes of varying type stored here */
};

static int calc_bufs(OperatorNode *n, uchar waveenv) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs(n->fmodchain, 1);
  if (!waveenv) {
    ++count, --i;
    if (n->amodchain) {j = calc_bufs(n->amodchain, 1); if (i < j) i = j;}
  }
  if (n->pmodchain) {j = calc_bufs(n->pmodchain, 0); if (i < j) i = j;}
  if (!n->link) return (i > 0 ? count + i : count);
  n = n->link;
  ++count, --i; /* need separate accumulating buf */
  goto BEGIN;
} /* need separate multiplying buf */

static void upsize_bufs(SGSGenerator *o, OperatorNode *n) {
  uint count = calc_bufs(n, 0);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

SGSGenerator* SGSGenerator_create(uint srate, struct SGSProgram *prg) {
  SGSGenerator *o;
  SGSProgramEvent *step;
  void *data;
  uint i, voice, op, indexwaittime;
  uint size, eventssize, voicessize, opssize, setssize;
  size = sizeof(SGSGenerator) - sizeof(OperatorNode);
  eventssize = sizeof(EventNode) * prg->eventc;
  voicessize = sizeof(VoiceNode) * prg->topopc;
  opssize = sizeof(OperatorNode) * prg->operatorc;
  setssize = 0;
  for (step = prg->events; step; step = step->next) {
    setssize += sizeof(SetNode) +
                (sizeof(Data) *
                 (count_flags(step->params) +
                  count_flags(step->params & (SGS_VALITFREQ |
                                              SGS_VALITAMP |
                                              SGS_VALITPANNING))*2 - 1));
    if (step->optype == SGS_TYPE_NESTED)
      setssize += sizeof(Data) *
                  ((step->params &
                    (SGS_AMOD|SGS_FMOD|SGS_PMOD|SGS_LINK)) != 0);
  }
  o = calloc(1, size + opssize + eventssize + voicessize + setssize);
  o->srate = srate;
  o->osc_coeff = SGSOsc_COEFF(srate);
  o->event = 0;
  o->eventc = prg->eventc;
  o->eventpos = 0;
  o->events = (void*)(((uchar*)o) + size + opssize);
  o->voice = 0;
  o->voicec = prg->topopc;
  o->voices = (void*)(((uchar*)o) + size + opssize + eventssize);
  data      = (void*)(((uchar*)o) + size + opssize + eventssize + voicessize);
  SGSOsc_init();
  step = prg->events;
  voice = op = 0;
  indexwaittime = 0;
  for (i = 0; i < prg->eventc; ++i) {
    EventNode *e = &o->events[step->id];
    SetNode *s = data;
    Data *set = s->data;
    e->node = s;
    e->waittime = ((float)step->wait_ms) * srate * .001f;
    s->setid = step->opid;
    s->params = step->params;
    if (step->optype == SGS_TYPE_NESTED) {
      s->setid += prg->topopc;
      if (s->params & (SGS_AMOD|SGS_FMOD|SGS_PMOD|SGS_LINK))
        (*set++).i = step->topopid;
    }
    if (s->params & SGS_AMOD)
      (*set++).i = step->amodid >= 0 ? (int)(step->amodid + prg->topopc) : -1;
    if (s->params & SGS_FMOD)
      (*set++).i = step->fmodid >= 0 ? (int)(step->fmodid + prg->topopc) : -1;
    if (s->params & SGS_PMOD)
      (*set++).i = step->pmodid >= 0 ? (int)(step->pmodid + prg->topopc) : -1;
    if (s->params & SGS_LINK)
      (*set++).i = step->linkid >= 0 ? (int)(step->linkid + prg->topopc) : -1;
    if (s->params & SGS_ATTR)
      (*set++).i = step->attr;
    if (s->params & SGS_WAVE)
      (*set++).i = step->wave;
    if (s->params & SGS_TIME)
      (*set++).i = ((float)step->time_ms) * srate * .001f;
    if (s->params & SGS_SILENCE)
      (*set++).i = ((float)step->silence_ms) * srate * .001f;
    if (s->params & SGS_FREQ)
      (*set++).f = step->freq;
    if (s->params & SGS_VALITFREQ) {
      (*set++).i = ((float)step->valitfreq.time_ms) * srate * .001f;
      (*set++).f = step->valitfreq.goal;
      (*set++).i = step->valitfreq.type;
    }
    if (s->params & SGS_DYNFREQ)
      (*set++).f = step->dynfreq;
    if (s->params & SGS_PHASE)
      (*set++).i = SGSOsc_PHASE(step->phase);
    if (s->params & SGS_AMP)
      (*set++).f = step->amp;
    if (s->params & SGS_VALITAMP) {
      (*set++).i = ((float)step->valitamp.time_ms) * srate * .001f;
      (*set++).f = step->valitamp.goal;
      (*set++).i = step->valitamp.type;
    }
    if (s->params & SGS_DYNAMP)
      (*set++).f = step->dynamp;
    if (step->optype == SGS_TYPE_TOP) {
      if (s->params & SGS_PANNING)
        (*set++).f = step->topop.panning;
      if (s->params & SGS_VALITPANNING) {
        (*set++).i = ((float)step->topop.valitpanning.time_ms) * srate * .001f;
        (*set++).f = step->topop.valitpanning.goal;
        (*set++).i = step->topop.valitpanning.type;
      }
    }
    data = (void*)(((uchar*)data) +
                   (sizeof(SetNode) - sizeof(Data)) +
                   (((uchar*)set) - ((uchar*)s->data)));
    indexwaittime += e->waittime;
    if (!step->opprev) { /* new operator */
      OperatorNode *n = &o->ops[s->setid];
      n->type = step->optype;
      if (step->optype == SGS_TYPE_TOP) {
        VoiceNode *vn = &o->voices[s->setid];
        vn->o = n;
        vn->pos = -indexwaittime;
      }
      indexwaittime = 0;
    }
    step = step->next;
  }
  return o;
}

static void SGSGenerator_handle_event(SGSGenerator *o, EventNode *e) {
  if (1) {
    SetNode *s = e->node;
    OperatorNode *n = &o->ops[s->setid], *topn = 0;
    VoiceNode *vn = 0;
    Data *data = s->data;
    if (n->type == SGS_TYPE_TOP)
      vn = &o->voices[s->setid];
    if (s->params & (SGS_AMOD|SGS_FMOD|SGS_PMOD|SGS_LINK))
      topn = (vn ? n : &o->ops[(*data++).i]);
    /* set state */
    if (s->params & SGS_AMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        n->amodchain = &o->ops[id];
      } else
        n->amodchain = 0;
    }
    if (s->params & SGS_FMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        n->fmodchain = &o->ops[id];
      } else
        n->fmodchain = 0;
    }
    if (s->params & SGS_PMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        n->pmodchain = &o->ops[id];
      } else
        n->pmodchain = 0;
    }
    if (s->params & SGS_LINK) {
      int id = (*data++).i;
      if (id >= 0) {
        n->link = &o->ops[id];
      } else
        n->link = 0;
    }
    if (s->params & SGS_ATTR) {
      uchar attr = (uchar)(*data++).i;
      if (!(s->params & SGS_FREQ)) {
        /* May change during processing, so preserve state of FREQRATIO flag */
        attr &= ~SGS_ATTR_FREQRATIO;
        attr |= n->attr & SGS_ATTR_FREQRATIO;
      }
      n->attr = attr;
    }
    if (s->params & SGS_WAVE) switch ((*data++).i) {
    case SGS_WAVE_SIN:
      n->osctype = SGSOsc_sin;
      break;
    case SGS_WAVE_SRS:
      n->osctype = SGSOsc_srs;
      break;
    case SGS_WAVE_TRI:
      n->osctype = SGSOsc_tri;
      break;
    case SGS_WAVE_SQR:
      n->osctype = SGSOsc_sqr;
      break;
    case SGS_WAVE_SAW:
      n->osctype = SGSOsc_saw;
      break;
    }
    if (s->params & SGS_TIME) {
      n->time = (*data++).i;
      if (vn) {
        vn->pos = 0;
        if (!n->time)
          vn->flag &= ~SGS_FLAG_EXEC;
        else if (n->type == SGS_TYPE_TOP) {
          vn->flag |= SGS_FLAG_EXEC;
          if (o->voice > s->setid) /* go back to re-activated node */
            o->voice = s->setid;
        }
      }
    }
    if (s->params & SGS_SILENCE)
      n->silence = (*data++).i;
    if (s->params & SGS_FREQ)
      n->freq = (*data++).f;
    if (s->params & SGS_VALITFREQ) {
      n->valitfreq.time = (*data++).i;
      n->valitfreq.pos = 0;
      n->valitfreq.goal = (*data++).f;
      n->valitfreq.type = (*data++).i;
    }
    if (s->params & SGS_DYNFREQ)
      n->dynfreq = (*data++).f;
    if (s->params & SGS_PHASE)
      SGSOsc_SET_PHASE(&n->osc, (uint)(*data++).i);
    if (s->params & SGS_AMP)
      n->amp = (*data++).f;
    if (s->params & SGS_VALITAMP) {
      n->valitamp.time = (*data++).i;
      n->valitamp.pos = 0;
      n->valitamp.goal = (*data++).f;
      n->valitamp.type = (*data++).i;
    }
    if (s->params & SGS_DYNAMP)
      n->dynamp = (*data++).f;
    if (s->params & SGS_PANNING)
      vn->panning = (*data++).f;
    if (s->params & SGS_VALITPANNING) {
      vn->valitpanning.time = (*data++).i;
      vn->valitpanning.pos = 0;
      vn->valitpanning.goal = (*data++).f;
      vn->valitpanning.type = (*data++).i;
    }
    if (topn)
      upsize_bufs(o, topn);
    if (vn)
      vn->flag |= SGS_FLAG_INIT;
  }
}

void SGSGenerator_destroy(SGSGenerator *o) {
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static uchar run_param(Data *buf, uint buflen, ParameterValit *vi,
                       float *state, const Data *modbuf) {
  uint i, end, len, filllen;
  double coeff;
  float s0 = *state;
  if (!vi) {
    filllen = buflen;
    goto FILL;
  }
  coeff = 1.f / vi->time;
  len = vi->time - vi->pos;
  if (len > buflen) {
    len = buflen;
    filllen = 0;
  } else {
    filllen = buflen - len;
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
    s0 = *state = vi->goal;
  FILL:
    if (modbuf) {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0 * modbuf[i].f;
    } else for (i = 0; i < filllen; ++i)
      buf[i].f = s0;
    return (vi != 0);
  }
  return 0;
}

static void run_block(Buf *bufs, uint buflen, OperatorNode *n,
                      Data *parentfreq, double osc_coeff, uchar waveenv) {
  uchar acc = 0;
  uint i, len;
  Data *sbuf, *freq, *freqmod, *pm, *amp;
  Buf *nextbuf = bufs;
  ParameterValit *vi;
BEGIN:
  sbuf = *bufs;
  len = buflen;
  if (n->silence) {
    uint zerolen = n->silence;
    if (zerolen > len)
      zerolen = len;
    if (!acc) for (i = 0; i < zerolen; ++i)
      sbuf[i].i = 0;
    len -= zerolen;
    n->silence -= zerolen;
    if (!len)
      goto NEXT;
    sbuf += zerolen;
  }
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_VALITFREQ) {
    vi = &n->valitfreq;
    if (n->attr & SGS_ATTR_VALITFREQRATIO) {
      freqmod = parentfreq;
      if (!(n->attr & SGS_ATTR_FREQRATIO)) {
        n->attr |= SGS_ATTR_FREQRATIO;
        n->freq /= parentfreq[0].f;
      }
    } else {
      freqmod = 0;
      if (n->attr & SGS_ATTR_FREQRATIO) {
        n->attr &= ~SGS_ATTR_FREQRATIO;
        n->freq *= parentfreq[0].f;
      }
    }
  } else {
    vi = 0;
    freqmod = (n->attr & SGS_ATTR_FREQRATIO) ? parentfreq : 0;
  }
  if (run_param(freq, len, vi, &n->freq, freqmod))
    n->attr &= ~(SGS_ATTR_VALITFREQ|SGS_ATTR_VALITFREQRATIO);
  if (n->fmodchain) {
    Data *fmbuf;
    run_block(nextbuf, len, n->fmodchain, freq, osc_coeff, 1);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  pm = 0;
  if (n->pmodchain) {
    run_block(nextbuf, len, n->pmodchain, freq, osc_coeff, 0);
    pm = *(nextbuf++);
  }
  if (!waveenv) {
    if (n->amodchain) {
      float dynampdiff = n->dynamp - n->amp;
      run_block(nextbuf, len, n->amodchain, freq, osc_coeff, 1);
      amp = *(nextbuf++);
      for (i = 0; i < len; ++i)
        amp[i].f = n->amp + amp[i].f * dynampdiff;
    } else {
      amp = *(nextbuf++);
      vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
      if (run_param(amp, len, vi, &n->amp, 0))
        n->attr &= ~SGS_ATTR_VALITAMP;
    }
    for (i = 0; i < len; ++i) {
      int s, spm = 0;
      float sfreq = freq[i].f, samp = amp[i].f;
      if (pm)
        spm = pm[i].i;
      SGSOsc_RUN_PM(&n->osc, n->osctype, osc_coeff, sfreq, spm, samp, s);
      if (acc)
        s += sbuf[i].i;
      sbuf[i].i = s;
    }
  } else {
    for (i = 0; i < len; ++i) {
      float s, sfreq = freq[i].f;
      int spm = 0;
      if (pm)
        spm = pm[i].i;
      SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, osc_coeff, sfreq, spm, s);
      if (acc)
        s *= sbuf[i].f;
      sbuf[i].f = s;
    }
  }
NEXT:
  if (!n->link) return;
  acc = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static uint run_voice(SGSGenerator *o, VoiceNode *vn, short *sp, uint len) {
  double osc_coeff = o->osc_coeff;
  OperatorNode *n = vn->o;
  uint i, ret, time = n->time - vn->pos;
  if (time > len)
    time = len;
  ret = time;
  if (n->silence) {
    if (n->silence >= time) {
      n->silence -= time;
      goto RETURN;
    }
    sp += n->silence + n->silence; /* doubled given stereo interleaving */
    time -= n->silence;
    n->silence = 0;
  }
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0, osc_coeff, 0);
    if (n->attr & SGS_ATTR_VALITPANNING) {
      Data *buf = o->bufs[1];
      if (run_param(buf, len, &vn->valitpanning, &vn->panning, 0))
        n->attr &= ~SGS_ATTR_VALITPANNING;
      for (i = 0; i < len; ++i, sp += 2) {
        int s = (*o->bufs)[i].i, p;
        SET_I2F(p, ((float)s) * buf[i].f);
        sp[0] += s - p;
        sp[1] += p;
      }
    } else for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i, p;
      SET_I2F(p, ((float)s) * vn->panning);
      sp[0] += s - p;
      sp[1] += p;
    }
  } while (time);
RETURN:
  vn->pos += ret;
  if ((uint)vn->pos == n->time)
    vn->flag &= ~SGS_FLAG_EXEC;
  return ret;
}

/*
 * main run-function
 */

uchar SGSGenerator_run(SGSGenerator *o, short *buf, uint len) {
  short *sp;
  uint i, skiplen;
  sp = buf;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
PROCESS:
  skiplen = 0;
  while (o->event < o->eventc) {
    EventNode *e = &o->events[o->event];
    if (o->eventpos < e->waittime) {
      uint waittime = e->waittime - o->eventpos;
      if (waittime < len) {
        /* Split processing so that len is no longer than waittime, ensuring
         * event is handled before its operator is used.
         */
        skiplen = len - waittime;
        len = waittime;
      }
      o->eventpos += len;
      break;
    }
    SGSGenerator_handle_event(o, e);
    ++o->event;
    o->eventpos = 0;
  }
  for (i = o->voice; i < o->voicec; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint waittime = -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->flag & SGS_FLAG_EXEC)
      run_voice(o, vn, buf, len);
  }
  if (skiplen) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    VoiceNode *vn;
    if (o->voice == o->voicec)
      return (o->event != o->eventc);
    vn = &o->voices[o->voice];
    if (!(vn->flag & SGS_FLAG_INIT) || vn->flag & SGS_FLAG_EXEC)
      break;
    ++o->voice;
  }
  return 1;
}
