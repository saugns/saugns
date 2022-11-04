/* mgensys: Script parser.
 * Copyright (c) 2011, 2019-2022 Joel K. Pettersson
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

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

static uint8_t filter_symchar(MGS_File *restrict f mgsMaybeUnused,
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

bool MGS_init_LangOpt(MGS_LangOpt *restrict o, MGS_SymTab *restrict symt) {
  if (!MGS_SymTab_add_stra(symt, MGS_Line_names, MGS_LINE_TYPES,
                          MGS_SYM_LINE_ID) ||
      !MGS_SymTab_add_stra(symt, MGS_Noise_names, MGS_NOISE_TYPES,
                          MGS_SYM_NOISE_ID) ||
      !MGS_SymTab_add_stra(symt, MGS_Wave_names, MGS_WAVE_TYPES,
                          MGS_SYM_WAVE_ID))
    return false;
  return true;
}

typedef struct MGS_Parser {
  MGS_File *f;
  MGS_Program *prg;
  char *symbuf;
  uint32_t line;
  uint32_t reclevel;
  /* node state */
  uint32_t level;
  uint32_t setdef, setnode;
  MGS_ProgramNode *cur_node;
  MGS_ProgramNode *prev_node;
  MGS_ProgramNode *cur_dur;
  MGS_ProgramNode *cur_sound;
  struct MGS_NodeData *cur_nd;
  /* settings/ops */
  float n_pan;
  float n_ampmult;
  float n_time;
  float n_freq, n_ratio;
} MGS_Parser;

static mgsNoinline void warning(MGS_Parser *o, const char *s, char c) {
  MGS_File *f = o->f;
  if (IS_VISIBLE(c)) {
    fprintf(stderr, "warning: %s [line %d, at '%c'] - %s\n",
            f->path, o->line, c, s);
  } else if (c > MGS_FILE_MARKER) {
    fprintf(stderr, "warning: %s [line %d, at 0x%xc] - %s\n",
            f->path, o->line, c, s);
  } else if (MGS_File_AT_EOF(f)) {
    fprintf(stderr, "warning: %s [line %d, at EOF] - %s\n",
            f->path, o->line, s);
  } else {
    fprintf(stderr, "warning: %s [line %d] - %s\n",
            f->path, o->line, s);
  }
}

static void skip_ws(MGS_Parser *restrict o) {
  for (;;) {
    uint8_t c = MGS_File_GETC(o->f);
    if (IS_SPACE(c))
      continue;
    if (c == '\n') {
      ++o->line;
      MGS_File_TRYC(o->f, '\r');
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      MGS_File_skipline(o->f);
      c = MGS_File_GETC(o->f);
    } else {
      MGS_File_DECP(o->f);
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
static bool check_invalid(MGS_Parser *restrict o, char c) {
  bool eof = MGS_File_AT_EOF(o->f);
  if (!eof || c > MGS_FILE_MARKER)
    warning(o, "invalid character", c);
  return !eof;
}

enum {
  ND_OWN_NODE = 1<<0,
};

/* things that need to be separate for each nested parse_level() go here */
typedef struct MGS_NodeData {
  MGS_Parser *o;
  struct MGS_NodeData *up;
  MGS_ProgramNode *node; /* state for tentative node until end_node() */
  MGS_ProgramArrData *target;
  MGS_SymItem *set_var; /* assign variable to next node? */
  uint32_t flags;
  /* timing/delay */
  uint8_t n_time_delay; // TODO: implement
  float n_delay_next;
} MGS_NodeData;

/*
 * Allocate sound data and do common initialization.
 *
 * References keep the original root and
 * are never added to nesting lists.
 * TODO: Implement references which copy
 * nodes to new locations, and/or move.
 */
static void new_sounddata(MGS_NodeData *nd, size_t size) {
  MGS_Parser *o = nd->o;
  MGS_Program *p = o->prg;
  MGS_ProgramNode *n = nd->node;
  MGS_ProgramSoundData *snd;
  if (!n->ref_prev) {
    snd = MGS_MemPool_alloc(p->mem, size);
    if (!nd->target) {
      snd->root = n;
      ++p->root_count;
    } else {
      MGS_ProgramSoundData *prev_snd = o->cur_sound->data;
      snd->root = prev_snd->root;
      if (!nd->target->scope.first_node)
        nd->target->scope.first_node = n;
      else {
        MGS_ProgramSoundData *link_sound = nd->target->scope.last_node->data;
        link_sound->nested_next = n;
      }
      nd->target->scope.last_node = n;
      ++nd->target->count;
    }
    snd->amp = 1.f;
    snd->dynamp = snd->amp;
    snd->pan = o->n_pan;
    n->base_id = p->base_counts[MGS_BASETYPE_SOUND]++;
  } else {
    MGS_ProgramNode *ref = n->ref_prev;
    snd = MGS_MemPool_memdup(p->mem, ref->data, size);
    snd->params = 0;
    n->base_id = ref->base_id;
  }
  /* time is not copied across reference */
  snd->time.v = o->n_time;
  snd->time.flags = 0;
  n->base_type = MGS_BASETYPE_SOUND;
  n->data = snd;
  o->cur_sound = n;
}

static void new_linedata(MGS_NodeData *nd) {
  new_sounddata(nd, sizeof(MGS_ProgramLineData));
  MGS_ProgramNode *n = nd->node;
  MGS_ProgramLineData *lod = n->data;
  lod->line.time_ms = lrint(lod->sound.time.v * 1000.f);
  lod->line.fill_type = MGS_LINE_LIN;
  lod->line.flags |= MGS_LINEP_STATE
                  | MGS_LINEP_FILL_TYPE
                  | MGS_LINEP_TIME_IF_NEW;
}

static void new_noisedata(MGS_NodeData *nd) {
  new_sounddata(nd, sizeof(MGS_ProgramNoiseData));
}

static void new_wavedata(MGS_NodeData *nd) {
  new_sounddata(nd, sizeof(MGS_ProgramWaveData));
  MGS_Parser *o = nd->o;
  MGS_ProgramNode *n = nd->node;
  MGS_ProgramWaveData *wod = n->data;
  if (!n->ref_prev) {
    if (!nd->target) {
      wod->freq = o->n_freq;
    } else {
      wod->freq = o->n_ratio;
      wod->attr |= MGS_ATTR_FREQRATIO | MGS_ATTR_DYNFREQRATIO;
    }
    wod->dynfreq = wod->freq;
  }
}

static void new_durdata(MGS_NodeData *nd) {
  MGS_Parser *o = nd->o;
  MGS_Program *p = o->prg;
  MGS_ProgramNode *n = nd->node;
  MGS_ProgramDurData *dur;
  dur = MGS_MemPool_alloc(p->mem, sizeof(MGS_ProgramDurData));
  n->base_type = MGS_BASETYPE_SCOPE;
  n->data = dur;
  if (o->cur_dur != NULL) {
    MGS_ProgramDurData *prev_dur = o->cur_dur->data;
    prev_dur->next = n;
  }
  o->cur_dur = n;
}

static void end_node(MGS_NodeData *nd);

static void new_node(MGS_NodeData *nd,
    MGS_ProgramNode *ref_prev, uint8_t type) {
  MGS_Parser *o = nd->o;
  MGS_Program *p = o->prg;
  end_node(nd);

  MGS_ProgramNode *n;
  n = MGS_MemPool_alloc(p->mem, sizeof(MGS_ProgramNode));
  nd->node = n;
  nd->flags |= ND_OWN_NODE;
  n->ref_prev = ref_prev;
  n->type = type;
  if (o->cur_dur != NULL && type != MGS_TYPE_DUR) {
    MGS_ProgramDurData *dur = o->cur_dur->data;
    if (!dur->scope.first_node)
      dur->scope.first_node = n;
    dur->scope.last_node = n;
  }

  /*
   * Handle IDs and linking.
   */
  o->prev_node = o->cur_node;
  ++p->node_count;
  if (!p->node_list)
    p->node_list = n;
  else
    o->cur_node->next = n;
  o->cur_node = n;

  /* prepare timing adjustment */
  n->delay = nd->n_delay_next;
  nd->n_delay_next = 0.f;

  switch (type) {
  case MGS_TYPE_LINE:
    new_linedata(nd);
    break;
  case MGS_TYPE_NOISE:
    new_noisedata(nd);
    break;
  case MGS_TYPE_WAVE:
    new_wavedata(nd);
    break;
  case MGS_TYPE_DUR:
    new_durdata(nd);
    break;
  }

  /*
   * Make a variable point to this?
   */
  if (nd->set_var) {
    nd->set_var->data_use = MGS_SYM_DATA_OBJ;
    nd->set_var->data.obj = n;
    nd->set_var = NULL;
  }
}

/*
 * Common sound data scope-ending preparation.
 */
static void end_sounddata(MGS_NodeData *nd) {
  MGS_Parser *o = nd->o;
  MGS_ProgramNode *n = nd->node;
  MGS_ProgramSoundData *snd = n->data;
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
      snd->amp *= o->n_ampmult;
  }
}

static void end_linedata(MGS_NodeData *nd) {
  end_sounddata(nd);
}

static void end_noisedata(MGS_NodeData *nd) {
  end_sounddata(nd);
}

static void end_wavedata(MGS_NodeData *nd) {
  end_sounddata(nd);
}

static void end_durdata(MGS_NodeData *nd mgsMaybeUnused) {
}

static void end_node(MGS_NodeData *nd) {
  MGS_ProgramNode *n = nd->node;
  if (!(nd->flags & ND_OWN_NODE))
    return; /* nothing to do */

  switch (n->type) {
  case MGS_TYPE_LINE:
    end_linedata(nd);
    break;
  case MGS_TYPE_NOISE:
    end_noisedata(nd);
    break;
  case MGS_TYPE_WAVE:
    end_wavedata(nd);
    break;
  case MGS_TYPE_DUR:
    end_durdata(nd);
    break;
  }

  nd->node = NULL;
  nd->flags &= ~ND_OWN_NODE;
}

static void MGS_init_NodeData(MGS_NodeData *nd, MGS_Parser *o,
    MGS_ProgramArrData *target) {
  memset(nd, 0, sizeof(MGS_NodeData));
  nd->o = o;
  nd->target = target;
  if (o->cur_nd != NULL) {
    MGS_NodeData *up = o->cur_nd;
    nd->up = up;
    if (!target) {
      nd->target = up->target;
    }
  }
  if (!o->cur_dur) {
    new_node(nd, NULL, MGS_TYPE_DUR); // initial instance
  }
  o->cur_nd = nd;
}

static void MGS_fini_NodeData(MGS_NodeData *nd) {
  MGS_Parser *o = nd->o;
  end_node(nd);
  o->cur_nd = nd->up;
}

static MGS_SymItem *scan_sym(MGS_Parser *o, uint32_t type_id, char pos_c);

/* \return length if number read and \p val set */
typedef size_t (*NumSym_f)(MGS_Parser *restrict o, double *restrict val);

typedef struct NumParser {
  MGS_Parser *pr;
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
  MGS_Parser *pr = o->pr;
  double num;
  uint8_t c;
  if (level > 0) skip_ws(pr);
  c = MGS_File_GETC(pr->f);
  if (c == '(') {
    num = scan_num_r(o, NUMEXP_SUB, level+1);
  } else if (c == '+' || c == '-') {
    num = scan_num_r(o, NUMEXP_ADT, level+1);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (c == '$') {
    MGS_SymItem *var = scan_sym(pr, MGS_SYM_VAR, c);
    if (!var) goto REJECT;
    if (var->data_use != MGS_SYM_DATA_NUM) {
      warning(pr,
"variable used in numerical expression doesn't hold a number", c);
      goto REJECT;
    }
    num = var->data.num;
  } else if (o->numsym_f && IS_ALPHA(c)) {
    MGS_File_DECP(pr->f);
    size_t read_len = o->numsym_f(pr, &num);
    if (read_len == 0) goto REJECT;
    c = MGS_File_RETC(pr->f);
    if (IS_SYMCHAR(c)) {
      MGS_File_UNGETN(pr->f, read_len);
      goto REJECT;
    }
  } else {
    MGS_File_DECP(pr->f);
    size_t read_len;
    MGS_File_getd(pr->f, &num, false, &read_len);
    if (read_len == 0) goto REJECT;
  }
  if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
  for (;;) {
    bool rpar_mlt = false;
    if (isinf(num)) o->has_infnum = true;
    if (level > 0) skip_ws(pr);
    c = MGS_File_GETC(pr->f);
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
        MGS_File_DECP(pr->f);
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
  MGS_File_DECP(pr->f);
ACCEPT:
  if (0)
REJECT: {
    num = NAN;
  }
  return num;
}
static mgsNoinline bool scan_num(MGS_Parser *restrict o,
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

static mgsNoinline bool scan_timeval(MGS_Parser *restrict o,
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
static MGS_SymItem *scan_sym(MGS_Parser *o, uint32_t type_id, char pos_c) {
  MGS_SymTab *st = o->prg->symt;
  size_t len = 0;
  bool truncated;
  truncated = !MGS_File_getstr(o->f, o->symbuf, SYMKEY_MAXLEN + 1,
      &len, filter_symchar);
  if (len == 0) {
    warning(o, "symbol name missing", pos_c);
    return NULL;
  }
  if (truncated) {
    warning(o, "limiting symbol name to "SYMKEY_MAXLEN_A" characters", pos_c);
    len += MGS_File_skipstr(o->f, filter_symchar);
  }
  MGS_SymStr *s = MGS_SymTab_get_symstr(st, o->symbuf, len);
  MGS_SymItem *item = MGS_SymTab_find_item(st, s, type_id);
  if (!item && type_id == MGS_SYM_VAR)
    item = MGS_SymTab_add_item(st, s, MGS_SYM_VAR);
  return item;
}

static size_t numsym_channel(MGS_Parser *restrict o, double *restrict val) {
  char c = MGS_File_GETC(o->f);
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
    MGS_File_DECP(o->f);
    return 0;
  }
}

static bool scan_linetype(MGS_Parser *restrict o,
    size_t *restrict found_id, char pos_c) {
  MGS_SymItem *item = scan_sym(o, MGS_SYM_LINE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = MGS_Line_names;
  warning(o, "invalid line type; available are:", pos_c);
  MGS_print_names(names, "\t", stderr);
  return false;
}

static bool scan_noisetype(MGS_Parser *restrict o,
    size_t *restrict found_id, char pos_c) {
  MGS_SymItem *item = scan_sym(o, MGS_SYM_NOISE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = MGS_Noise_names;
  warning(o, "invalid noise type; available are:", pos_c);
  MGS_print_names(names, "\t", stderr);
  return false;
}

static bool scan_wavetype(MGS_Parser *restrict o,
    size_t *restrict found_id, char pos_c) {
  MGS_SymItem *item = scan_sym(o, MGS_SYM_WAVE_ID, pos_c);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = MGS_Wave_names;
  warning(o, "invalid wave type; available are:", pos_c);
  MGS_print_names(names, "\t", stderr);
  return false;
}

static void parse_level(MGS_Parser *o, MGS_ProgramArrData *chain);

static MGS_Program* parse(MGS_File *f, MGS_Parser *o) {
  memset(o, 0, sizeof(MGS_Parser));
  o->f = f;
  MGS_MemPool *mem = MGS_create_MemPool(0);
  MGS_SymTab *symt = MGS_create_SymTab(mem);
  o->prg = MGS_MemPool_alloc(mem, sizeof(MGS_Program));
  o->prg->mem = mem;
  o->prg->symt = symt;
  o->prg->name = f->path;
  MGS_init_LangOpt(&o->prg->lopt, symt);
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

static bool parse_amp(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_Program *p = o->prg;
  MGS_ProgramSoundData *sound;
  sound = MGS_ProgramNode_get_data(n, MGS_BASETYPE_SOUND);
  if (!sound) goto INVALID;
  double f;
  if (MGS_File_TRYC(o->f, '!')) {
    if (!MGS_File_TESTC(o->f, '{')) {
      if (!scan_num(o, NULL, &f)) goto INVALID;
      sound->dynamp = f;
      sound->params |= MGS_SOUNDP_DYNAMP;
    }
    if (MGS_File_TRYC(o->f, '{')) {
      sound->amod = MGS_MemPool_alloc(p->mem, sizeof(MGS_ProgramArrData));
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

static bool parse_channel(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_ProgramSoundData *sound;
  sound = MGS_ProgramNode_get_data(n, MGS_BASETYPE_SOUND);
  if (!sound) goto INVALID;
  MGS_NodeData *nd = o->cur_nd;
  if (nd->target != NULL) {
    MGS_ProgramArrData *target = nd->target;
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

static bool parse_freq(MGS_Parser *o, MGS_ProgramNode *n, bool ratio) {
  MGS_Program *p = o->prg;
  MGS_ProgramWaveData *wod = MGS_ProgramNode_get_data(n, MGS_TYPE_WAVE);
  if (!wod) goto INVALID;
  if (ratio && (n->base_id == wod->sound.root->base_id)) goto INVALID;
  double f;
  if (MGS_File_TRYC(o->f, '!')) {
    if (!MGS_File_TESTC(o->f, '{')) {
      if (!scan_num(o, NULL, &f)) goto INVALID;
      wod->dynfreq = f;
      if (ratio) {
        wod->attr |= MGS_ATTR_DYNFREQRATIO;
      } else {
        wod->attr &= ~MGS_ATTR_DYNFREQRATIO;
      }
      wod->sound.params |= MGS_WAVEP_DYNFREQ | MGS_WAVEP_ATTR;
    }
    if (MGS_File_TRYC(o->f, '{')) {
      wod->fmod = MGS_MemPool_alloc(p->mem, sizeof(MGS_ProgramArrData));
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
    wod->sound.params |= MGS_WAVEP_FREQ | MGS_WAVEP_ATTR;
  }
  return true;
INVALID:
  return false;
}

static bool parse_goal(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_ProgramLineData *lod = MGS_ProgramNode_get_data(n, MGS_TYPE_LINE);
  if (!lod) goto INVALID;
  double f;
  if (!scan_num(o, NULL, &f)) goto INVALID;
  lod->line.vt = f;
  lod->line.flags |= MGS_LINEP_GOAL;
  return true;
INVALID:
  return false;
}

static bool parse_line(MGS_Parser *o, MGS_ProgramNode *n, char pos_c) {
  MGS_ProgramLineData *lod = MGS_ProgramNode_get_data(n, MGS_TYPE_LINE);
  if (!lod) goto INVALID;
  size_t line;
  if (!scan_linetype(o, &line, pos_c)) goto INVALID;
  lod->line.fill_type = line;
  lod->line.flags |= MGS_LINEP_FILL_TYPE;
  return true;
INVALID:
  return false;
}

static bool parse_noise(MGS_Parser *o, MGS_ProgramNode *n, char pos_c) {
  MGS_ProgramNoiseData *nod = MGS_ProgramNode_get_data(n, MGS_TYPE_NOISE);
  if (!nod) goto INVALID;
  size_t noise;
  if (!scan_noisetype(o, &noise, pos_c)) goto INVALID;
  nod->noise = noise;
  nod->sound.params |= MGS_NOISEP_NOISE;
  return true;
INVALID:
  return false;
}

static bool parse_phase(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_Program *p = o->prg;
  MGS_ProgramWaveData *wod = MGS_ProgramNode_get_data(n, MGS_TYPE_WAVE);
  if (!wod) goto INVALID;
  double f;
  if (MGS_File_TRYC(o->f, '!')) {
    if (MGS_File_TRYC(o->f, '{')) {
      wod->pmod = MGS_MemPool_alloc(p->mem, sizeof(MGS_ProgramArrData));
      wod->pmod->mod_type = MGS_MOD_PM;
      parse_level(o, wod->pmod);
    }
  } else {
    if (!scan_num(o, NULL, &f)) goto INVALID;
    wod->phase = lrint(f * UINT32_MAX);
    wod->sound.params |= MGS_WAVEP_PHASE;
  }
  return true;
INVALID:
  return false;
}

static bool parse_time(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_ProgramSoundData *sound = MGS_ProgramNode_get_data(n, MGS_BASETYPE_SOUND);
  if (!sound) goto INVALID;
  double f;
  if (!scan_timeval(o, &f)) goto INVALID;
  sound->time.v = f;
  sound->time.flags |= MGS_TIME_SET;
  MGS_ProgramLineData *lod = MGS_ProgramNode_get_data(n, MGS_TYPE_LINE);
  if (lod != NULL) {
    lod->line.time_ms = lrint(f * 1000.f);
    lod->line.flags |= MGS_LINEP_TIME;
    lod->line.flags &= ~MGS_LINEP_TIME_IF_NEW;
  }
  return true;
INVALID:
  return false;
}

static bool parse_value(MGS_Parser *o, MGS_ProgramNode *n) {
  MGS_ProgramLineData *lod = MGS_ProgramNode_get_data(n, MGS_TYPE_LINE);
  if (!lod) goto INVALID;
  double f;
  if (!scan_num(o, NULL, &f)) goto INVALID;
  lod->line.v0 = f;
  lod->line.flags |= MGS_LINEP_STATE;
  return true;
INVALID:
  return false;
}

static bool parse_wave(MGS_Parser *o, MGS_ProgramNode *n, char pos_c) {
  MGS_ProgramWaveData *wod = MGS_ProgramNode_get_data(n, MGS_TYPE_WAVE);
  if (!wod) goto INVALID;
  size_t wave;
  if (!scan_wavetype(o, &wave, pos_c)) goto INVALID;
  wod->wave = wave;
  wod->sound.params |= MGS_WAVEP_WAVE;
  return true;
INVALID:
  return false;
}

static bool parse_ref(MGS_Parser *o, char pos_c) {
  MGS_NodeData *nd = o->cur_nd;
  if (nd->target != NULL)
    return false;
  MGS_SymItem *sym = scan_sym(o, MGS_SYM_VAR, ':');
  if (!sym)
    return false;
  if (sym->data_use == MGS_SYM_DATA_OBJ) {
    MGS_ProgramNode *ref = sym->data.obj;
    new_node(nd, ref, ref->type);
    sym->data.obj = o->cur_node;
    o->setnode = o->level + 1;
  } else {
    warning(o, "reference doesn't point to an object", pos_c);
  }
  return true;
}

static void parse_level(MGS_Parser *o, MGS_ProgramArrData *chain) {
  char c;
  double f;
  MGS_NodeData nd;
  uint32_t entrylevel = o->level;
  ++o->reclevel;
  MGS_init_NodeData(&nd, o, chain);
  for (;;) {
    c = MGS_File_GETC(o->f);
    MGS_File_skipspace(o->f);
    switch (c) {
    case '\n':
      MGS_File_TRYC(o->f, '\r');
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
      MGS_File_skipspace(o->f);
      break;
    case '#':
      MGS_File_skipline(o->f);
      break;
    case '/':
      if (o->setdef > o->setnode) goto INVALID;
      if (MGS_File_TRYC(o->f, 't')) {
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
      new_node(&nd, NULL, MGS_TYPE_ENV);
      o->setnode = o->level + 1;
      break;
    case 'L':
      if (!scan_num(o, NULL, &f)) break;
      new_node(&nd, NULL, MGS_TYPE_LINE);
      MGS_ProgramLineData *lod = nd.node->data;
      lod->line.v0 = f;
      lod->line.flags |= MGS_LINEP_STATE;
      o->setnode = o->level + 1;
      break;
    case 'N': {
      size_t noise;
      if (!scan_noisetype(o, &noise, c)) break;
      new_node(&nd, NULL, MGS_TYPE_NOISE);
      MGS_ProgramNoiseData *nod = nd.node->data;
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
      new_node(&nd, NULL, MGS_TYPE_WAVE);
      MGS_ProgramWaveData *wod = nd.node->data;
      wod->wave = wave;
      o->setnode = o->level + 1;
      break; }
    case '|': {
      MGS_ProgramDurData *dur = o->cur_dur->data;
      if (!dur->scope.first_node) {
        warning(o, "no sounds precede time separator", c);
        break;
      }
      new_node(&nd, NULL, MGS_TYPE_DUR);
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
      MGS_SymItem *var = nd.set_var;
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
  MGS_fini_NodeData(&nd);
  --o->reclevel;
}

MGS_Program* MGS_create_Program(const char *file, bool is_path) {
  MGS_Program *o = NULL;
  MGS_Parser p;
  MGS_File *f = MGS_create_File();
  if (!f) goto ERROR;
  if (!is_path) {
    if (!MGS_File_stropenrb(f, "<string>", file)) {
      MGS_error(NULL, "NULL string passed for opening");
      goto ERROR;
    }
  } else if (!MGS_File_fopenrb(f, file)) {
    MGS_error(NULL, "couldn't open script file \"%s\" for reading", file);
    goto ERROR;
  }
  o = parse(f, &p);
  if (!o) goto ERROR;
  MGS_adjust_node_list(o->node_list);
ERROR:
  MGS_destroy_File(f);
  return o;
}

void MGS_destroy_Program(MGS_Program *o) {
  if (!o)
    return;
  MGS_destroy_SymTab(o->symt);
  MGS_destroy_MemPool(o->mem);
}
