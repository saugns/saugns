/* mgensys: Script parser.
 * Copyright (c) 2011, 2019-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "parser.h"
#include <string.h>
#include <stdlib.h>

#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

static uint8_t filter_symchar(mgsFile *restrict f mgsMaybeUnused,
                              uint8_t c) {
  return IS_SYMCHAR(c) ? c : 0;
}

enum {
  MGS_SYM_VAR = 0,
  MGS_SYM_LINE_ID,
  MGS_SYM_NOISE_ID,
  MGS_SYM_WAVE_ID,
  MGS_SYM_TYPES
};

bool mgs_init_LangOpt(mgsLangOpt *restrict o, mgsSymTab *restrict symt) {
  (void)o;
  if (!mgsSymTab_add_stra(symt, mgsLine_names, MGS_LINE_NAMED,
                          MGS_SYM_LINE_ID) ||
      !mgsSymTab_add_stra(symt, mgsNoise_names, MGS_NOISE_NAMED,
                          MGS_SYM_NOISE_ID) ||
      !mgsSymTab_add_stra(symt, mgsWave_names, MGS_WAVE_NAMED,
                          MGS_SYM_WAVE_ID))
    return false;
  return true;
}

typedef struct mgsParser {
  mgsFile *f;
  mgsProgram *prg;
  mgsMemPool *mp;
  char *symbuf;
  uint32_t line;
  uint32_t reclevel;
  /* node state */
  uint32_t level;
  uint32_t setdef, setnode;
  mgsProgramNode *cur_node;
  mgsProgramNode *prev_node;
  mgsProgramNode *cur_dur;
  mgsProgramNode *cur_sound;
  struct mgsNodeData *cur_nd;
  /* settings/ops */
  float n_pan;
  float n_ampmult;
  float n_time;
  float n_freq, n_ratio;
} mgsParser;

static mgsNoinline void warning(mgsParser *o, const char *s, char c) {
  mgsFile *f = o->f;
  if (MGS_IS_ASCIIVISIBLE(c)) {
    fprintf(stderr, "warning: %s [line %d, at '%c'] - %s\n",
            f->path, o->line, c, s);
  } else if (c > MGS_FILE_MARKER) {
    fprintf(stderr, "warning: %s [line %d, at 0x%xc] - %s\n",
            f->path, o->line, c, s);
  } else if (mgsFile_AT_EOF(f)) {
    fprintf(stderr, "warning: %s [line %d, at EOF] - %s\n",
            f->path, o->line, s);
  } else {
    fprintf(stderr, "warning: %s [line %d] - %s\n",
            f->path, o->line, s);
  }
}

static void skip_ws(mgsParser *restrict o) {
  for (;;) {
    uint8_t c = mgsFile_GETC(o->f);
    if (IS_SPACE(c))
      continue;
    if (c == '\n') {
      ++o->line;
      mgsFile_TRYC(o->f, '\r');
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      mgsFile_skipline(o->f);
      c = mgsFile_GETC(o->f);
    } else {
      mgsFile_DECP(o->f);
      break;
    }
  }
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool check_invalid(mgsParser *restrict o, char c) {
  bool eof = mgsFile_AT_EOF(o->f);
  if (!eof || c > MGS_FILE_MARKER)
    warning(o, "invalid character", c);
  return !eof;
}

enum {
  ND_OWN_NODE = 1<<0,
};

/* things that need to be separate for each nested parse_level() go here */
typedef struct mgsNodeData {
  mgsParser *o;
  struct mgsNodeData *up;
  mgsProgramNode *node; /* state for tentative node until end_node() */
  mgsProgramArrData *target;
  mgsSymItem *set_var; /* assign variable to next node? */
  uint32_t flags;
  /* timing/delay */
  uint8_t n_time_delay; // TODO: implement
  float n_delay_next;
} mgsNodeData;

static void end_node(mgsNodeData *nd) {
  if (nd->node) {
    mgsProgramData *data = nd->node->data;
    mgs_svirt(end_prev_node, data, nd->o);
  }
}

static bool mgsProgramData_end_prev_node(mgsParser *pr) {
  mgsNodeData *nd = pr->cur_nd;
  if (!(nd->flags & ND_OWN_NODE))
    return false; /* nothing to do */

  nd->node = NULL;
  nd->flags &= ~ND_OWN_NODE;
  return true;
}

static bool mgsProgramSoundData_end_prev_node(mgsParser *pr) {
  mgsNodeData *nd = pr->cur_nd;
  mgsProgramNode *n = nd->node;
  if (!mgsProgramData_end_prev_node(pr))
    return false; /* nothing to do */

  mgsProgramSoundData *snd = n->data;
  if (!n->ref_prev) {
    /* first node sets all values */
    snd->params |= MGS_PARAM_MASK;
  } else {
    if (snd->time.flags & MGS_TIME_SET)
      snd->params |= MGS_SOUNDP_TIME;
  }
  if (snd->params & MGS_SOUNDP_AMP) {
    /* only apply to non-modulators */
    if (n->base_id == snd->root->base_id)
      snd->amp *= pr->n_ampmult;
  }
  return true;
}

static void mgsProgramData_vtinit(mgsProgramSoundData_Meta *meta) {
	meta->virt.end_prev_node = mgsProgramData_end_prev_node;
}

static void mgsProgramSoundData_vtinit(mgsProgramSoundData_Meta *meta) {
	meta->virt.end_prev_node = mgsProgramSoundData_end_prev_node;
}

MGSmetainst(mgsProgramData, mgsNone, NULL, mgsProgramData_vtinit)
MGSmetainst(mgsProgramSoundData, mgsProgramData, NULL,
		mgsProgramSoundData_vtinit)
MGSmetainst(mgsProgramLineData, mgsProgramSoundData, NULL, NULL)
MGSmetainst(mgsProgramNoiseData, mgsProgramSoundData, NULL, NULL)
MGSmetainst(mgsProgramWaveData, mgsProgramSoundData, NULL, NULL)
MGSmetainst(mgsProgramScopeData, mgsProgramData, NULL, NULL)
MGSmetainst(mgsProgramDurData, mgsProgramScopeData, NULL, NULL)
MGSmetainst(mgsProgramArrData, mgsProgramScopeData, NULL, NULL)

static mgsProgramNode *create_node(mgsParser *pr) {
  mgsNodeData *nd = pr->cur_nd;
  mgsProgram *p = pr->prg;
  end_node(nd);
  mgsProgramNode *n = mgs_mpalloc(p->mem, sizeof(mgsProgramNode));
  nd->node = n;
  nd->flags |= ND_OWN_NODE;

  /*
   * Handle IDs and linking.
   */
  pr->prev_node = pr->cur_node;
  ++p->node_count;
  if (!p->node_list)
    p->node_list = n;
  else
    pr->cur_node->next = n;
  pr->cur_node = n;

  /* prepare timing adjustment */
  n->delay = nd->n_delay_next;
  nd->n_delay_next = 0.f;

  return n;
}

MGSctordef_(mgsProgramSoundData,,,
		(void *mem, mgsParser *pr, mgsProgramNode *ref_prev),
		(mem, pr, ref_prev)) {
  mgsNodeData *nd = pr->cur_nd;
  mgsProgramSoundData *snd = mem;
  mgsProgram *p = pr->prg;
  mgsProgramNode *n = create_node(pr);
  n->ref_prev = ref_prev;
  if (pr->cur_dur != NULL) {
    mgsProgramDurData *dur = pr->cur_dur->data;
    if (!dur->first_node)
      dur->first_node = n;
    dur->last_node = n;
  }

  if (!n->ref_prev) {
    if (!nd->target) {
      snd->root = n;
      ++p->root_count;
    } else {
      mgsProgramSoundData *prev_snd = pr->cur_sound->data;
      snd->root = prev_snd->root;
      if (!nd->target->first_node)
        nd->target->first_node = n;
      else {
        mgsProgramSoundData *link_sound = nd->target->last_node->data;
        link_sound->nested_next = n;
      }
      nd->target->last_node = n;
      ++nd->target->count;
    }
    snd->amp = 1.f;
    snd->dynamp = snd->amp;
    snd->pan = pr->n_pan;
    n->base_id = p->base_counts[MGS_BASETYPE_SOUND]++;
  } else {
    mgsProgramNode *ref = n->ref_prev;
    mgsProgramSoundData *ref_snd = ref->data;
    //memcpy(snd, ref_snd, snd->meta->size);
    snd->root = ref_snd->root;
    n->base_id = ref->base_id;
  }
  /* time is not copied across reference */
  snd->time.v = pr->n_time;
  snd->time.flags = 0;
  n->base_type = MGS_BASETYPE_SOUND;
  n->data = snd;
  pr->cur_sound = n;

  /*
   * Make a variable point to this?
   */
  if (nd->set_var) {
    nd->set_var->data_use = MGS_SYM_DATA_OBJ;
    nd->set_var->data.obj = n;
    nd->set_var = NULL;
  }
  return true;
}

MGSctordef_(mgsProgramLineData,,,
		(void *mem, mgsParser *pr, mgsProgramNode *ref_prev),
		(mem, pr, ref_prev)) {
  if (!mgsProgramSoundData_ctor(mem, pr, ref_prev))
	  return false;
  mgsNodeData *nd = pr->cur_nd;
  mgsProgramNode *n = nd->node;
  n->type = MGS_TYPE_LINE;
  mgsProgramLineData *lod = mem;
  lod->line.time_ms = lrint(lod->time.v * 1000.f);
  lod->line.fill_type = MGS_LINE_N_lin;
  lod->line.flags |= MGS_LINEP_STATE
                  | MGS_LINEP_FILL_TYPE
                  | MGS_LINEP_TIME_IF_NEW;
  return true;
}

MGSctordef_(mgsProgramNoiseData,,,
		(void *mem, mgsParser *pr, mgsProgramNode *ref_prev),
		(mem, pr, ref_prev)) {
  if (!mgsProgramSoundData_ctor(mem, pr, ref_prev))
	  return false;
  mgsNodeData *nd = pr->cur_nd;
  mgsProgramNode *n = nd->node;
  n->type = MGS_TYPE_NOISE;
  return true;
}

MGSctordef_(mgsProgramWaveData,,,
		(void *mem, mgsParser *pr, mgsProgramNode *ref_prev),
		(mem, pr, ref_prev)) {
  if (!mgsProgramSoundData_ctor(mem, pr, ref_prev))
	  return false;
  mgsProgramWaveData *wod = mem;
  mgsNodeData *nd = pr->cur_nd;
  mgsProgramNode *n = nd->node;
  n->type = MGS_TYPE_WAVE;
  if (!n->ref_prev) {
    if (!nd->target) {
      wod->freq = pr->n_freq;
    } else {
      wod->freq = pr->n_ratio;
      wod->attr |= MGS_ATTR_FREQRATIO | MGS_ATTR_DYNFREQRATIO;
    }
    wod->dynfreq = wod->freq;
  }
  return true;
}

MGSctordef_(mgsProgramScopeData,,, (void *mem, mgsParser *pr), (mem, pr)) {
  (void)pr;
  (void)mem;
  return true;
}

MGSctordef_(mgsProgramDurData,,, (void *mem, mgsParser *pr), (mem, pr)) {
  if (!mgsProgramScopeData_ctor(mem, pr))
	  return false;
  mgsProgramNode *n = create_node(pr);
  if (!n)
	  return false;
  n->base_type = MGS_BASETYPE_SCOPE;
  n->data = mem;
  mgsProgramDurData *dur = mem;
  (void)dur;
  //mgsNodeData *nd = pr->cur_nd;
  //mgsProgramNode *n = nd->node;
  n->type = MGS_TYPE_DUR;
  if (pr->cur_dur != NULL) {
    mgsProgramDurData *prev_dur = pr->cur_dur->data;
    prev_dur->next_dur = n;
  }
  pr->cur_dur = n;
  return true;
}

MGSctordef_(mgsProgramArrData,,, (void *mem, mgsParser *pr), (mem, pr)) {
  if (!mgsProgramScopeData_ctor(mem, pr))
	  return false;
  return true;
}

static void mgs_init_NodeData(mgsNodeData *nd, mgsParser *o,
    mgsProgramArrData *target) {
  memset(nd, 0, sizeof(mgsNodeData));
  nd->o = o;
  nd->target = target;
  if (o->cur_nd != NULL) {
    mgsNodeData *up = o->cur_nd;
    nd->up = up;
    if (!target) {
      nd->target = up->target;
    }
  }
  o->cur_nd = nd;
  if (!o->cur_dur) {
    mgsProgramDurData_mpnew(o->mp, o);
  }
}

static void mgs_fini_NodeData(mgsNodeData *nd) {
  mgsParser *o = nd->o;
  end_node(nd);
  o->cur_nd = nd->up;
}

static mgsSymItem *scan_sym(mgsParser *o, uint32_t type_id, char pos_c);

/* \return length if number read and \p val set */
typedef size_t (*NumSym_f)(mgsParser *restrict o, double *restrict val);

typedef struct NumParser {
  mgsParser *pr;
  NumSym_f numsym_f;
  bool has_infnum;
  bool after_rpar;
} NumParser;
enum {
  NUMEXP_SUB = 0,
  NUMEXP_ADT,
  NUMEXP_MLT,
  NUMEXP_POW,
  NUMEXP_NUM,
};
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
  mgsParser *pr = o->pr;
  double num;
  uint8_t c;
  if (level > 0) skip_ws(pr);
  c = mgsFile_GETC(pr->f);
  if (c == '(') {
    num = scan_num_r(o, NUMEXP_SUB, level+1);
  } else if (c == '+' || c == '-') {
    num = scan_num_r(o, NUMEXP_ADT, level+1);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (c == '$') {
    mgsSymItem *var = scan_sym(pr, MGS_SYM_VAR, c);
    if (!var) goto REJECT;
    if (var->data_use != MGS_SYM_DATA_NUM) {
      warning(pr,
"variable used in numerical expression doesn't hold a number", c);
      goto REJECT;
    }
    num = var->data.num;
  } else if (o->numsym_f && IS_ALPHA(c)) {
    mgsFile_DECP(pr->f);
    size_t read_len = o->numsym_f(pr, &num);
    if (read_len == 0) goto REJECT;
    c = mgsFile_RETC(pr->f);
    if (IS_SYMCHAR(c)) {
      mgsFile_UNGETN(pr->f, read_len);
      goto REJECT;
    }
  } else {
    mgsFile_DECP(pr->f);
    size_t read_len;
    mgsFile_getd(pr->f, &num, false, &read_len);
    if (read_len == 0) goto REJECT;
  }
  if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
  for (;;) {
    bool rpar_mlt = false;
    if (isinf(num)) o->has_infnum = true;
    if (level > 0) skip_ws(pr);
    c = mgsFile_GETC(pr->f);
    if (pri < NUMEXP_MLT) {
      rpar_mlt = o->after_rpar;
      o->after_rpar = false;
    }
    switch (c) {
    case '(':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num *= scan_num_r(o, NUMEXP_SUB, level+1);
      break;
    case ')':
      if (pri != NUMEXP_SUB || level == 0) goto DEFER;
      o->after_rpar = true;
      goto ACCEPT;
    case '^':
      if (pri > NUMEXP_POW) goto DEFER;
      num = pow(num, scan_num_r(o, NUMEXP_POW, level));
      break;
    case '*':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num *= scan_num_r(o, NUMEXP_MLT, level);
      break;
    case '/':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num /= scan_num_r(o, NUMEXP_MLT, level);
      break;
    case '%':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num = fmod(num, scan_num_r(o, NUMEXP_MLT, level));
      break;
    case '+':
      if (pri >= NUMEXP_ADT) goto DEFER;
      num += scan_num_r(o, NUMEXP_ADT, level);
      break;
    case '-':
      if (pri >= NUMEXP_ADT) goto DEFER;
      num -= scan_num_r(o, NUMEXP_ADT, level);
      break;
    default:
      if (rpar_mlt && !(IS_SPACE(c) || IS_LNBRK(c))) {
        mgsFile_DECP(pr->f);
        double rval = scan_num_r(o, NUMEXP_MLT, level);
        if (isnan(rval)) goto ACCEPT;
        num *= rval;
        break;
      }
      if (pri == NUMEXP_SUB && level > 0) {
        warning(pr, "numerical expression has '(' without closing ')'", c);
      }
      goto DEFER;
    }
    if (isnan(num)) goto DEFER;
  }
DEFER:
  mgsFile_DECP(pr->f);
ACCEPT:
  if (0)
REJECT: {
    num = NAN;
  }
  return num;
}
static mgsNoinline bool scan_num(mgsParser *restrict o,
    NumSym_f scan_numsym, double *restrict var) {
  NumParser np = {o, scan_numsym, false, false};
  double num = scan_num_r(&np, NUMEXP_SUB, 0);
  if (isnan(num))
    return false;
  if (isinf(num)) np.has_infnum = true;
  if (np.has_infnum) {
    warning(o, "discarding expression with infinite number", 0);
    return false;
  }
  *var = num;
  return true;
}

static mgsNoinline bool scan_timeval(mgsParser *restrict o,
    double *restrict val) {
  double tval;
  if (!scan_num(o, NULL, &tval))
    return false;
  if (tval < 0) {
    warning(o, "discarding negative time value", 0);
    return false;
  }
  *val = tval;
  return true;
}

#define SYMKEY_MAXLEN 79
#define SYMKEY_MAXLEN_A "79"
static mgsSymItem *scan_sym(mgsParser *o, uint32_t type_id, char pos_c) {
  mgsSymTab *st = o->prg->symt;
  size_t len = 0;
  bool truncated;
  truncated = !mgsFile_getstr(o->f, o->symbuf, SYMKEY_MAXLEN + 1,
      &len, filter_symchar);
  if (len == 0) {
    warning(o, "symbol name missing", pos_c);
    return NULL;
  }
  if (truncated) {
    warning(o, "limiting symbol name to "SYMKEY_MAXLEN_A" characters", pos_c);
    len += mgsFile_skipstr(o->f, filter_symchar);
  }
  mgsSymStr *s = mgsSymTab_get_symstr(st, o->symbuf, len);
  mgsSymItem *item = mgsSymTab_find_item(st, s, type_id);
  if (!item && type_id == MGS_SYM_VAR)
    item = mgsSymTab_add_item(st, s, MGS_SYM_VAR);
  return item;
}

static size_t numsym_channel(mgsParser *restrict o, double *restrict val) {
  char c = mgsFile_GETC(o->f);
  switch (c) {
  case 'C':
    *val = 0.f;
    return 1;
  case 'L':
    *val = -1.f;
    return 1;
  case 'R':
    *val = 1.f;
    return 1;
  default:
    mgsFile_DECP(o->f);
    return 0;
  }
}

static bool scan_linetype(mgsParser *restrict o,
    size_t *restrict found_id, char pos_c) {
  mgsSymItem *item = scan_sym(o, MGS_SYM_LINE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = mgsLine_names;
  warning(o, "invalid line type; available are:", pos_c);
  mgs_print_names(names, "\t", stderr);
  return false;
}

static bool scan_noisetype(mgsParser *restrict o,
    size_t *restrict found_id, char pos_c) {
  mgsSymItem *item = scan_sym(o, MGS_SYM_NOISE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = mgsNoise_names;
  warning(o, "invalid noise type; available are:", pos_c);
  mgs_print_names(names, "\t", stderr);
  return false;
}

static bool scan_wavetype(mgsParser *restrict o,
    size_t *restrict found_id, char pos_c) {
  mgsSymItem *item = scan_sym(o, MGS_SYM_WAVE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = mgsWave_names;
  warning(o, "invalid wave type; available are:", pos_c);
  mgs_print_names(names, "\t", stderr);
  return false;
}

static void parse_level(mgsParser *o, mgsProgramArrData *chain);

static mgsProgram* parse(mgsFile *f, mgsParser *o) {
  memset(o, 0, sizeof(mgsParser));
  o->f = f;
  mgsMemPool *mem = mgs_create_MemPool(0);
  mgsSymTab *symt = mgs_create_SymTab(mem);
  o->prg = mgs_mpalloc(mem, sizeof(mgsProgram));
  o->prg->mem = mem;
  o->prg->symt = symt;
  o->prg->name = f->path;
  o->mp = mem;
  mgs_init_LangOpt(&o->prg->lopt, symt);
  o->symbuf = calloc(SYMKEY_MAXLEN + 1, sizeof(char));
  o->line = 1;
  o->n_pan = 0.f; /* default until changed */
  o->n_ampmult = 1.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_ratio = 1.f; /* default until changed */
  parse_level(o, NULL);
  free(o->symbuf);
  return o->prg;
}

static bool parse_amp(mgsParser *o, mgsProgramNode *n) {
  mgsProgramSoundData *sound;
  sound = mgsProgramNode_get_data(n, mgsProgramSoundData);
  if (!sound) goto INVALID;
  double f;
  if (mgsFile_TRYC(o->f, '!')) {
    if (!mgsFile_TESTC(o->f, '{')) {
      if (!scan_num(o, NULL, &f)) goto INVALID;
      sound->dynamp = f;
      sound->params |= MGS_SOUNDP_DYNAMP;
    }
    if (mgsFile_TRYC(o->f, '{')) {
      sound->amod = mgsProgramArrData_mpnew(o->mp, o);
      sound->amod->mod_type = MGS_MOD_AM;
      parse_level(o, sound->amod);
    }
  } else {
    if (!scan_num(o, NULL, &f)) goto INVALID;
    sound->amp = f;
    sound->params |= MGS_SOUNDP_AMP;
  }
  return true;
INVALID:
  return false;
}

static bool parse_channel(mgsParser *o, mgsProgramNode *n) {
  mgsProgramSoundData *sound;
  sound = mgsProgramNode_get_data(n, mgsProgramSoundData);
  if (!sound) goto INVALID;
  mgsNodeData *nd = o->cur_nd;
  if (nd->target != NULL) {
    mgsProgramArrData *target = nd->target;
    if (target->mod_type != 0) goto INVALID;
  }
  double f;
  /* TODO: support modulation */
  if (!scan_num(o, numsym_channel, &f)) goto INVALID;
  sound->pan = f;
  sound->params |= MGS_SOUNDP_PAN;
  return true;
INVALID:
  return false;
}

static bool parse_freq(mgsParser *o, mgsProgramNode *n, bool ratio) {
  mgsProgramWaveData *wod = mgsProgramNode_get_data(n, mgsProgramWaveData);
  if (!wod) goto INVALID;
  if (ratio && (n->base_id == wod->root->base_id)) goto INVALID;
  double f;
  if (mgsFile_TRYC(o->f, '!')) {
    if (!mgsFile_TESTC(o->f, '{')) {
      if (!scan_num(o, NULL, &f)) goto INVALID;
      wod->dynfreq = f;
      if (ratio) {
        wod->attr |= MGS_ATTR_DYNFREQRATIO;
      } else {
        wod->attr &= ~MGS_ATTR_DYNFREQRATIO;
      }
      wod->params |= MGS_WAVEP_DYNFREQ | MGS_WAVEP_ATTR;
    }
    if (mgsFile_TRYC(o->f, '{')) {
      wod->fmod = mgsProgramArrData_mpnew(o->mp, o);
      wod->fmod->mod_type = MGS_MOD_FM;
      parse_level(o, wod->fmod);
    }
  } else {
    if (!scan_num(o, NULL, &f)) goto INVALID;
    wod->freq = f;
    if (ratio) {
      wod->attr |= MGS_ATTR_FREQRATIO;
    } else {
      wod->attr &= ~MGS_ATTR_FREQRATIO;
    }
    wod->params |= MGS_WAVEP_FREQ | MGS_WAVEP_ATTR;
  }
  return true;
INVALID:
  return false;
}

static bool parse_goal(mgsParser *o, mgsProgramNode *n) {
  mgsProgramLineData *lod = mgsProgramNode_get_data(n, mgsProgramLineData);
  if (!lod) goto INVALID;
  double f;
  if (!scan_num(o, NULL, &f)) goto INVALID;
  lod->line.vt = f;
  lod->line.flags |= MGS_LINEP_GOAL;
  return true;
INVALID:
  return false;
}

static bool parse_line(mgsParser *o, mgsProgramNode *n, char pos_c) {
  mgsProgramLineData *lod = mgsProgramNode_get_data(n, mgsProgramLineData);
  if (!lod) goto INVALID;
  size_t line;
  if (!scan_linetype(o, &line, pos_c)) goto INVALID;
  lod->line.fill_type = line;
  lod->line.flags |= MGS_LINEP_FILL_TYPE;
  return true;
INVALID:
  return false;
}

static bool parse_noise(mgsParser *o, mgsProgramNode *n, char pos_c) {
  mgsProgramNoiseData *nod = mgsProgramNode_get_data(n, mgsProgramNoiseData);
  if (!nod) goto INVALID;
  size_t noise;
  if (!scan_noisetype(o, &noise, pos_c)) goto INVALID;
  nod->noise = noise;
  nod->params |= MGS_NOISEP_NOISE;
  return true;
INVALID:
  return false;
}

static bool parse_phase(mgsParser *o, mgsProgramNode *n) {
  mgsProgramWaveData *wod = mgsProgramNode_get_data(n, mgsProgramWaveData);
  if (!wod) goto INVALID;
  double f;
  if (mgsFile_TRYC(o->f, '!')) {
    if (mgsFile_TRYC(o->f, '{')) {
      wod->pmod = mgsProgramArrData_mpnew(o->mp, o);
      wod->pmod->mod_type = MGS_MOD_PM;
      parse_level(o, wod->pmod);
    }
  } else {
    if (!scan_num(o, NULL, &f)) goto INVALID;
    wod->phase = lrint(f * UINT32_MAX);
    wod->params |= MGS_WAVEP_PHASE;
  }
  return true;
INVALID:
  return false;
}

static bool parse_time(mgsParser *o, mgsProgramNode *n) {
  mgsProgramSoundData *sound = mgsProgramNode_get_data(n, mgsProgramSoundData);
  if (!sound) goto INVALID;
  double f;
  if (!scan_timeval(o, &f)) goto INVALID;
  sound->time.v = f;
  sound->time.flags |= MGS_TIME_SET;
  mgsProgramLineData *lod = mgsProgramNode_get_data(n, mgsProgramLineData);
  if (lod != NULL) {
    lod->line.time_ms = lrint(f * 1000.f);
    lod->line.flags |= MGS_LINEP_TIME;
    lod->line.flags &= ~MGS_LINEP_TIME_IF_NEW;
  }
  return true;
INVALID:
  return false;
}

static bool parse_value(mgsParser *o, mgsProgramNode *n) {
  mgsProgramLineData *lod = mgsProgramNode_get_data(n, mgsProgramLineData);
  if (!lod) goto INVALID;
  double f;
  if (!scan_num(o, NULL, &f)) goto INVALID;
  lod->line.v0 = f;
  lod->line.flags |= MGS_LINEP_STATE;
  return true;
INVALID:
  return false;
}

static bool parse_wave(mgsParser *o, mgsProgramNode *n, char pos_c) {
  mgsProgramWaveData *wod = mgsProgramNode_get_data(n, mgsProgramWaveData);
  if (!wod) goto INVALID;
  size_t wave;
  if (!scan_wavetype(o, &wave, pos_c)) goto INVALID;
  wod->wave = wave;
  wod->params |= MGS_WAVEP_WAVE;
  return true;
INVALID:
  return false;
}

static bool parse_ref(mgsParser *o, char pos_c) {
  mgsNodeData *nd = o->cur_nd;
  if (nd->target != NULL)
    return false;
  mgsSymItem *sym = scan_sym(o, MGS_SYM_VAR, ':');
  if (!sym)
    return false;
  if (sym->data_use == MGS_SYM_DATA_OBJ) {
    mgsProgramNode *ref = sym->data.obj;
    void *data = NULL;
    switch (ref->type) {
    case MGS_TYPE_LINE: data = mgsProgramLineData_mpnew(o->mp, o, ref); break;
    case MGS_TYPE_NOISE: data = mgsProgramNoiseData_mpnew(o->mp, o, ref); break;
    case MGS_TYPE_WAVE: data = mgsProgramWaveData_mpnew(o->mp, o, ref); break;
    default: break; // FIXME: Do this better than switch-case...
    }
    sym->data.obj = o->cur_node;
    o->setnode = o->level + 1;
  } else {
    warning(o, "reference doesn't point to an object", pos_c);
  }
  return true;
}

static void parse_level(mgsParser *o, mgsProgramArrData *chain) {
  char c;
  double f;
  mgsNodeData nd;
  uint32_t entrylevel = o->level;
  ++o->reclevel;
  mgs_init_NodeData(&nd, o, chain);
  for (;;) {
    c = mgsFile_GETC(o->f);
    mgsFile_skipspace(o->f);
    switch (c) {
    case '\n':
      mgsFile_TRYC(o->f, '\r');
      /* fall-through */
    case '\r':
      if (!nd.target) {
        if (o->setdef > o->level)
          o->setdef = (o->level) ? (o->level - 1) : 0;
        else if (o->setnode > o->level) {
          o->setnode = (o->level) ? (o->level - 1) : 0;
          end_node(&nd);
        }
      }
      ++o->line;
      break;
    case '\t':
    case ' ':
      mgsFile_skipspace(o->f);
      break;
    case '#':
      mgsFile_skipline(o->f);
      break;
    case '/':
      if (o->setdef > o->setnode) goto INVALID;
      if (mgsFile_TRYC(o->f, 't')) {
        nd.n_time_delay = 1;
        break;
      }
      if (!scan_timeval(o, &f)) goto INVALID;
      nd.n_time_delay = 0;
      nd.n_delay_next += f;
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '}':
      if (!nd.target)
        goto INVALID;
      if (o->level != entrylevel) {
        o->level = entrylevel;
        warning(o, "closing '}' before closing '>'s", c);
      }
      goto RETURN;
    case '<':
      ++o->level;
      break;
    case '>':
      if (!o->level) {
        warning(o, "closing '>' without opening '<'", c);
        break;
      }
      if (o->setdef > o->level)
        o->setdef = (o->level) ? (o->level - 1) : 0;
      else if (o->setnode > o->level) {
        o->setnode = (o->level) ? (o->level - 1) : 0;
        end_node(&nd);
      }
      --o->level;
      break;
    case 'E':
      // new_node(&nd, NULL, MGS_TYPE_ENV);
      o->setnode = o->level + 1;
      break;
    case 'L':
      if (!scan_num(o, NULL, &f)) break;
      mgsProgramLineData *lod = mgsProgramLineData_mpnew(o->mp, o, NULL);
      lod->line.v0 = f;
      lod->line.flags |= MGS_LINEP_STATE;
      o->setnode = o->level + 1;
      break;
    case 'N': {
      size_t noise;
      if (!scan_noisetype(o, &noise, c)) break;
      mgsProgramNoiseData *nod = mgsProgramNoiseData_mpnew(o->mp, o, NULL);
      nod->noise = noise;
      o->setnode = o->level + 1;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      o->setdef = o->level + 1;
      break;
    case 'W': {
      size_t wave;
      if (!scan_wavetype(o, &wave, c)) break;
      mgsProgramWaveData *wod = mgsProgramWaveData_mpnew(o->mp, o, NULL);
      wod->wave = wave;
      o->setnode = o->level + 1;
      break; }
    case '|': {
      mgsProgramDurData *dur = o->cur_dur->data;
      if (!dur->first_node) {
        warning(o, "no sounds precede time separator", c);
        break;
      }
      dur = mgsProgramDurData_mpnew(o->mp, o);
      break; }
    case '\\':
      if (o->setdef > o->setnode)
        goto INVALID;
      if (!scan_timeval(o, &f)) goto INVALID;
      nd.node->delay += f;
      break;
    case '\'':
      end_node(&nd);
      if (nd.set_var != NULL) {
        warning(o, "ignoring variable assignment to variable assignment", c);
        break;
      }
      if (!(nd.set_var = scan_sym(o, MGS_SYM_VAR, '\''))) goto INVALID;
      break;
    case '=': {
      mgsSymItem *var = nd.set_var;
      if (!var) {
        warning(o, "ignoring dangling '='", c);
        break;
      }
      nd.set_var = NULL; // used here
      if (scan_num(o, NULL, &var->data.num))
        var->data_use = MGS_SYM_DATA_NUM;
      else
        warning(o, "missing right-hand value for variable '='", c);
      break; }
    case ':':
      if (!parse_ref(o, c)) goto INVALID;
      break;
    case 'a':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_ampmult = f;
        break;
      } else if (o->setnode <= 0)
        goto INVALID;
      if (!parse_amp(o, nd.node)) goto INVALID;
      break;
    case 'c':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, numsym_channel, &f)) goto INVALID;
        o->n_pan = f;
        break;
      } else if (o->setnode <= 0)
        goto INVALID;
      if (!parse_channel(o, nd.node)) goto INVALID;
      break;
    case 'f':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_freq = f;
        break;
      } else if (o->setnode <= 0)
        goto INVALID;
      if (!parse_freq(o, nd.node, false)) goto INVALID;
      break;
    case 'g':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_goal(o, nd.node)) goto INVALID;
      break;
    case 'l':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_line(o, nd.node, c)) goto INVALID;
      break;
    case 'n':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_noise(o, nd.node, c)) goto INVALID;
      break;
    case 'p':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_phase(o, nd.node)) goto INVALID;
      break;
    case 'r':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_ratio = f;
        break;
      } else if (o->setnode <= 0)
        goto INVALID;
      if (!parse_freq(o, nd.node, true)) goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode) {
        if (!scan_timeval(o, &f)) goto INVALID;
        o->n_time = f;
        break;
      } else if (o->setnode <= 0)
        goto INVALID;
      if (!parse_time(o, nd.node)) goto INVALID;
      break;
    case 'v':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_value(o, nd.node)) goto INVALID;
      break;
    case 'w':
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (!parse_wave(o, nd.node, c)) goto INVALID;
      break;
    default:
    INVALID:
      if (!check_invalid(o, c)) goto FINISH;
      break;
    }
  }
FINISH:
  if (o->level)
    warning(o, "end of file without closing '>'s", c);
  if (o->reclevel > 1)
    warning(o, "end of file without closing '}'s", c);
RETURN:
  mgs_fini_NodeData(&nd);
  --o->reclevel;
}

mgsProgram* mgs_create_Program(const char *file, bool is_path) {
  mgsProgram *o = NULL;
  mgsParser p;
  mgsFile *f = mgs_create_File();
  if (!f) goto ERROR;
  if (!is_path) {
    if (!mgsFile_stropenrb(f, "<string>", file)) {
      mgs_error(NULL, "NULL string passed for opening");
      goto ERROR;
    }
  } else if (!mgsFile_fopenrb(f, file)) {
    mgs_error(NULL, "couldn't open script file \"%s\" for reading", file);
    goto ERROR;
  }
  o = parse(f, &p);
  if (!o) goto ERROR;
  mgs_adjust_node_list(o->node_list);
ERROR:
  mgs_destroy_File(f);
  return o;
}

void mgs_destroy_Program(mgsProgram *o) {
  if (!o)
    return;
  mgs_destroy_MemPool(o->mem);
}
