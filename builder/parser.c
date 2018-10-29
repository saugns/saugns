/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#include "symtab.h"
#include "../script.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * General-purpose functions
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

#define IS_WHITESPACE(c) \
  (IS_SPACE(c) || (c) == '\n' || (c) == '\r')

static bool retc(FILE *f) {
  char gc = getc(f);
  ungetc(gc, f);
  return gc;
}

static bool testc(char c, FILE *f) {
  char gc = getc(f);
  ungetc(gc, f);
  return (gc == c);
}

static bool tryc(char c, FILE *f) {
  char gc;
  if ((gc = getc(f)) == c) return true;
  ungetc(gc, f);
  return false;
}

static bool readsym(FILE *f, char *buf, uint32_t buf_len,
                    uint32_t *sym_len) {
  uint32_t i = 0;
  uint32_t max_len = buf_len - 1;
  bool truncate = false;
  for (;;) {
    if (i == max_len) {
      truncate = true;
      break;
    }
    uint8_t c = getc(f);
    if (!IS_SYMCHAR(c)) {
      ungetc(c, f);
      break;
    }
    buf[i++] = c;
  }
  buf[i] = '\0';
  *sym_len = i;
  return !truncate;
}

static int32_t getinum(FILE *f) {
  char c;
  int32_t num = -1;
  c = getc(f);
  if (IS_DIGIT(c)) {
    num = c - '0';
    for (;;) {
      c = getc(f);
      if (IS_DIGIT(c))
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  ungetc(c, f);
  return num;
}

static int32_t strfind(FILE *f, const char *const*str) {
  int32_t ret;
  uint32_t i, len, pos, matchpos;
  char c, undo[256];
  uint32_t strc;
  const char **s;
  for (len = 0, strc = 0; str[strc]; ++strc)
    if ((i = strlen(str[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = str[i];
  ret = -1;
  pos = matchpos = 0;
  while ((c = getc(f)) != EOF) {
    undo[pos] = c;
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
    if (pos == len) break;
    ++pos;
  }
  free(s);
  for (i = pos; i > matchpos; --i) ungetc(undo[i], f);
  return ret;
}

static void eatws(FILE *f) {
  char c;
  do {
    c = getc(f);
  } while (IS_SPACE(c));
  ungetc(c, f);
}

/*
 * Parser
 */

typedef struct SGS_Parser {
  FILE *f;
  const char *fn;
  SGS_SymTab *st;
  uint32_t line;
  uint32_t calllevel;
  uint32_t scopeid;
  char c, nextc;
  SGS_ScriptOptions sopt;
  /* node state */
  SGS_ScriptEvData *events;
  SGS_ScriptEvData *last_event;
  SGS_ScriptEvData *group_start, *group_end;
} SGS_Parser;

/*
 * Default script options, used until changed in a script.
 */
static const SGS_ScriptOptions def_sopt = {
  .changed = 0,
  .ampmult = 1.f,
  .A4_freq = 440.f,
  .def_time_ms = 1000,
  .def_freq = 440.f,
  .def_ratio = 1.f,
};

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static void init_parser(SGS_Parser *o) {
  *o = (SGS_Parser){0};
  o->st = SGS_create_SymTab();
  o->sopt = def_sopt;
}

/*
 * Finalize parser instance.
 */
static void fini_parser(SGS_Parser *o) {
  SGS_destroy_SymTab(o->st);
}

enum {
  /* parsing scopes */
  SCOPE_SAME = 0,
  SCOPE_TOP = 1,
  SCOPE_BIND,
  SCOPE_NEST,
};

enum {
  SDPL_IN_DEFAULTS = 1<<0, /* adjusting default values */
  SDPL_IN_NODE = 1<<1,     /* adjusting operator and/or voice */
  SDPL_NESTED_SCOPE = 1<<2,
  SDPL_NEW_EVENT_FORK = 1<<3,
  SDPL_BIND_MULTIPLE = 1<<4, /* previous node interpreted as set of nodes */
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
typedef struct ParseLevel {
  SGS_Parser *o;
  struct ParseLevel *parent;
  uint32_t pl_flags;
  uint8_t scope;
  SGS_ScriptEvData *event, *last_event;
  SGS_ScriptOpData *operator, *first_operator, *last_operator;
  SGS_ScriptOpData *parent_on, *on_prev;
  uint8_t linktype;
  uint8_t last_linktype; /* FIXME: kludge */
  const char *set_label; /* label assigned to next node */
  /* timing/delay */
  SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

typedef struct SGS_ScriptEvBranch {
  SGS_ScriptEvData *events;
  struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

#define NEWLINE '\n'
static char read_char(SGS_Parser *o) {
  char c;
  eatws(o->f);
  if (o->nextc != 0) {
    c = o->nextc;
    o->nextc = 0;
  } else {
    c = getc(o->f);
  }
  if (c == '#')
    while ((c = getc(o->f)) != '\n' && c != '\r' && c != EOF) ;
  if (c == '\n') {
    tryc('\r', o->f);
    c = NEWLINE;
  } else if (c == '\r') {
    c = NEWLINE;
  } else {
    eatws(o->f);
  }
  o->c = c;
  return c;
}

static void read_ws(SGS_Parser *o) {
  char c;
  do {
    c = getc(o->f);
    if (IS_SPACE(c))
      continue;
    if (c == '\n') {
      ++o->line;
      tryc('\r', o->f);
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      while ((c = getc(o->f)) != '\n' && c != '\r' && c != EOF) ;
    } else {
      ungetc(c, o->f);
      break;
    }
  } while (c != EOF);
}

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void warning(SGS_Parser *o, const char *str) {
  char buf[4] = {'\'', o->c, '\'', 0};
  fprintf(stderr, "warning: %s [line %d, at %s] - %s\n",
          o->fn, o->line, (o->c == EOF ? "EOF" : buf), str);
}
#define WARN_INVALID "invalid character"

typedef float (*ReadSym_f)(SGS_Parser *o);

typedef struct NumParser {
  SGS_Parser *pr;
  ReadSym_f read_sym_f;
  bool after_rpar;
  char buf[64];
} NumParser;
enum {
  NUMEXP_SUB = 0,
  NUMEXP_ADT,
  NUMEXP_MLT,
  NUMEXP_POW,
  NUMEXP_NUM,
};
static double read_num_r(NumParser *o, uint8_t pri, uint32_t level) {
  SGS_Parser *pr = o->pr;
  bool dot = false;
  double num;
  char c;
  if (level > 0) read_ws(pr);
  c = getc(pr->f);
  if (c == '(') {
    num = read_num_r(o, NUMEXP_SUB, level+1);
  } else if (c == '+' || c == '-') {
    num = read_num_r(o, NUMEXP_ADT, level+1);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (o->read_sym_f && IS_ALPHA(c)) {
    ungetc(c, pr->f);
    num = o->read_sym_f(pr);
    if (isnan(num)) goto REJECT;
  } else {
    char *p = o->buf;
    const size_t len = 64;
    while (IS_DIGIT(c) || (!dot && (dot = (c == '.')))) {
      if ((p+1) == (o->buf+len)) {
        break;
      }
      *p++ = c;
      c = getc(pr->f);
    }
    ungetc(c, pr->f);
    if (p == o->buf) goto REJECT;
    *p = '\0';
    num = strtod(o->buf, 0);
  }
  if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
  for (;;) {
    bool rpar_mlt = false;
    if (level > 0) read_ws(pr);
    c = getc(pr->f);
    if (pri < NUMEXP_MLT) {
      rpar_mlt = o->after_rpar;
      o->after_rpar = false;
    }
    switch (c) {
    case '(':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num *= read_num_r(o, NUMEXP_SUB, level+1);
      break;
    case ')':
      if (pri != NUMEXP_SUB) goto DEFER;
      o->after_rpar = true;
      goto ACCEPT;
    case '^':
      if (pri > NUMEXP_POW) goto DEFER;
      num = pow(num, read_num_r(o, NUMEXP_POW, level));
      break;
    case '*':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num *= read_num_r(o, NUMEXP_MLT, level);
      break;
    case '/':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num /= read_num_r(o, NUMEXP_MLT, level);
      break;
    case '%':
      if (pri >= NUMEXP_MLT) goto DEFER;
      num = fmod(num, read_num_r(o, NUMEXP_MLT, level));
      break;
    case '+':
      if (pri >= NUMEXP_ADT) goto DEFER;
      num += read_num_r(o, NUMEXP_ADT, level);
      break;
    case '-':
      if (pri >= NUMEXP_ADT) goto DEFER;
      num -= read_num_r(o, NUMEXP_ADT, level);
      break;
    default:
      if (rpar_mlt && !IS_WHITESPACE(c)) {
        ungetc(c, pr->f);
        double rval = read_num_r(o, NUMEXP_MLT, level);
        if (isnan(rval)) goto ACCEPT;
        num *= rval;
        break;
      }
      if (pri == NUMEXP_SUB && level > 0) {
        warning(pr, "numerical expression has '(' without closing ')'");
      }
      goto DEFER;
    }
    if (isnan(num)) goto DEFER;
  }
DEFER:
  ungetc(c, pr->f);
ACCEPT:
  if (0)
REJECT: {
    num = NAN;
  }
  return num;
}
static bool read_num(SGS_Parser *o, ReadSym_f read_symbol, float *var) {
  NumParser np = {o, read_symbol, false, {0}};
  float num = read_num_r(&np, NUMEXP_SUB, 0);
  if (isnan(num))
    return false;
  *var = num;
  return true;
}

#define OCTAVES 11
static float read_note(SGS_Parser *o) {
  static const float octaves[OCTAVES] = {
    (1.f/16.f),
    (1.f/8.f),
    (1.f/4.f),
    (1.f/2.f),
    1.f, /* no. 4 - standard tuning here */
    2.f,
    4.f,
    8.f,
    16.f,
    32.f,
    64.f
  };
  static const float notes[3][8] = {
    { /* flat */
      48.f/25.f,
      16.f/15.f,
      6.f/5.f,
      32.f/25.f,
      36.f/25.f,
      8.f/5.f,
      9.f/5.f,
      96.f/25.f
    },
    { /* normal (9/8 replaced with 10/9 for symmetry) */
      1.f,
      10.f/9.f,
      5.f/4.f,
      4.f/3.f,
      3.f/2.f,
      5.f/3.f,
      15.f/8.f,
      2.f
    },
    { /* sharp */
      25.f/24.f,
      75.f/64.f,
      125.f/96.f,
      25.f/18.f,
      25.f/16.f,
      225.f/128.f,
      125.f/64.f,
      25.f/12.f
    }
  };
  float freq;
  o->c = getc(o->f);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  if (o->c >= 'a' && o->c <= 'g') {
    subnote = o->c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    o->c = getc(o->f);
  }
  if (o->c < 'A' || o->c > 'G') {
    warning(o, "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = o->c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  o->c = getc(o->f);
  if (o->c == 's')
    semitone = 2;
  else if (o->c == 'f')
    semitone = 0;
  else
    ungetc(o->c, o->f);
  octave = getinum(o->f);
  if (octave < 0) /* none given, default to 4 */
    octave = 4;
  else if (octave >= OCTAVES) {
    warning(o, "invalid octave specified for note - valid range 0-10");
    octave = 4;
  }
  freq = o->sopt.A4_freq * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
}

#define LABEL_LEN 80
#define LABEL_LEN_A "80"
typedef char LabelBuf[LABEL_LEN];
static uint32_t read_label(SGS_Parser *o, LabelBuf label, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  uint32_t len = 0;
  bool error;
  nolabel_msg[9] = op; /* replace ? */
  error = !readsym(o->f, label, LABEL_LEN, &len);
  o->c = retc(o->f);
  if (len == 0) {
    warning(o, nolabel_msg);
  }
  if (error) {
    warning(o, "ignoring label name from "LABEL_LEN_A"th character");
  }
  return len;
}

static int32_t read_wavetype(SGS_Parser *o) {
  int32_t wave = strfind(o->f, SGS_Wave_names);
  if (wave < 0) {
    warning(o, "invalid wave type; available types are:");
    uint8_t i = 0;
    fprintf(stderr, "\t%s", SGS_Wave_names[i]);
    while (++i < SGS_WAVE_TYPES) {
      fprintf(stderr, ", %s", SGS_Wave_names[i]);
    }
    putc('\n', stderr);
  }
  return wave;
}

static bool read_valit(SGS_Parser *o, ReadSym_f read_symbol,
                        SGS_ProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  char c;
  bool goal = false;
  int32_t type;
  vi->time_ms = SGS_TIME_DEFAULT;
  vi->type = SGS_VALIT_LIN; /* default */
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case NEWLINE:
      ++o->line;
      break;
    case 'c':
      type = strfind(o->f, valittypes);
      if (type >= 0) {
        vi->type = type + SGS_VALIT_LIN;
        break;
      }
      goto INVALID;
    case 't': {
      float time;
      if (read_num(o, 0, &time)) {
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        vi->time_ms = lrint(time * 1000.f);
      }
      break; }
    case 'v':
      if (read_num(o, read_symbol, &vi->goal))
        goal = true;
      break;
    case '}':
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID);
      break;
    }
  }
  warning(o, "end of file without closing '}'");
RETURN:
  if (!goal) {
    warning(o, "ignoring gradual parameter change with no target value");
    vi->type = SGS_VALIT_NONE;
    return false;
  }
  return true;
}

static bool parse_waittime(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  /* FIXME: ADD_WAIT_DURATION */
  if (tryc('t', o->f)) {
    if (!pl->last_operator) {
      warning(o, "add wait for last duration before any parts given");
      return false;
    }
    pl->last_event->ev_flags |= SGS_SDEV_ADD_WAIT_DURATION;
  } else {
    float wait;
    uint32_t wait_ms;
    read_num(o, 0, &wait);
    if (wait < 0.f) {
      warning(o, "ignoring '\\' with sub-zero time");
      return false;
    }
    wait_ms = lrint(wait * 1000.f);
    pl->next_wait_ms += wait_ms;
  }
  return true;
}

/*
 * Node- and scope-handling functions
 */

enum {
  /* node list/node link types */
  NL_REFER = 0,
  NL_GRAPH,
  NL_FMODS,
  NL_PMODS,
  NL_AMODS,
};

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SGS_ScriptOpData *op) {
  SGS_PtrList_clear(&op->on_next);
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->fmods);
  for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrList_clear(&op->fmods);
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->pmods);
  for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrList_clear(&op->pmods);
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->amods);
  for (i = op->amods.old_count; i < op->amods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrList_clear(&op->amods);
  free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SGS_ScriptEvData *e) {
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrList_clear(&e->operators);
  SGS_PtrList_clear(&e->op_graph);
  free(e);
}

static void end_operator(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  SGS_ScriptOpData *op = pl->operator;
  if (!op)
    return; /* nothing to do */
  if (!op->on_prev) { /* initial event should reset its parameters */
    op->op_params |= SGS_POPP_ADJCS |
                     SGS_POPP_WAVE |
                     SGS_POPP_TIME |
                     SGS_POPP_SILENCE |
                     SGS_POPP_FREQ |
                     SGS_POPP_DYNFREQ |
                     SGS_POPP_PHASE |
                     SGS_POPP_AMP |
                     SGS_POPP_DYNAMP |
                     SGS_POPP_ATTR;
  } else {
    SGS_ScriptOpData *pop = op->on_prev;
    if (op->attr != pop->attr)
      op->op_params |= SGS_POPP_ATTR;
    if (op->wave != pop->wave)
      op->op_params |= SGS_POPP_WAVE;
    /* SGS_TIME set when time set */
    if (op->silence_ms != 0)
      op->op_params |= SGS_POPP_SILENCE;
    /* SGS_FREQ set when freq set */
    if (op->dynfreq != pop->dynfreq)
      op->op_params |= SGS_POPP_DYNFREQ;
    /* SGS_PHASE set when phase set */
    /* SGS_AMP set when amp set */
    if (op->dynamp != pop->dynamp)
      op->op_params |= SGS_POPP_DYNAMP;
  }
  if (op->valitfreq.type != SGS_VALIT_NONE)
    op->op_params |= SGS_POPP_ATTR |
                     SGS_POPP_VALITFREQ;
  if (op->valitamp.type != SGS_VALIT_NONE)
    op->op_params |= SGS_POPP_ATTR |
                     SGS_POPP_VALITAMP;
  if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
    op->amp *= o->sopt.ampmult;
    op->valitamp.goal *= o->sopt.ampmult;
  }
  pl->operator = NULL;
  pl->last_operator = op;
}

static void end_event(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptEvData *pve;
  if (!e)
    return; /* nothing to do */
  end_operator(pl);
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
    e->vo_params |= SGS_PVOP_ATTR |
                    SGS_PVOP_PANNING;
  } else {
    if (e->panning != pve->panning)
      e->vo_params |= SGS_PVOP_PANNING;
  }
  if (e->valitpanning.type != SGS_VALIT_NONE)
    e->vo_params |= SGS_PVOP_ATTR |
                    SGS_PVOP_VALITPANNING;
  pl->last_event = e;
  pl->event = NULL;
  SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
  if (!o->group_start)
    o->group_start = group_e;
  o->group_end = group_e;
}

static void begin_event(ParseLevel *pl, uint8_t linktype,
                        bool is_compstep) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e, *pve;
  end_event(pl);
  pl->event = calloc(1, sizeof(SGS_ScriptEvData));
  e = pl->event;
  e->wait_ms = pl->next_wait_ms;
  pl->next_wait_ms = 0;
  if (pl->on_prev != NULL) {
    SGS_ScriptEvBranch *fork;
    pve = pl->on_prev->event;
    pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
    fork = pve->forks;
    if (is_compstep) {
      if (pl->pl_flags & SDPL_NEW_EVENT_FORK) {
        if (!pl->main_ev)
          pl->main_ev = pve;
        else
          fork = pl->main_ev->forks;
        pl->main_ev->forks = calloc(1, sizeof(SGS_ScriptEvBranch));
        pl->main_ev->forks->events = e;
        pl->main_ev->forks->prev = fork;
        pl->pl_flags &= ~SDPL_NEW_EVENT_FORK;
      } else {
        pve->next = e;
      }
    } else while (fork != NULL) {
      SGS_ScriptEvData *last_ce;
      for (last_ce = fork->events; last_ce->next; last_ce = last_ce->next) ;
      last_ce->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
      fork = fork->prev;
    }
    e->voice_prev = pve;
    e->vo_attr = pve->vo_attr;
    e->panning = pve->panning;
    e->valitpanning = pve->valitpanning;
  } else { /* set defaults */
    e->panning = 0.5f; /* center */
  }
  if (!is_compstep) {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    pl->main_ev = NULL;
  }
}

static void begin_operator(ParseLevel *pl, uint8_t linktype,
                           bool is_compstep) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptOpData *op, *pop = pl->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(pl);
  pl->operator = calloc(1, sizeof(SGS_ScriptOpData));
  op = pl->operator;
  if (!pl->first_operator)
    pl->first_operator = op;
  if (!is_compstep && pl->last_operator != NULL)
    pl->last_operator->next_bound = op;
  if (!is_compstep)
    pl->pl_flags |= SDPL_NEW_EVENT_FORK;
  /*
   * Initialize node.
   */
  if (pop != NULL) {
    pop->op_flags |= SGS_SDOP_LATER_USED;
    op->on_prev = pop;
    op->op_flags = pop->op_flags & (SGS_SDOP_NESTED |
                                    SGS_SDOP_MULTIPLE);
    if (is_compstep) {
      pop->op_flags |= SGS_SDOP_HAS_COMPSTEP;
      op->op_flags |= SGS_SDOP_TIME_DEFAULT; /* default: previous or infinite time */
    }
    op->attr = pop->attr;
    op->wave = pop->wave;
    op->time_ms = pop->time_ms;
    op->freq = pop->freq;
    op->dynfreq = pop->dynfreq;
    op->phase = pop->phase;
    op->amp = pop->amp;
    op->dynamp = pop->dynamp;
    op->valitfreq = pop->valitfreq;
    op->valitamp = pop->valitamp;
    SGS_PtrList_soft_copy(&op->fmods, &pop->fmods);
    SGS_PtrList_soft_copy(&op->pmods, &pop->pmods);
    SGS_PtrList_soft_copy(&op->amods, &pop->amods);
    if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
      SGS_ScriptOpData *mpop = pop;
      uint32_t max_time = 0;
      do {
        if (max_time < mpop->time_ms) max_time = mpop->time_ms;
        SGS_PtrList_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound) != NULL);
      op->op_flags |= SGS_SDOP_MULTIPLE;
      op->time_ms = max_time;
      pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
    } else {
      SGS_PtrList_add(&pop->on_next, op);
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->op_flags = SGS_SDOP_TIME_DEFAULT; /* default: depends on context */
    op->time_ms = o->sopt.def_time_ms;
    op->amp = 1.0f;
    if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
      op->freq = o->sopt.def_freq;
    } else {
      op->op_flags |= SGS_SDOP_NESTED;
      op->freq = o->sopt.def_ratio;
      op->attr |= SGS_POPA_FREQRATIO;
    }
  }
  op->event = e;
  /*
   * Add new operator to parent(s), ie. either the current event node, or an
   * operator node (either ordinary or representing multiple carriers) in the
   * case of operator linking/nesting.
   */
  if (linktype == NL_REFER ||
      linktype == NL_GRAPH) {
    SGS_PtrList_add(&e->operators, op);
    if (linktype == NL_GRAPH) {
      e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
      SGS_PtrList_add(&e->op_graph, op);
    }
  } else {
    SGS_PtrList *list = NULL;
    switch (linktype) {
    case NL_FMODS:
      list = &pl->parent_on->fmods;
      break;
    case NL_PMODS:
      list = &pl->parent_on->pmods;
      break;
    case NL_AMODS:
      list = &pl->parent_on->amods;
      break;
    }
    pl->parent_on->op_params |= SGS_POPP_ADJCS;
    SGS_PtrList_add(list, op);
  }
  /*
   * Assign label. If no new label but previous node (for a non-compstep)
   * has one, update label to point to new node, but keep pointer in
   * previous node.
   */
  if (pl->set_label != NULL) {
    SGS_SymTab_set(o->st, pl->set_label, strlen(pl->set_label), op);
    op->label = pl->set_label;
    pl->set_label = NULL;
  } else if (!is_compstep && pop != NULL && pop->label != NULL) {
    SGS_SymTab_set(o->st, pop->label, strlen(pop->label), op);
    op->label = pop->label;
  }
}

/*
 * Default values for new nodes are being set.
 */
#define in_defaults(pl) ((pl)->pl_flags & SDPL_IN_DEFAULTS)
#define enter_defaults(pl) ((void)((pl)->pl_flags |= SDPL_IN_DEFAULTS))
#define leave_defaults(pl) ((void)((pl)->pl_flags &= ~SDPL_IN_DEFAULTS))

/*
 * Values for current node are being set.
 */
#define in_current_node(pl) ((pl)->pl_flags & SDPL_IN_NODE)
#define enter_current_node(pl) ((void)((pl)->pl_flags |= SDPL_IN_NODE))
#define leave_current_node(pl) ((void)((pl)->pl_flags &= ~SDPL_IN_NODE))

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *pl, SGS_ScriptOpData *previous,
                       uint8_t linktype, bool is_compstep) {
  pl->on_prev = previous;
  if (!pl->event ||
      !in_current_node(pl) /* previous event implicitly ended */ ||
      pl->next_wait_ms ||
      is_compstep)
    begin_event(pl, linktype, is_compstep);
  begin_operator(pl, linktype, is_compstep);
  pl->last_linktype = linktype; /* FIXME: kludge */
}

static void flush_durgroup(SGS_Parser *o) {
  if (o->group_start != NULL) {
    o->group_end->group_backref = o->group_start;
    o->group_start = o->group_end = NULL;
  }
}

static void begin_scope(SGS_Parser *o, ParseLevel *pl,
                        ParseLevel *parent_pl,
                        uint8_t linktype, uint8_t newscope) {
  memset(pl, 0, sizeof(ParseLevel));
  pl->o = o;
  pl->scope = newscope;
  if (parent_pl != NULL) {
    pl->parent = parent_pl;
    pl->pl_flags = parent_pl->pl_flags;
    if (newscope == SCOPE_SAME)
      pl->scope = parent_pl->scope;
    pl->event = parent_pl->event;
    pl->operator = parent_pl->operator;
    pl->parent_on = parent_pl->parent_on;
    if (newscope == SCOPE_NEST) {
      pl->pl_flags |= SDPL_NESTED_SCOPE;
      pl->parent_on = parent_pl->operator;
    }
  }
  pl->linktype = linktype;
}

static void end_scope(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  end_operator(pl);
  if (pl->scope == SCOPE_BIND) {
    /*
     * Begin multiple-operator node in parent scope for the operator nodes in
     * this scope, provided any are present.
     */
    if (pl->first_operator != NULL) {
      pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
      begin_node(pl->parent, pl->first_operator, pl->parent->last_linktype, false);
    }
  } else if (!pl->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    end_event(pl);
    flush_durgroup(o);
  }
  if (pl->set_label != NULL) {
    warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  char c;
  enter_defaults(pl);
  leave_current_node(pl);
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'a':
      if (read_num(o, 0, &o->sopt.ampmult)) {
        o->sopt.changed |= SGS_SOPT_AMPMULT;
      }
      break;
    case 'f':
      if (read_num(o, read_note, &o->sopt.def_freq)) {
        o->sopt.changed |= SGS_SOPT_DEF_FREQ;
      }
      break;
    case 'n': {
      float freq;
      if (read_num(o, 0, &freq)) {
        if (freq < 1.f) {
          warning(o, "ignoring tuning frequency (Hz) below 1.0");
          break;
        }
        o->sopt.A4_freq = freq;
        o->sopt.changed |= SGS_SOPT_A4_FREQ;
      }
      break; }
    case 'r':
      if (read_num(o, 0, &o->sopt.def_ratio)) {
        o->sopt.changed |= SGS_SOPT_DEF_RATIO;
      }
      break;
    case 't': {
      float time;
      if (read_num(o, 0, &time)) {
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        o->sopt.def_time_ms = lrint(time * 1000.f);
        o->sopt.changed |= SGS_SOPT_DEF_TIME;
      }
      break; }
    default:
    /*UNKNOWN:*/
      o->nextc = c;
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

static bool parse_level(SGS_Parser *o, ParseLevel *parent_pl,
                        uint8_t linktype, char newscope);

static bool parse_step(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  char c;
  leave_defaults(pl);
  enter_current_node(pl);
  while ((c = read_char(o)) != EOF) {
    SGS_ScriptEvData *e = pl->event;
    SGS_ScriptOpData *op = pl->operator;
    switch (c) {
    case 'P':
      if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
        goto UNKNOWN;
      if (read_num(o, 0, &e->panning)) {
        if (e->valitpanning.type == SGS_VALIT_NONE)
          e->vo_attr &= ~SGS_PVOA_VALITPANNING;
      }
      if (tryc('{', o->f)) {
        if (read_valit(o, 0, &e->valitpanning))
          e->vo_attr |= SGS_PVOA_VALITPANNING;
      }
      break;
    case '\\':
      if (parse_waittime(pl)) {
        // FIXME: Buggy update node handling for carriers etc. if enabled.
        //begin_node(pl, pl->operator, NL_REFER, false);
      }
      break;
    case 'a':
      if (read_num(o, 0, &op->amp)) {
        op->op_params |= SGS_POPP_AMP;
        if (op->valitamp.type == SGS_VALIT_NONE)
          op->attr &= ~SGS_POPA_VALITAMP;
      }
      if (tryc('{', o->f)) {
        if (read_valit(o, 0, &op->valitamp))
          op->attr |= SGS_POPA_VALITAMP;
      }
      if (tryc('!', o->f)) {
        if (!testc('[', o->f)) {
          read_num(o, 0, &op->dynamp);
        }
        if (tryc('[', o->f)) {
          if (op->amods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->amods);
          }
          parse_level(o, pl, NL_AMODS, SCOPE_NEST);
        }
      }
      break;
    case 'f':
      if (read_num(o, read_note, &op->freq)) {
        op->attr &= ~SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (op->valitfreq.type == SGS_VALIT_NONE)
          op->attr &= ~(SGS_POPA_VALITFREQ |
                        SGS_POPA_VALITFREQRATIO);
      }
      if (tryc('{', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->attr |= SGS_POPA_VALITFREQ;
          op->attr &= ~SGS_POPA_VALITFREQRATIO;
        }
      }
      if (tryc('!', o->f)) {
        if (!testc('[', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_POPA_DYNFREQRATIO;
          }
        }
        if (tryc('[', o->f)) {
          if (op->fmods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->fmods);
          }
          parse_level(o, pl, NL_FMODS, SCOPE_NEST);
        }
      }
      break;
    case 'p':
      if (read_num(o, 0, &op->phase)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->op_params |= SGS_POPP_PHASE;
      }
      if (tryc('[', o->f)) {
        if (op->pmods.count > 0) {
          op->op_params |= SGS_POPP_ADJCS;
          SGS_PtrList_clear(&op->pmods);
        }
        parse_level(o, pl, NL_PMODS, SCOPE_NEST);
      }
      break;
    case 'r':
      if (!(pl->pl_flags & SDPL_NESTED_SCOPE))
        goto UNKNOWN;
      if (read_num(o, 0, &op->freq)) {
        op->attr |= SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (op->valitfreq.type == SGS_VALIT_NONE)
          op->attr &= ~(SGS_POPA_VALITFREQ |
                        SGS_POPA_VALITFREQRATIO);
      }
      if (tryc('{', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->attr |= SGS_POPA_VALITFREQ |
                      SGS_POPA_VALITFREQRATIO;
        }
      }
      if (tryc('!', o->f)) {
        if (!testc('[', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->attr |= SGS_POPA_DYNFREQRATIO;
          }
        }
        if (tryc('[', o->f)) {
          if (op->fmods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->fmods);
          }
          parse_level(o, pl, NL_FMODS, SCOPE_NEST);
        }
      }
      break;
    case 's': {
      float silence;
      if (!read_num(o, 0, &silence)) break;
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = lrint(silence * 1000.f);
      break; }
    case 't':
      if (tryc('d', o->f)) {
        op->op_flags |= SGS_SDOP_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = o->sopt.def_time_ms;
      } else if (tryc('i', o->f)) {
        if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
          warning(o, "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        if (!read_num(o, 0, &time)) break;
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
        op->time_ms = lrint(time * 1000.f);
      }
      op->op_params |= SGS_POPP_TIME;
      break;
    case 'w': {
      int32_t wave = read_wavetype(o);
      if (wave < 0)
        break;
      op->wave = wave;
      break; }
    default:
    UNKNOWN:
      o->nextc = c;
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

enum {
  HANDLE_DEFER = 1<<0,
  DEFERRED_STEP = 1<<1,
  DEFERRED_SETTINGS = 1<<2
};
static bool parse_level(SGS_Parser *o, ParseLevel *parent_pl,
                        uint8_t linktype, char newscope) {
  LabelBuf label;
  ParseLevel pl;
  char c;
  uint32_t label_len;
  uint8_t flags = 0;
  bool endscope = false;
  begin_scope(o, &pl, parent_pl, linktype, newscope);
  ++o->calllevel;
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case NEWLINE:
      ++o->line;
      if (pl.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->calllevel > 1)
          goto RETURN;
        flags = 0;
        leave_defaults(&pl);
        if (in_current_node(&pl)) {
          leave_current_node(&pl);
        }
        pl.first_operator = NULL;
      }
      break;
    case '\'':
      /*
       * Label assignment (set to what follows).
       */
      if (pl.set_label != NULL) {
        warning(o, "ignoring label assignment to label assignment");
        break;
      }
      label_len = read_label(o, label, c);
      pl.set_label = SGS_SymTab_pool_str(o->st, label, label_len);
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (in_defaults(&pl) || !pl.event)
        goto INVALID;
      begin_node(&pl, pl.operator, NL_REFER, true);
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '@':
      if (tryc('[', o->f)) {
        end_operator(&pl);
        if (parse_level(o, &pl, pl.linktype, SCOPE_BIND))
          goto RETURN;
        /*
         * Multiple-operator node now open.
         */
        flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        break;
      }
      /*
       * Label reference (get and use value).
       */
      if (pl.set_label != NULL) {
        warning(o, "ignoring label assignment to label reference");
        pl.set_label = NULL;
      }
      leave_defaults(&pl);
      leave_current_node(&pl);
      label_len = read_label(o, label, c);
      if (label_len > 0) {
        SGS_ScriptOpData *ref = SGS_SymTab_get(o->st, label, label_len);
        if (!ref)
          warning(o, "ignoring reference to undefined label");
        else {
          begin_node(&pl, ref, NL_REFER, false);
          flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case 'O': {
      int32_t wave = read_wavetype(o);
      if (wave < 0)
        break;
      begin_node(&pl, 0, pl.linktype, false);
      pl.operator->wave = wave;
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      flags = parse_settings(&pl) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '[':
      warning(o, "opening '[' out of place");
      break;
    case '\\':
      if (in_defaults(&pl) ||
          (pl.pl_flags & SDPL_NESTED_SCOPE && pl.event))
        goto INVALID;
      parse_waittime(&pl);
      break;
    case ']':
      if (pl.scope == SCOPE_BIND) {
        endscope = true;
        goto RETURN;
      }
      if (pl.scope == SCOPE_NEST) {
        end_operator(&pl);
        endscope = true;
        goto RETURN;
      }
      warning(o, "closing ']' without opening '['");
      break;
    case '{':
      warning(o, "opening '{' out of place");
      break;
    case '|':
      if (in_defaults(&pl) ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      end_event(&pl);
      if (!o->group_start) {
        warning(o, "no sounds precede time separator");
        break;
      }
      flush_durgroup(o);
      leave_current_node(&pl);
      break;
    case '}':
      warning(o, "closing '}' without opening '{'");
      break;
    default:
    INVALID:
      warning(o, WARN_INVALID);
      break;
    }
    /* Return to sub-parsing routines. */
    if (flags != 0 && !(flags & HANDLE_DEFER)) {
      uint8_t test = flags;
      flags = 0;
      if ((test & DEFERRED_STEP) != 0) {
        if (parse_step(&pl))
          flags = HANDLE_DEFER | DEFERRED_STEP;
      } else if ((test & DEFERRED_SETTINGS) != 0)
        if (parse_settings(&pl))
          flags = HANDLE_DEFER | DEFERRED_SETTINGS;
    }
    flags &= ~HANDLE_DEFER;
  }
FINISH:
  if (newscope == SCOPE_NEST || newscope == SCOPE_BIND)
    warning(o, "end of file without closing ']'s");
RETURN:
  end_scope(&pl);
  --o->calllevel;
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && pl.scope != newscope);
}

/*
 * Process file.
 *
 * \return true if completed, false on error preventing parse
 */
static bool parse_file(SGS_Parser *o, const char *fname) {
  if (!(o->f = fopen(fname, "r"))) {
    SGS_error(NULL, "couldn't open script file \"%s\" for reading", fname);
    return false;
  }
  o->fn = fname;
  o->line = 1;
  parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
  fclose(o->f);
  o->f = NULL;
  return true;
}

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SGS_ScriptEvData *e_last) {
  SGS_ScriptEvData *e, *e_after = e_last->next;
  size_t i;
  uint32_t cur_longest = 0, wait_sum = 0, wait_after = 0;
  for (e = e_last->group_backref; e != e_after; ) {
    SGS_ScriptOpData **ops;
    ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ScriptOpData *op = ops[i];
      if (cur_longest < op->time_ms)
        cur_longest = op->time_ms;
    }
    wait_after = cur_longest;
    e = e->next;
    if (e != NULL) {
      if (cur_longest > e->wait_ms)
        cur_longest -= e->wait_ms;
      else
        cur_longest = 0;
      wait_sum += e->wait_ms;
    }
  }
  for (e = e_last->group_backref; e != e_after; ) {
    SGS_ScriptOpData **ops;
    ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ScriptOpData *op = ops[i];
      if ((op->op_flags & SGS_SDOP_TIME_DEFAULT) != 0) {
        /* fill in sensible default time */
        op->time_ms = cur_longest + wait_sum;
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
      }
    }
    e = e->next;
    if (e != NULL) {
      wait_sum -= e->wait_ms;
    }
  }
  e_last->group_backref = NULL;
  if (e_after != NULL)
    e_after->wait_ms += wait_after;
}

static void time_operator(SGS_ScriptOpData *op) {
  SGS_ScriptEvData *e = op->event;
  if (op->valitfreq.time_ms == SGS_TIME_DEFAULT)
    op->valitfreq.time_ms = op->time_ms;
  if (op->valitamp.time_ms == SGS_TIME_DEFAULT)
    op->valitamp.time_ms = op->time_ms;
  if ((op->op_flags & (SGS_SDOP_TIME_DEFAULT | SGS_SDOP_NESTED)) ==
                      (SGS_SDOP_TIME_DEFAULT | SGS_SDOP_NESTED)) {
    op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
    if (!(op->op_flags & SGS_SDOP_HAS_COMPSTEP))
      op->time_ms = SGS_TIME_INF;
  }
  if (op->time_ms != SGS_TIME_INF && !(op->op_flags & SGS_SDOP_SILENCE_ADDED)) {
    op->time_ms += op->silence_ms;
    op->op_flags |= SGS_SDOP_SILENCE_ADDED;
  }
  if ((e->ev_flags & SGS_SDEV_ADD_WAIT_DURATION) != 0) {
    if (e->next != NULL)
      ((SGS_ScriptEvData*)e->next)->wait_ms += op->time_ms;
    e->ev_flags &= ~SGS_SDEV_ADD_WAIT_DURATION;
  }
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->fmods);
  for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->pmods);
  for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&op->amods);
  for (i = op->amods.old_count; i < op->amods.count; ++i) {
    time_operator(ops[i]);
  }
}

static void time_event(SGS_ScriptEvData *e) {
  /*
   * Fill in blank valit durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->valitpanning.time_ms == SGS_TIME_DEFAULT)
    e->valitpanning.time_ms = 1000; /* FIXME! */
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    time_operator(ops[i]);
  }
  /*
   * Timing for sub-events - done before event list flattened.
   */
  SGS_ScriptEvBranch *fork = e->forks;
  while (fork != NULL) {
    SGS_ScriptEvData *ce = fork->events;
    SGS_ScriptOpData *ce_op, *ce_op_prev, *e_op;
    ce_op = (SGS_ScriptOpData*) SGS_PtrList_GET(&ce->operators, 0),
    ce_op_prev = ce_op->on_prev,
    e_op = ce_op_prev;
    if ((e_op->op_flags & SGS_SDOP_TIME_DEFAULT) != 0)
      e_op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
    for (;;) {
      ce->wait_ms += ce_op_prev->time_ms;
      if ((ce_op->op_flags & SGS_SDOP_TIME_DEFAULT) != 0) {
        ce_op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
        ce_op->time_ms = ((ce_op->op_flags & SGS_SDOP_NESTED) != 0 &&
                          !ce->next) ?
                         SGS_TIME_INF :
                         ce_op_prev->time_ms - ce_op_prev->silence_ms;
      }
      time_event(ce);
      if (ce_op->time_ms == SGS_TIME_INF)
        e_op->time_ms = SGS_TIME_INF;
      else if (e_op->time_ms != SGS_TIME_INF)
        e_op->time_ms += ce_op->time_ms +
                         (ce->wait_ms - ce_op_prev->time_ms);
      ce_op->op_params &= ~SGS_POPP_TIME;
      ce_op_prev = ce_op;
      ce = ce->next;
      if (!ce) break;
      ce_op = (SGS_ScriptOpData*) SGS_PtrList_GET(&ce->operators, 0);
    }
    fork = fork->prev;
  }
}

/*
 * Deals with events that are "sub-events" (attached to a main event as
 * nested sequence rather than part of the main linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SGS_ScriptEvData *e) {
  SGS_ScriptEvBranch *fork = e->forks;
  SGS_ScriptEvData *ne = fork->events;
  SGS_ScriptEvData *fe = e->next, *fe_prev = e;
  while (ne != NULL) {
    if (!fe) {
      /*
       * No more events in the flat sequence,
       * so append all sub-events.
       */
      fe_prev->next = fe = ne;
      break;
    }
    /*
     * Insert next sub-event before or after
     * the next events of the flat sequence.
     */
    SGS_ScriptEvData *ne_next = ne->next;
    if (fe->wait_ms >= ne->wait_ms) {
      fe->wait_ms -= ne->wait_ms;
      fe_prev->next = ne;
      ne->next = fe;
    } else {
      ne->wait_ms -= fe->wait_ms;
      /*
       * If several events should pass in the flat sequence
       * before the next sub-event is inserted, skip ahead.
       */
      while (fe->next && fe->next->wait_ms <= ne->wait_ms) {
        fe_prev = fe;
        fe = fe->next;
        ne->wait_ms -= fe->wait_ms;
      }
      SGS_ScriptEvData *fe_next = fe->next;
      fe->next = ne;
      ne->next = fe_next;
      fe = fe_next;
      if (fe)
        fe->wait_ms -= ne->wait_ms;
    }
    fe_prev = ne;
    ne = ne_next;
  }
  e->forks = fork->prev;
  free(fork);
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SGS_Parser *o) {
  SGS_ScriptEvData *e;
  for (e = o->events; e; e = e->next) {
    time_event(e);
    if (e->group_backref != NULL) time_durgroup(e);
  }
  /*
   * Must be separated into pass following timing adjustments for events;
   * otherwise, flattening will fail to arrange events in the correct order
   * in some cases.
   */
  for (e = o->events; e; e = e->next) {
    while (e->forks != NULL) flatten_events(e);
  }
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SGS_Script* SGS_load_Script(const char *fname) {
  SGS_Parser pr;
  init_parser(&pr);
  SGS_Script *o = NULL;
  if (!parse_file(&pr, fname)) {
    goto DONE;
  }

  postparse_passes(&pr);
  o = calloc(1, sizeof(SGS_Script));
  o->events = pr.events;
  o->name = fname;
  o->sopt = pr.sopt;

DONE:
  fini_parser(&pr);
  return o;
}

/**
 * Destroy instance.
 */
void SGS_discard_Script(SGS_Script *o) {
  SGS_ScriptEvData *e;
  for (e = o->events; e; ) {
    SGS_ScriptEvData *e_next = e->next;
    destroy_event_node(e);
    e = e_next;
  }
  free(o);
}
