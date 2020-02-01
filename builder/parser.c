/* mgensys: Script parser.
 * Copyright (c) 2011, 2019-2020 Joel K. Pettersson
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
#include "../program.h"
#include "../math.h"
#include "../help.h"
#include "../loader/file.h"
#include "../loader/symtab.h"
#include <string.h>
#include <stdlib.h>

#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

static uint8_t filter_symchar(MGS_File *restrict f mgsMaybeUnused,
                              uint8_t c) {
  return IS_SYMCHAR(c) ? c : 0;
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
  MGS_ProgramNode *cur_node, *cur_root;
  MGS_ProgramNode *prev_node, *prev_root;
  /* settings/ops */
  uint8_t n_mode;
  float n_ampmult;
  float n_time;
  float n_freq, n_ratio;
} MGS_Parser;

static mgsNoinline void warning(MGS_Parser *o, const char *s, char c) {
  MGS_File *f = o->f;
  if (IS_VISIBLE(c)) {
    fprintf(stderr, "warning: %s [line %d, at '%c'] - %s\n",
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
      MGS_File_UNGETC(o->f);
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
  if (MGS_File_AT_EOF(o->f) ||
      MGS_File_AFTER_EOF(o->f)) {
    return false;
  }
  warning(o, "invalid character", c);
  return true;
}

/* things that need to be separate for each nested parse_level() go here */
typedef struct NodeData {
  MGS_ProgramNode *node; /* state for tentative node until end_node() */
  MGS_ProgramNodeChain *target;
  MGS_ProgramNode *last;
  const char *setsym;
  size_t setsym_len;
  /* timing/delay */
  MGS_ProgramNode *n_begin;
  uint8_t n_end;
  uint8_t n_time_delay;
  float n_add_delay; /* added to node's delay in end_node */
  float n_next_add_delay;
} NodeData;

static void end_node(MGS_Parser *o, NodeData *nd);

static void new_node(MGS_Parser *o, NodeData *nd,
    MGS_ProgramNodeChain *target, MGS_ProgramNode *ref_prev, uint8_t type) {
  MGS_Program *p = o->prg;
  MGS_ProgramNode *n;
  end_node(o, nd);
  n = nd->node = calloc(1, sizeof(MGS_ProgramNode));
  n->ref_prev = ref_prev;
  nd->target = target;
  n->type = type;

  /* IDs and linking */
  o->prev_node = o->cur_node;
  o->prev_root = o->cur_root;
  n->id = p->node_count;
  ++p->node_count;
  if (!p->node_list)
    p->node_list = n;
  else
    o->cur_node->next = n;
  o->cur_node = n;
  if (!target) {
    if (!ref_prev) {
      n->root_id = n->id;
      ++p->root_count;
      n->type_id = p->type_counts[type]++;
      o->cur_root = n;
    } else {
      n->root_id = ref_prev->root_id;
      n->type_id = ref_prev->type_id;
    }
  } else {
    n->root_id = o->cur_root->id;
    if (!ref_prev) {
      n->type_id = p->type_counts[type]++;
    } else {
      n->type_id = ref_prev->type_id;
    }
    if (!target->chain)
      target->chain = n;
    else
      nd->last->nested_next = n;
    nd->last = n;
    ++target->count;
  }

  /* defaults */
  n->amp = 1.f;
  n->mode = o->n_mode;
  if (!target)
    n->time = -1.f; /* set later */
  else
    n->time = o->n_time;
  n->freq = o->n_freq;
  if (ref_prev != NULL) {
    /* time is not copied */
    n->wave = ref_prev->wave;
    n->mode = ref_prev->mode;
    n->amp = ref_prev->amp;
    n->dynamp = ref_prev->dynamp;
    n->freq = ref_prev->freq;
    n->dynfreq = ref_prev->dynfreq;
    n->attr = ref_prev->attr;
    n->pmod = ref_prev->pmod;
    n->fmod = ref_prev->fmod;
    n->amod = ref_prev->amod;
  }

  /* prepare timing adjustment */
  nd->n_add_delay += nd->n_next_add_delay;
  if (nd->n_time_delay) {
    if (o->prev_root)
      nd->n_add_delay += o->prev_root->time;
    nd->n_time_delay = 0;
  }
  nd->n_next_add_delay = 0.f;
}

static void end_node(MGS_Parser *o, NodeData *nd) {
  MGS_Program *p = o->prg;
  MGS_ProgramNode *n = nd->node;
  if (!n)
    return; /* nothing to do */
  nd->node = 0;
  if (!n->ref_prev) {
    n->params |= MGS_PARAM_MASK & ~MGS_MODS_MASK;
  } else {
    /* check what the set-node changes */
    MGS_ProgramNode *ref = n->ref_prev;
    /* MGS_TIME set when time set */
    if (n->wave != ref->wave)
      n->params |= MGS_WAVE;
    if (n->freq != ref->freq)
      n->params |= MGS_FREQ;
    if (n->dynfreq != ref->dynfreq)
      n->params |= MGS_DYNFREQ;
    if (n->phase != ref->phase)
      n->params |= MGS_PHASE;
    if (n->amp != ref->amp)
      n->params |= MGS_AMP;
    if (n->dynamp != ref->dynamp)
      n->params |= MGS_DYNAMP;
    if (n->attr != ref->attr)
      n->params |= MGS_ATTR;
    if (n->amod.chain != ref->amod.chain)
      n->params |= MGS_AMODS;
    if (n->fmod.chain != ref->fmod.chain)
      n->params |= MGS_FMODS;
    if (n->pmod.chain != ref->pmod.chain)
      n->params |= MGS_PMODS;
  }

  if (!nd->target) /* only apply to root operator */
    n->amp *= o->n_ampmult;
  /* node-to-| sequence timing */
  if (!nd->n_begin)
    nd->n_begin = n;
  else if (nd->n_end) {
    double delay = 0.f, delaycount = 0.f;
    MGS_ProgramNode *step;
    for (step = nd->n_begin; step != n; step = step->next) {
      if (step->next == n && step->time < 0.f)
        step->time = o->n_time; /* set and use default for last node in group */
      if (delay < step->time)
        delay = step->time;
      delay -= step->next->delay;
      delaycount += step->next->delay;
    }
    for (step = nd->n_begin; step != n; step = step->next) {
      if (step->time < 0.f)
        step->time = delay + delaycount; /* fill in sensible default time */
      delaycount -= step->next->delay;
    }
    nd->n_add_delay += delay;
    nd->n_begin = n;
    nd->n_end = 0;
  }
  n->delay += nd->n_add_delay;
  nd->n_add_delay = 0.f;

  if (nd->setsym) {
    MGS_SymTab_set(p->symtab, nd->setsym, n);
    nd->setsym = NULL;
    nd->setsym_len = 0;
  }
}

typedef float (*NumSym_f)(MGS_Parser *restrict o);

typedef struct NumParser {
  MGS_Parser *pr;
  NumSym_f numsym_f;
  bool has_infnum;
} NumParser;
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
  MGS_Parser *pr = o->pr;
  double num;
  bool minus = false;
  uint8_t c;
  if (level > 0) skip_ws(pr);
  c = MGS_File_GETC(pr->f);
  if ((level > 0) && (c == '+' || c == '-')) {
    if (c == '-') minus = true;
    skip_ws(pr);
    c = MGS_File_GETC(pr->f);
  }
  if (c == '(') {
    num = scan_num_r(o, 255, level+1);
    if (minus) num = -num;
    if (level == 0) return num;
    goto EVAL;
  }
  if (o->numsym_f && IS_ALPHA(c)) {
    MGS_File_UNGETC(pr->f);
    num = o->numsym_f(pr);
    if (isnan(num))
      return NAN;
    if (minus) num = -num;
  } else {
    size_t read_len;
    MGS_File_UNGETC(pr->f);
    MGS_File_getd(pr->f, &num, false, &read_len);
    if (read_len == 0)
      return NAN;
    if (minus) num = -num;
  }
EVAL:
  if (pri == 0)
    return num; /* defer all */
  for (;;) {
    if (isinf(num)) o->has_infnum = true;
    if (level > 0) skip_ws(pr);
    c = MGS_File_GETC(pr->f);
    switch (c) {
    case '(':
      num *= scan_num_r(o, 255, level+1);
      break;
    case ')':
      if (pri < 255) goto DEFER;
      return num;
    case '^':
      num = exp(log(num) * scan_num_r(o, 0, level));
      break;
    case '*':
      num *= scan_num_r(o, 1, level);
      break;
    case '/':
      num /= scan_num_r(o, 1, level);
      break;
    case '+':
      if (pri < 2) goto DEFER;
      num += scan_num_r(o, 2, level);
      break;
    case '-':
      if (pri < 2) goto DEFER;
      num -= scan_num_r(o, 2, level);
      break;
    default:
      if (pri == 255) {
        warning(pr, "numerical expression has '(' without closing ')'", c);
      }
      goto DEFER;
    }
    if (isnan(num)) goto DEFER;
  }
DEFER:
  MGS_File_UNGETC(pr->f);
  return num;
}
static mgsNoinline bool scan_num(MGS_Parser *restrict o,
    NumSym_f scan_numsym, float *restrict var) {
  NumParser np = {o, scan_numsym, false};
  float num = scan_num_r(&np, 0, 0);
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

static int32_t strfind(MGS_File *restrict f, const char *const*restrict str) {
  int32_t ret;
  size_t i, len, pos, matchpos;
  size_t strc;
  const char **s;
  for (len = 0, strc = 0; str[strc]; ++strc)
    if ((i = strlen(str[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = str[i];
  ret = -1;
  pos = matchpos = 0;
  for (;;) {
    uint8_t c = MGS_File_GETC(f);
    for (i = 0; i < strc; ++i) {
      if (!s[i]) continue;
      else if (!s[i][pos]) {
        s[i] = 0;
        ret = i;
        matchpos = pos-1;
      } else if (c != s[i][pos]) {
        s[i] = 0;
      }
    }
    if (c <= MGS_FILE_MARKER) break;
    if (pos == len) break;
    ++pos;
  }
  free(s);
  MGS_File_UNGETN(f, (pos-matchpos));
  return ret;
}

static int32_t scan_wavetype(MGS_Parser *restrict o, char from_c) {
  int32_t wave = strfind(o->f, MGS_Wave_names);
  if (wave < 0) {
    warning(o, "invalid wave type; available types are:", from_c);
    MGS_print_names(MGS_Wave_names, "\t", stderr);
  }
  return wave;
}

#define SYMKEY_MAXLEN 79
#define SYMKEY_MAXLEN_A "79"
static const char *scan_sym(MGS_Parser *o, size_t *len, char op) {
  char nosym_msg[] = "ignoring ? without symbol name";
  size_t read_len = 0;
  bool truncated;
  nosym_msg[9] = op; /* replace ? */
  truncated = !MGS_File_getstr(o->f, o->symbuf, SYMKEY_MAXLEN + 1,
      &read_len, filter_symchar);
  if (read_len == 0) {
    warning(o, nosym_msg, op);
    return NULL;
  }
  if (len != NULL)
    *len = read_len;
  if (truncated) {
    warning(o, "limiting symbol name to "SYMKEY_MAXLEN_A" characters", op);
    read_len += MGS_File_skipstr(o->f, filter_symchar);
  }
  return o->symbuf;
}

static void parse_level(MGS_Parser *o, MGS_ProgramNodeChain *chain, uint8_t modtype);

static MGS_Program* parse(MGS_File *f, MGS_Parser *o) {
  memset(o, 0, sizeof(MGS_Parser));
  o->f = f;
  o->prg = calloc(1, sizeof(MGS_Program));
  o->prg->symtab = MGS_create_SymTab();
  o->symbuf = calloc(SYMKEY_MAXLEN + 1, sizeof(char));
  o->line = 1;
  o->n_mode = MGS_MODE_CENTER; /* default until changed */
  o->n_ampmult = 1.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_ratio = 1.f; /* default until changed */
  parse_level(o, 0, 0);
  free(o->symbuf);
  return o->prg;
}

static void parse_level(MGS_Parser *o, MGS_ProgramNodeChain *chain, uint8_t modtype) {
  char c;
  float f;
  NodeData nd;
  uint32_t entrylevel = o->level;
  ++o->reclevel;
  memset(&nd, 0, sizeof(NodeData));
  if (chain) {
    chain->count = 0;
    chain->chain = 0;
  }
  for (;;) {
    c = MGS_File_GETC(o->f);
    MGS_File_skipspace(o->f);
    switch (c) {
    case '\n':
      MGS_File_TRYC(o->f, '\r');
      /* fall-through */
    case '\r':
      if (!chain) {
        if (o->setdef > o->level)
          o->setdef = (o->level) ? (o->level - 1) : 0;
        else if (o->setnode > o->level) {
          o->setnode = (o->level) ? (o->level - 1) : 0;
          end_node(o, &nd);
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
      if (!scan_num(o, NULL, &f)) goto INVALID;
      nd.n_time_delay = 0;
      nd.n_next_add_delay += f;
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '}':
      if (!chain)
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
        end_node(o, &nd);
      }
      --o->level;
      break;
    case 'C':
      o->n_mode = MGS_MODE_CENTER;
      break;
    case 'E':
      new_node(o, &nd, 0, NULL, MGS_TYPE_ENV);
      o->setnode = o->level + 1;
      break;
    case 'L':
      o->n_mode = MGS_MODE_LEFT;
      break;
    case 'Q':
      goto FINISH;
    case 'R':
      o->n_mode = MGS_MODE_RIGHT;
      break;
    case 'S':
      o->setdef = o->level + 1;
      break;
    case 'W': {
      int wave = scan_wavetype(o, c);
      if (wave < 0) break;
      new_node(o, &nd, chain, NULL, MGS_TYPE_OP);
      nd.node->wave = wave;
      o->setnode = o->level + 1;
      break; }
    case '|':
      end_node(o, &nd);
      if (!nd.n_begin)
        warning(o, "end of sequence before any parts given", c);
      else
        nd.n_end = 1;
      break;
    case '\\':
      if (o->setdef > o->setnode)
        goto INVALID;
      if (!scan_num(o, NULL, &f)) goto INVALID;
      nd.node->delay += f;
      break;
    case '\'':
      end_node(o, &nd);
      if (nd.setsym) {
        warning(o, "ignoring label assignment to label assignment", c);
        break;
      }
      nd.setsym = scan_sym(o, &nd.setsym_len, '\'');
      break;
    case ':':
      end_node(o, &nd);
      if (nd.setsym)
        warning(o, "ignoring label assignment to label reference", c);
      else if (chain)
        goto INVALID;
      nd.setsym = scan_sym(o, &nd.setsym_len, ':');
      if (nd.setsym != NULL) {
        MGS_ProgramNode *ref = MGS_SymTab_get(o->prg->symtab, nd.setsym);
        if (!ref)
          warning(o, "ignoring reference to undefined label", c);
        else {
          new_node(o, &nd, 0, ref, ref->type);
          o->setnode = o->level + 1;
        }
      }
      break;
    case 'a':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_ampmult = f;
      } else if (o->setnode > 0) {
        if (modtype == MGS_AMODS ||
            modtype == MGS_FMODS)
          goto INVALID;
        if (MGS_File_TRYC(o->f, '!')) {
          if (!MGS_File_TESTC(o->f, '{')) {
            if (!scan_num(o, NULL, &f)) goto INVALID;
            nd.node->dynamp = f;
          }
          if (MGS_File_TRYC(o->f, '{')) {
            parse_level(o, &nd.node->amod, MGS_AMODS);
          }
        } else {
          if (!scan_num(o, NULL, &f)) goto INVALID;
          nd.node->amp = f;
        }
      } else
        goto INVALID;
      break;
    case 'f':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_freq = f;
      } else if (o->setnode > 0) {
        if (MGS_File_TRYC(o->f, '!')) {
          if (!MGS_File_TESTC(o->f, '{')) {
            if (!scan_num(o, NULL, &f)) goto INVALID;
            nd.node->dynfreq = f;
            nd.node->attr &= ~MGS_ATTR_DYNFREQRATIO;
          }
          if (MGS_File_TRYC(o->f, '{')) {
            parse_level(o, &nd.node->fmod, MGS_FMODS);
          }
        } else {
          if (!scan_num(o, NULL, &f)) goto INVALID;
          nd.node->freq = f;
          nd.node->attr &= ~MGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 'p': {
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (MGS_File_TRYC(o->f, '!')) {
        if (MGS_File_TRYC(o->f, '{')) {
          parse_level(o, &nd.node->pmod, MGS_PMODS);
        }
      } else {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        nd.node->phase = fmod(f, 1.f);
        if (nd.node->phase < 0.f)
          nd.node->phase += 1.f;
      }
      break; }
    case 'r':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_ratio = 1.f / f;
      } else if (o->setnode > 0) {
        if (!chain)
          goto INVALID;
        if (MGS_File_TRYC(o->f, '!')) {
          if (!MGS_File_TESTC(o->f, '{')) {
            if (!scan_num(o, NULL, &f)) goto INVALID;
            nd.node->dynfreq = 1.f / f;
            nd.node->attr |= MGS_ATTR_DYNFREQRATIO;
          }
          if (MGS_File_TRYC(o->f, '{')) {
            parse_level(o, &nd.node->fmod, MGS_FMODS);
          }
        } else {
          if (!scan_num(o, NULL, &f)) goto INVALID;
          nd.node->freq = 1.f / f;
          nd.node->attr |= MGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        o->n_time = f;
      } else if (o->setnode > 0) {
        if (!scan_num(o, NULL, &f)) goto INVALID;
        nd.node->time = f;
        nd.node->params |= MGS_TIME;
      } else
        goto INVALID;
      break;
    case 'w': {
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      int wave = scan_wavetype(o, c);
      if (wave < 0) break;
      nd.node->wave = wave;
      break; }
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
  if (nd.node) {
    if (nd.node->time < 0.f)
      nd.node->time = o->n_time; /* use default */
    nd.n_end = 1; /* end grouping if any */
    end_node(o, &nd);
  }
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
ERROR:
  MGS_destroy_File(f);
  return o;
}

void MGS_destroy_Program(MGS_Program *o) {
  if (!o)
    return;
  MGS_ProgramNode *n = o->node_list;
  while (n) {
    MGS_ProgramNode *nn = n->next;
    free(n);
    n = nn;
  }
  MGS_destroy_SymTab(o->symtab);
}
