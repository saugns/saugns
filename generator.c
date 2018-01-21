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

typedef struct IndexNode {
  void *node;
  int pos; /* negative for waittime/time shift */
  uchar type, flag;
} IndexNode;

typedef struct OperatorNode {
  uint time, silence;
  uchar type, attr;
  float freq, dynfreq;
  struct OperatorNode *fmodchain;
  struct OperatorNode *pmodchain;
  short *osctype;
  SGSOsc osc;
  float amp, dynamp;
  struct OperatorNode *amodchain;
  struct OperatorNode *link;
  union {
    float panning;
    uint parentid;
  } spec;
} OperatorNode;

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
  ushort params;
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
  uint node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
};

static int calc_bufs_waveenv(OperatorNode *n);

static int calc_bufs(OperatorNode *n) {
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

static int calc_bufs_waveenv(OperatorNode *n) {
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

static void upsize_bufs(SGSGenerator *o, OperatorNode *n) {
  uint count = calc_bufs(n);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

SGSGenerator* SGSGenerator_create(uint srate, struct SGSProgram *prg) {
  SGSGenerator *o;
  SGSProgramEvent *step;
  void *data;
  uint i, indexwaittime;
  uint size, indexsize, nodessize, eventssize;
  size = sizeof(SGSGenerator) - sizeof(IndexNode);
  eventssize = sizeof(EventNode) * prg->eventc;
  indexsize = nodessize = 0;
  for (step = prg->events; step; step = step->next) {
    nodessize += sizeof(SetNode) +
                   (sizeof(Data) *
                    (count_flags(step->params) - 1));
    if (!step->opprev) { /* new operator */
      indexsize += sizeof(IndexNode);
      nodessize += sizeof(OperatorNode);
    }
  }
  o = calloc(1, size + indexsize + nodessize + eventssize);
  o->srate = srate;
  o->osc_coeff = SGSOsc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->topopc; /* only loop top-level nodes */
  o->event = 0;
  o->eventc = prg->eventc;
  o->eventpos = 0;
  o->events = (void*)(((uchar*)o) + size + indexsize);
  data = (void*)(((uchar*)o->events) + eventssize);
  SGSOsc_init();
  step = prg->events;
  indexwaittime = 0;
  for (i = 0; i < prg->eventc; ++i) {
    EventNode *e = &o->events[i];
    SetNode *s = data;
    Data *set = s->data;
    e->node = s;
    e->waittime = step->wait_ms * srate * .001f;
    s->setid = step->opid;
    if (step->optype == SGS_TYPE_NESTED)
      s->setid += prg->topopc;
    s->params = step->params;
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
      (*set++).i = step->time_ms * srate * .001f;
    if (s->params & SGS_SILENCE)
      (*set++).i = step->silence_ms * srate * .001f;
    if (s->params & SGS_FREQ)
      (*set++).f = step->freq;
    if (s->params & SGS_DYNFREQ)
      (*set++).f = step->dynfreq;
    if (s->params & SGS_PHASE)
      (*set++).i = SGSOsc_PHASE(step->phase);
    if (s->params & SGS_AMP)
      (*set++).f = step->amp;
    if (s->params & SGS_DYNAMP)
      (*set++).f = step->dynamp;
    if (s->params & SGS_PANNING)
      (*set++).i = step->panning;
    data = (void*)(((uchar*)data) +
                   sizeof(SetNode) +
                   (sizeof(Data) *
                    (count_flags(step->params) - 1)));
    indexwaittime += e->waittime;
    if (!step->opprev) { /* new operator */
      IndexNode *in = &o->nodes[s->setid];
      in->node = data;
      in->pos = -indexwaittime;
      in->type = step->optype;
      indexwaittime = 0;
      data = (void*)(((uchar*)data) + sizeof(OperatorNode));
    }
    step = step->next;
  }
  return o;
}

static void SGSGenerator_handle_event(SGSGenerator *o, EventNode *e) {
  if (1) {
    SetNode *s = e->node;
    IndexNode *refin = &o->nodes[s->setid];
    OperatorNode *refn = refin->node;
    Data *data = s->data;
    /* set state */
    if (s->params & SGS_AMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        refn->amodchain = o->nodes[id].node;
        refn->amodchain->spec.parentid = s->setid;
      } else
        refn->amodchain = 0;
    }
    if (s->params & SGS_FMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        refn->fmodchain = o->nodes[id].node;
        refn->fmodchain->spec.parentid = s->setid;
      } else
        refn->fmodchain = 0;
    }
    if (s->params & SGS_PMOD) {
      int id = (*data++).i;
      if (id >= 0) {
        refn->pmodchain = o->nodes[id].node;
        refn->pmodchain->spec.parentid = s->setid;
      } else
        refn->pmodchain = 0;
    }
    if (s->params & SGS_LINK) {
      int id = (*data++).i;
      if (id >= 0) {
        refn->link = o->nodes[id].node;
        refn->link->spec.parentid = s->setid;
      } else
        refn->link = 0;
    }
    if (s->params & SGS_ATTR)
      refn->attr = (uchar)(*data++).i;
    if (s->params & SGS_WAVE) switch ((*data++).i) {
    case SGS_WAVE_SIN:
      refn->osctype = SGSOsc_sin;
      break;
    case SGS_WAVE_SQR:
      refn->osctype = SGSOsc_sqr;
      break;
    case SGS_WAVE_TRI:
      refn->osctype = SGSOsc_tri;
      break;
    case SGS_WAVE_SAW:
      refn->osctype = SGSOsc_saw;
      break;
    }
    if (s->params & SGS_TIME) {
      refn->time = (*data++).i;
      refin->pos = 0;
      if (!refn->time)
        refin->flag &= ~SGS_FLAG_EXEC;
      else if (refin->type == SGS_TYPE_TOP) {
        refin->flag |= SGS_FLAG_EXEC;
        if (o->node > s->setid) /* go back to re-activated node */
          o->node = s->setid;
      }
    }
    if (s->params & SGS_SILENCE)
      refn->silence = (*data++).i;
    if (s->params & SGS_FREQ)
      refn->freq = (*data++).f;
    if (s->params & SGS_DYNFREQ)
      refn->dynfreq = (*data++).f;
    if (s->params & SGS_PHASE)
      SGSOsc_SET_PHASE(&refn->osc, (uint)(*data++).i);
    if (s->params & SGS_AMP)
      refn->amp = (*data++).f;
    if (s->params & SGS_DYNAMP)
      refn->dynamp = (*data++).f;
    if (s->params & SGS_PANNING)
      refn->spec.panning = (*data++).f;
    if (refn->type == SGS_TYPE_TOP)
      upsize_bufs(o, refn);
    else {
      IndexNode *topin = refin;
      while (topin->type == SGS_TYPE_NESTED)
        topin = &o->nodes[((OperatorNode*)topin->node)->spec.parentid];
      upsize_bufs(o, topin->node);
    }
    refin->flag |= SGS_FLAG_INIT;
  }
}

void SGSGenerator_destroy(SGSGenerator *o) {
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static void run_block_waveenv(Buf *bufs, uint buflen, OperatorNode *n,
                              Data *parentfreq, double osc_coeff);

static void run_block(Buf *bufs, uint buflen, OperatorNode *n,
                      Data *parentfreq, double osc_coeff) {
  uchar acc = 0;
  uint i, len;
  Data *sbuf, *freq, *amp, *pm;
  Buf *nextbuf = bufs;
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
  if (n->attr & SGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq, osc_coeff);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  if (n->amodchain) {
    float dynampdiff = n->dynamp - n->amp;
    run_block_waveenv(nextbuf, len, n->amodchain, freq, osc_coeff);
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
    run_block(nextbuf, len, n->pmodchain, freq, osc_coeff);
    pm = *(nextbuf++);
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
NEXT:
  if (!n->link) return;
  acc = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static void run_block_waveenv(Buf *bufs, uint buflen, OperatorNode *n,
                              Data *parentfreq, double osc_coeff) {
  uchar mul = 0;
  uint i, len;
  Data *sbuf, *freq, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  sbuf = *bufs;
  len = buflen;
  if (n->silence) {
    uint zerolen = n->silence;
    if (zerolen > len)
      zerolen = len;
    if (!mul) for (i = 0; i < zerolen; ++i)
      sbuf[i].i = 0;
    len -= zerolen;
    n->silence -= zerolen;
    if (!len)
      goto NEXT;
    sbuf += zerolen;
  }
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq, osc_coeff);
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
    run_block(nextbuf, len, n->pmodchain, freq, osc_coeff);
    pm = *(nextbuf++);
  }
  for (i = 0; i < len; ++i) {
    float s, sfreq = freq[i].f;
    int spm = 0;
    if (pm)
      spm = pm[i].i;
    SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, osc_coeff, sfreq, spm, s);
    if (mul)
      s *= sbuf[i].f;
    sbuf[i].f = s;
  }
NEXT:
  if (!n->link) return;
  mul = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate multiplying buf */
  goto BEGIN;
}

static uint run_node(SGSGenerator *o, OperatorNode *n, short *sp, uint pos, uint len) {
  double osc_coeff = o->osc_coeff;
  uint i, ret, time = n->time - pos;
  if (time > len)
    time = len;
  if (n->silence) {
    if (n->silence >= time) {
      n->silence -= time;
      return time;
    }
    sp += n->silence + n->silence; /* doubled given stereo interleaving */
    time -= n->silence;
    n->silence = 0;
  }
  ret = time;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0, osc_coeff);
    for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i, p;
      SET_I2F(p, ((float)s) * n->spec.panning);
      sp[0] += s - p;
      sp[1] += p;
    }
  } while (time);
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
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint waittime = -in->pos;
      if (waittime >= len) {
        in->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      in->pos = 0;
    }
    if (in->flag & SGS_FLAG_EXEC) {
      OperatorNode *n = in->node;
      in->pos += run_node(o, n, buf, in->pos, len);
      if ((uint)in->pos == n->time)
        in->flag &= ~SGS_FLAG_EXEC;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    IndexNode *in;
    if (o->node == o->nodec)
      return (o->event != o->eventc);
    in = &o->nodes[o->node];
    if (!(in->flag & SGS_FLAG_INIT) || in->flag & SGS_FLAG_EXEC)
      break;
    ++o->node;
  }
  return 1;
}
