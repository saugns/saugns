#include "sgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uchar type, flag;
  int ref; /* for set nodes, id of node set; for nested, id of parent */
} IndexNode;

typedef struct SoundNode {
  uint time;
  uchar type, attr, mode;
  float freq, dynfreq;
  struct SoundNode *fmodchain;
  struct SoundNode *pmodchain;
  short *osctype;
  SGSOsc osc;
  float amp, dynampdiff;
  struct SoundNode *amodchain;
  struct SoundNode *link;
} SoundNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct SetNode {
  uchar values, mods;
  Data data[1]; /* sized for number of things set */
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
  int delay_offs;
  uint node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
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

static void upsize_bufs(SGSGenerator *o, SoundNode *n) {
  uint count = calc_bufs(n);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

SGSGenerator* SGSGenerator_create(uint srate, struct SGSProgram *prg) {
  SGSGenerator *o;
  SGSProgramNode *step;
  void *data;
  uint i, size, indexsize, nodessize;
  size = sizeof(SGSGenerator) - sizeof(IndexNode);
  indexsize = sizeof(IndexNode) * prg->nodec;
  nodessize = 0;
  for (step = prg->nodelist; step; step = step->next) {
    if (step->type == SGS_TYPE_TOP ||
        step->type == SGS_TYPE_NESTED)
      nodessize += sizeof(SoundNode);
    else if (step->type == SGS_TYPE_SETTOP ||
             step->type == SGS_TYPE_SETNESTED)
      nodessize += sizeof(SetNode) +
                   (sizeof(Data) *
                    (count_flags((step->spec.set.values << 8) |
                                 step->spec.set.mods) - 1));
  }
  o = calloc(1, size + indexsize + nodessize);
  o->srate = srate;
  o->osc_coeff = SGSOsc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->topc; /* only loop top-level nodes */
  data = (void*)(((uchar*)o) + size + indexsize);
  SGSOsc_init();
  step = prg->nodelist;
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    uint delay = step->delay * srate;
    in->node = data;
    in->pos = -delay;
    in->type = step->type;
    in->flag = step->flag;
    if (step->type == SGS_TYPE_TOP ||
        step->type == SGS_TYPE_NESTED) {
      SoundNode *n = data;
      uint time = step->time * srate;
      in->ref = -1;
      n->time = time;
      switch (step->wave) {
      case SGS_WAVE_SIN:
        n->osctype = SGSOsc_sin;
        break;
      case SGS_WAVE_SQR:
        n->osctype = SGSOsc_sqr;
        break;
      case SGS_WAVE_TRI:
        n->osctype = SGSOsc_tri;
        break;
      case SGS_WAVE_SAW:
        n->osctype = SGSOsc_saw;
        break;
      }
      n->attr = step->attr;
      n->mode = step->mode;
      n->amp = step->amp;
      n->dynampdiff = step->dynamp - step->amp;
      n->freq = step->freq;
      n->dynfreq = step->dynfreq;
      SGSOsc_SET_PHASE(&n->osc, SGSOsc_PHASE(step->phase));
      /* mods init part one - replaced with proper entries next loop */
      n->amodchain = (void*)step->amod.chain;
      n->fmodchain = (void*)step->fmod.chain;
      n->pmodchain = (void*)step->pmod.chain;
      n->link = (void*)step->spec.nested.link;
      data = (void*)(((uchar*)data) + sizeof(SoundNode));
    } else if (step->type == SGS_TYPE_SETTOP ||
               step->type == SGS_TYPE_SETNESTED) {
      SetNode *n = data;
      Data *set = n->data;
      SGSProgramNode *ref = step->spec.set.ref;
      in->ref = ref->id;
      if (ref->type == SGS_TYPE_NESTED)
        in->ref += prg->topc;
      n->values = step->spec.set.values;
      n->values &= ~SGS_DYNAMP;
      n->mods = step->spec.set.mods;
      if (n->values & SGS_TIME) {
        (*set).i = step->time * srate; ++set;
      }
      if (n->values & SGS_FREQ) {
        (*set).f = step->freq; ++set;
      }
      if (n->values & SGS_DYNFREQ) {
        (*set).f = step->dynfreq; ++set;
      }
      if (n->values & SGS_PHASE) {
        (*set).i = SGSOsc_PHASE(step->phase); ++set;
      }
      if (n->values & SGS_AMP) {
        (*set).f = step->amp; ++set;
      }
      if ((step->dynamp - step->amp) != (ref->dynamp - ref->amp)) {
        (*set).f = (step->dynamp - step->amp); ++set;
        n->values |= SGS_DYNAMP;
      }
      if (n->values & SGS_ATTR) {
        (*set).i = step->attr; ++set;
      }
      if (n->mods & SGS_AMODS) {
        (*set).i = step->amod.chain->id + prg->topc;
        ++set;
      }
      if (n->mods & SGS_FMODS) {
        (*set).i = step->fmod.chain->id + prg->topc;
        ++set;
      }
      if (n->mods & SGS_PMODS) {
        (*set).i = step->pmod.chain->id + prg->topc;
        ++set;
      }
      data = (void*)(((uchar*)data) +
                     sizeof(SetNode) +
                     (sizeof(Data) *
                      (count_flags((step->spec.set.values << 8) |
                                   step->spec.set.mods) - 1)));
    }
    step = step->next;
  }
  /* mods init part two - give proper entries */
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->type == SGS_TYPE_TOP ||
        in->type == SGS_TYPE_NESTED) {
      SoundNode *n = in->node;
      if (n->amodchain) {
        uint id = ((SGSProgramNode*)n->amodchain)->id + prg->topc;
        n->amodchain = o->nodes[id].node;
        o->nodes[id].ref = i;
      }
      if (n->fmodchain) {
        uint id = ((SGSProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->nodes[id].node;
        o->nodes[id].ref = i;
      }
      if (n->pmodchain) {
        uint id = ((SGSProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->nodes[id].node;
        o->nodes[id].ref = i;
      }
      if (n->link) {
        uint id = ((SGSProgramNode*)n->link)->id + prg->topc;
        n->link = o->nodes[id].node;
      }
    }
  }
  return o;
}

static void adjust_time(SGSGenerator *o, SoundNode *n) {
  int pos_offs;
  /* click reduction: increase time to make it end at wave cycle's end */
  SGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->freq, n->time, pos_offs);
  n->time -= pos_offs;
  if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
    o->delay_offs = pos_offs;
}

static void SGSGenerator_enter_node(SGSGenerator *o, IndexNode *in) {
  switch (in->type) {
  case SGS_TYPE_TOP:
    upsize_bufs(o, in->node);
    adjust_time(o, in->node);
  case SGS_TYPE_NESTED:
    break;
  case SGS_TYPE_SETTOP:
  case SGS_TYPE_SETNESTED: {
    IndexNode *refin = &o->nodes[in->ref];
    SoundNode *refn = refin->node;
    SetNode *setn = in->node;
    Data *data = setn->data;
    uchar adjtime = 0;
    /* set state */
    if (setn->values & SGS_TIME) {
      refn->time = (*data).i; ++data;
      refin->pos = 0;
      if (refn->time) {
        if (refin->type == SGS_TYPE_TOP)
          refin->flag |= SGS_FLAG_EXEC;
        adjtime = 1;
      } else
        refin->flag &= ~SGS_FLAG_EXEC;
    }
    if (setn->values & SGS_FREQ) {
      refn->freq = (*data).f; ++data;
      adjtime = 1;
    }
    if (setn->values & SGS_DYNFREQ) {
      refn->dynfreq = (*data).f; ++data;
    }
    if (setn->values & SGS_PHASE) {
      SGSOsc_SET_PHASE(&refn->osc, (uint)(*data).i); ++data;
    }
    if (setn->values & SGS_AMP) {
      refn->amp = (*data).f; ++data;
    }
    if (setn->values & SGS_DYNAMP) {
      refn->dynampdiff = (*data).f; ++data;
    }
    if (setn->values & SGS_ATTR) {
      refn->attr = (uchar)(*data).i; ++data;
    }
    if (setn->mods & SGS_AMODS) {
      refn->amodchain = o->nodes[(*data).i].node; ++data;
    }
    if (setn->mods & SGS_FMODS) {
      refn->fmodchain = o->nodes[(*data).i].node; ++data;
    }
    if (setn->mods & SGS_PMODS) {
      refn->pmodchain = o->nodes[(*data).i].node; ++data;
    }
    if (refn->type == SGS_TYPE_TOP) {
      upsize_bufs(o, refn);
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, refn);
    } else {
      IndexNode *topin = refin;
      while (topin->ref > -1)
        topin = &o->nodes[topin->ref];
      upsize_bufs(o, topin->node);
    }
    /* take over place of ref'd node */
    *in = *refin;
    refin->flag &= ~SGS_FLAG_EXEC;
    break; }
  case SGS_TYPE_ENV:
    break;
  }
  in->flag |= SGS_FLAG_ENTERED;
}

void SGSGenerator_destroy(SGSGenerator *o) {
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static void run_block_waveenv(Buf *bufs, uint len, SoundNode *n,
                              Data *parentfreq, double osc_coeff);

static void run_block(Buf *bufs, uint len, SoundNode *n,
                      Data *parentfreq, double osc_coeff) {
  uchar acc = 0;
  uint i;
  Data *sbuf = *bufs, *freq, *amp, *pm;
  Buf *nextbuf = bufs;
BEGIN:
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
    run_block_waveenv(nextbuf, len, n->amodchain, freq, osc_coeff);
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp + amp[i].f * n->dynampdiff;
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
  if (!n->link) return;
  acc = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static void run_block_waveenv(Buf *bufs, uint len, SoundNode *n,
                              Data *parentfreq, double osc_coeff) {
  uchar mul = 0;
  uint i;
  Data *sbuf = *bufs, *freq, *pm;
  Buf *nextbuf = bufs;
BEGIN:
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
  if (!n->link) return;
  mul = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate multiplying buf */
  goto BEGIN;
}

static uint run_node(SGSGenerator *o, SoundNode *n, short *sp, uint pos, uint len) {
  double osc_coeff = o->osc_coeff;
  uint i, ret, time = n->time - pos;
  if (time > len)
    time = len;
  ret = time;
  if (n->mode == SGS_MODE_RIGHT) ++sp;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0, osc_coeff);
    for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i;
      sp[0] += s;
      if (n->mode == SGS_MODE_CENTER)
        sp[1] += s;
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
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS)
        delay -= o->delay_offs; /* delay inc == previous time inc */
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
    if (!(in->flag & SGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      SGSGenerator_enter_node(o, in);
  }
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        in->pos += o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (delay >= len) {
        in->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      in->pos = 0;
    } else
    if (!(in->flag & SGS_FLAG_ENTERED))
      SGSGenerator_enter_node(o, in);
    if (in->flag & SGS_FLAG_EXEC) {
      SoundNode *n = in->node;
      in->pos += run_node(o, n, buf, in->pos, len);
      if ((uint)in->pos == n->time)
        in->flag &= ~SGS_FLAG_EXEC;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->nodec)
      return 0;
    if (o->nodes[o->node].flag & SGS_FLAG_EXEC)
      break;
    ++o->node;
  }
  return 1;
}
