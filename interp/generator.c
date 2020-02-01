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
  MGS_INDN_PREP = 1<<0,
  MGS_INDN_EXEC = 1<<1,
};

/*
 * Data for timed updates to and running of sound nodes.
 */
typedef struct IndexNode {
  int pos; /* negative for delay/time shift */
  uint8_t status, type;
  void *node;
  struct IndexNode *ref_prev;
} IndexNode;

typedef struct SoundNode {
  uint32_t time;
  uint32_t root_i;
  uint8_t attr, mode;
  float freq, dynfreq;
  struct SoundNode *fmodchain;
  struct SoundNode *pmodchain;
  MGS_Osc osc;
  float amp, dynamp;
  struct SoundNode *amodchain;
  struct SoundNode *link;
  //IndexNode *cur_indn;
} SoundNode;

typedef struct VoiceNode {
  SoundNode *root_sndn;
} VoiceNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct UpdateNode {
  uint32_t params;
  SoundNode *sndn;
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
  float osc_coeff;
  int delay_offs;
  int time_flags;
  uint32_t indn_i, indn_end;
  IndexNode *index_nodes;
  SoundNode *sound_nodes;
  VoiceNode *voice_nodes;
  UpdateNode *update_nodes;
  size_t sndn_count, vocn_count, updn_count;
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

static void MGS_print_state(MGS_Generator *o, IndexNode *indn) {
  uint32_t id = indn - o->index_nodes;
  char t = 'O';
  printf(
"[%d]:\tpos %d\n"
"%c\n", id, indn->pos, t);
}

static void init_for_nodelist(MGS_Generator *o, MGS_ProgramNode *node_list) {
  uint32_t srate = o->srate;
  Data *node_data = o->node_data;
  size_t i = 0;
  for (MGS_ProgramNode *step = node_list; step != NULL; step = step->next) {
    uint32_t delay = step->delay * srate;
    IndexNode *indn_ref_prev = NULL;
    uint32_t sndn_id = step->type_id;
    SoundNode *sndn = &o->sound_nodes[sndn_id];
    if (!step->ref_prev) {
      uint32_t time = step->time * srate;
      sndn->time = time;
      sndn->root_i = step->root_id;
      sndn->mode = step->mode; // not included in params
      sndn->osc.coeff = o->osc_coeff;
      if (step->first_id == step->root_id) {
        uint32_t vocn_id = o->vocn_count++;
        VoiceNode *vocn = &o->voice_nodes[vocn_id];
        vocn->root_sndn = sndn;
      }
      /* mods init part one - replaced with proper entries next loop */
      sndn->link = (void*)step->nested_next;
    } else {
      indn_ref_prev = &o->index_nodes[step->ref_prev->id];
    }
    UpdateNode *updn = &o->update_nodes[o->updn_count++];
    Data *set = node_data;
    updn->params = step->params;
    updn->sndn = sndn;
    updn->data = set;
    if (updn->params & MGS_TIME) {
      (*set++).i = step->time * srate;
    }
    if (updn->params & MGS_WAVE) {
      (*set++).i = step->wave;
    }
    if (updn->params & MGS_FREQ) {
      (*set++).f = step->freq;
    }
    if (updn->params & MGS_DYNFREQ) {
      (*set++).f = step->dynfreq;
    }
    if (updn->params & MGS_PHASE) {
      (*set++).i = MGS_Osc_PHASE(step->phase);
    }
    if (updn->params & MGS_AMP) {
      (*set++).f = step->amp;
    }
    if (updn->params & MGS_DYNAMP) {
      (*set++).f = step->dynamp;
    }
    if (updn->params & MGS_ATTR) {
      (*set++).i = step->attr;
    }
    if (updn->params & MGS_AMODS) {
      (*set++).i = step->amod.chain->type_id;
    }
    if (updn->params & MGS_FMODS) {
      (*set++).i = step->fmod.chain->type_id;
    }
    if (updn->params & MGS_PMODS) {
      (*set++).i = step->pmod.chain->type_id;
    }
    node_data += count_flags(step->params);
    if (set != node_data) {
      MGS_warning("generator", "node %zd data value count off by %d",
          i, (int)(set - node_data));
    }
    IndexNode *indn = &o->index_nodes[i];
    indn->node = updn;
    indn->pos = -delay;
    indn->type = step->type;
    indn->ref_prev = indn_ref_prev;
    MGS_print_state(o, indn);
    ++i;
  }
}

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate) {
  MGS_Generator *o;
  MGS_ProgramNode *step;
  size_t indn_count = prg->node_count;
  size_t sndn_count = prg->type_counts[MGS_TYPE_OP];
  size_t vocn_count = 0;
  size_t updn_count = prg->node_count;
  size_t data_count = 0;
  for (step = prg->node_list; step; step = step->next) {
    if (!step->ref_prev && step->first_id == step->root_id) {
      ++vocn_count;
      //--b
    }
    data_count += count_flags(step->params);
  }
  o = calloc(1, sizeof(MGS_Generator));
  o->prg = prg;
  o->srate = srate;
  o->osc_coeff = MGS_Osc_COEFF(srate);
  o->indn_end = indn_count;
  o->index_nodes = calloc(indn_count, sizeof(IndexNode));
  o->sound_nodes = calloc(sndn_count, sizeof(SoundNode));
  o->voice_nodes = calloc(vocn_count, sizeof(VoiceNode));
  o->update_nodes = calloc(updn_count, sizeof(UpdateNode));
  o->node_data = calloc(data_count, sizeof(Data));
  MGS_global_init_Wave();
  init_for_nodelist(o, prg->node_list);
  /* mods init part two - give proper entries */
  for (size_t i = 0; i < sndn_count; ++i) {
    SoundNode *sndn = &o->sound_nodes[i];
    if (sndn->link) {
      uint32_t type_id = ((MGS_ProgramNode*)sndn->link)->type_id;
      sndn->link = &o->sound_nodes[type_id];
    }
    //sndn->cur_indn = NULL;
  }
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

static void MGS_Generator_prepare_node(MGS_Generator *o, IndexNode *indn) {
  UpdateNode *updn = indn->node;
  SoundNode *sndn = updn->sndn;
  IndexNode *root_indn = &o->index_nodes[sndn->root_i];
  SoundNode *root_sndn = (root_indn->status & MGS_INDN_PREP) ?
      root_indn->node :
      ((UpdateNode*)root_indn->node)->sndn;
  bool is_root = (sndn == root_sndn);
  switch (indn->type) {
  case MGS_TYPE_OP: {
    Data *get = updn->data;
    bool adjtime = false;
    /* set state */
    if (updn->params & MGS_TIME) {
      sndn->time = (*get++).i;
      indn->pos = 0;
      if (sndn->time) {
        if (is_root)
          indn->status |= MGS_INDN_EXEC;
        adjtime = true;
      } else
        indn->status &= ~MGS_INDN_EXEC;
    }
    if (updn->params & MGS_WAVE) {
      uint8_t wave = (*get++).i;
      sndn->osc.lut = MGS_Osc_LUT(wave);
    }
    if (updn->params & MGS_FREQ) {
      sndn->freq = (*get++).f;
      adjtime = true;
    }
    if (updn->params & MGS_DYNFREQ) {
      sndn->dynfreq = (*get++).f;
    }
    if (updn->params & MGS_PHASE) {
      sndn->osc.phase = (uint32_t)(*get++).i;
    }
    if (updn->params & MGS_AMP) {
      sndn->amp = (*get++).f;
    }
    if (updn->params & MGS_DYNAMP) {
      sndn->dynamp = (*get++).f;
    }
    if (updn->params & MGS_ATTR) {
      sndn->attr = (uint8_t)(*get++).i;
    }
    if (updn->params & MGS_AMODS) {
      sndn->amodchain = &o->sound_nodes[(*get++).i];
    }
    if (updn->params & MGS_FMODS) {
      sndn->fmodchain = &o->sound_nodes[(*get++).i];
    }
    if (updn->params & MGS_PMODS) {
      sndn->pmodchain = &o->sound_nodes[(*get++).i];
    }
    if (is_root) {
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, sndn);
    }
    upsize_bufs(o, root_sndn);
    if (indn->ref_prev != NULL) {
      /*
       * Ensure previous node is disabled;
       * prevents simultaneous running and
       * overlapping audio generation when
       * timing is tweaked.
       */
      indn->ref_prev->status &= ~MGS_INDN_EXEC;
    }
    indn->node = sndn;
    break; }
  case MGS_TYPE_ENV:
    break;
  }
  indn->status |= MGS_INDN_PREP;
  //MGS_print_state(o, indn);
}

void MGS_destroy_Generator(MGS_Generator *o) {
  if (!o)
    return;
  free(o->bufs);
  free(o->index_nodes);
  free(o->sound_nodes);
  free(o->voice_nodes);
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
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0);
    for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i;
      sp[0] += s;
      if (n->mode == MGS_MODE_CENTER)
        sp[1] += s;
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
    IndexNode *indn = &o->index_nodes[i];
    if (indn->pos < 0) {
      uint32_t delay = -indn->pos;
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
    if (!(indn->status & MGS_INDN_PREP))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGS_Generator_prepare_node(o, indn);
  }
  for (i = o->indn_i; i < o->indn_end; ++i) {
    IndexNode *indn = &o->index_nodes[i];
    if (indn->pos < 0) {
      uint32_t delay = -indn->pos;
      if (o->time_flags & MGS_GEN_TIME_OFFS) {
        indn->pos += o->delay_offs; /* delay change == previous time change */
        o->delay_offs = 0;
        o->time_flags &= ~MGS_GEN_TIME_OFFS;
      }
      if (delay >= len) {
        indn->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      indn->pos = 0;
    }
    if (!(indn->status & MGS_INDN_PREP))
      MGS_Generator_prepare_node(o, indn);
    if (indn->status & MGS_INDN_EXEC) {
      SoundNode *n = indn->node;
      indn->pos += run_node(o, n, buf, indn->pos, len);
      if ((uint32_t)indn->pos == n->time)
        indn->status &= ~MGS_INDN_EXEC;
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
    uint8_t cur_status = o->index_nodes[o->indn_i].status;
    if (!(cur_status & MGS_INDN_PREP) || cur_status & MGS_INDN_EXEC)
      break;
    ++o->indn_i;
  }
  return true;
}
