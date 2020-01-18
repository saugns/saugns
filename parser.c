/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include "file.h"
#include "mempool.h"
#include "arrtype.h"
#include "script.h"
#include "help.h"
#include "math.h"
#include "parser/parseconv.h"
#include <stdlib.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

static uint8_t filter_symchar(SGS_File *restrict f sgsMaybeUnused,
                              uint8_t c) {
  return IS_SYMCHAR(c) ? c : 0;
}

/*
 * Parser scanner
 */

typedef struct ScanLookup {
  SGS_ScriptOptions sopt;
  const char *const*wave_names;
  const char *const*ramp_names;
} ScanLookup;

typedef struct PScanner {
  SGS_File *f;
  uint32_t line;
  uint8_t c, next_c;
  bool newline;
  bool stash;
  SGS_Symtab *st;
  ScanLookup *sl;
} PScanner;

typedef float (*NumSym_f)(PScanner *restrict o);

static void init_scanner(PScanner *restrict o,
                         SGS_File *restrict f,
                         SGS_Symtab *restrict st,
                         ScanLookup *restrict sl) {
  *o = (PScanner){0};
  o->f = f;
  o->line = 1;
  o->st = st;
  o->sl = sl;
}

static void fini_scanner(PScanner *restrict o) {
  SGS_File_close(o->f);
  o->f = NULL; // freed by invoker
}

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void sgsNoinline scan_warning(PScanner *restrict o,
                                     const char *restrict str) {
  SGS_File *f = o->f;
  uint8_t c = o->c;
  if (SGS_IS_ASCIIVISIBLE(c)) {
    fprintf(stderr, "warning: %s [line %d, at '%c'] - %s\n",
            f->path, o->line, c, str);
  } else if (SGS_File_AT_EOF(f)) {
    fprintf(stderr, "warning: %s [line %d, at EOF] - %s\n",
            f->path, o->line, str);
  } else {
    fprintf(stderr, "warning: %s [line %d, at 0x%02hhX] - %s\n",
            f->path, o->line, c, str);
  }
}

#define SCAN_NEWLINE '\n'
static uint8_t scan_getc(PScanner *restrict o) {
  SGS_File *f = o->f;
  uint8_t c;
  if (o->newline) {
    ++o->line;
    o->newline = false;
  }
  SGS_File_skipspace(f);
  if (o->stash) {
    o->stash = false;
    c = o->next_c;
  } else {
    c = SGS_File_GETC(f);
  }
  if (c == '#') {
    SGS_File_skipline(f);
    c = SGS_File_GETC(f);
  }
  if (c == '\n') {
    SGS_File_TRYC(f, '\r');
    c = SCAN_NEWLINE;
    o->newline = true;
  } else if (c == '\r') {
    c = SCAN_NEWLINE;
    o->newline = true;
  } else {
    SGS_File_skipspace(f);
  }
  o->c = c;
  return c;
}

static bool scan_stashc(PScanner *restrict o, uint8_t c) {
  if (o->stash) {
    SGS_warning("PScanner", "only one stashed character supported");
    return false;
  }
  o->next_c = c;
  o->stash = true;
  return true;
}

static void scan_ws(PScanner *restrict o) {
  SGS_File *f = o->f;
  for (;;) {
    uint8_t c = SGS_File_GETC(f);
    if (IS_SPACE(c))
      continue;
    if (c == '\n') {
      ++o->line;
      SGS_File_TRYC(f, '\r');
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      SGS_File_skipline(f);
      c = SGS_File_GETC(f);
    } else {
      SGS_File_UNGETC(f);
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
static bool handle_unknown_or_end(PScanner *restrict o) {
  SGS_File *f = o->f;
  if (SGS_File_AT_EOF(f) ||
      SGS_File_AFTER_EOF(f)) {
    return false;
  }
  scan_warning(o, "invalid character");
  return true;
}

typedef struct NumParser {
  PScanner *sc;
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
static double scan_num_r(NumParser *restrict o,
                         uint8_t pri, uint32_t level) {
  PScanner *sc = o->sc;
  SGS_File *f = sc->f;
  double num;
  uint8_t c;
  if (level > 0) scan_ws(sc);
  c = SGS_File_GETC(f);
  if (c == '(') {
    num = scan_num_r(o, NUMEXP_SUB, level+1);
  } else if (c == '+' || c == '-') {
    num = scan_num_r(o, NUMEXP_ADT, level);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (o->numsym_f && IS_ALPHA(c)) {
    SGS_File_UNGETC(f);
    num = o->numsym_f(sc);
    if (isnan(num)) goto REJECT;
  } else {
    size_t read_len;
    SGS_File_UNGETC(f);
    SGS_File_getd(f, &num, false, &read_len);
    if (read_len == 0) goto REJECT;
  }
  if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
  for (;;) {
    bool rpar_mlt = false;
    if (isinf(num)) o->has_infnum = true;
    if (level > 0) scan_ws(sc);
    c = SGS_File_GETC(f);
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
        SGS_File_UNGETC(f);
        double rval = scan_num_r(o, NUMEXP_MLT, level);
        if (isnan(rval)) goto ACCEPT;
        num *= rval;
        break;
      }
      if (pri == NUMEXP_SUB && level > 0) {
        scan_warning(sc, "numerical expression has '(' without closing ')'");
      }
      goto DEFER;
    }
    if (isnan(num)) goto DEFER;
  }
DEFER:
  SGS_File_UNGETC(f);
ACCEPT:
  if (0)
REJECT: {
    num = NAN;
  }
  return num;
}
static bool scan_num(PScanner *restrict o, NumSym_f scan_numsym,
                     float *restrict var) {
  NumParser np = {o, scan_numsym, false, false};
  float num = scan_num_r(&np, NUMEXP_SUB, 0);
  if (isnan(num))
    return false;
  if (isinf(num)) np.has_infnum = true;
  if (np.has_infnum) {
    scan_warning(o, "discarding expression with infinite number");
    return false;
  }
  *var = num;
  return true;
}

#define OCTAVES 11
static float scan_note(PScanner *restrict o) {
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
  SGS_File *f = o->f;
  float freq;
  o->c = SGS_File_GETC(f);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  size_t read_len;
  if (o->c >= 'a' && o->c <= 'g') {
    subnote = o->c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    o->c = SGS_File_GETC(f);
  }
  if (o->c < 'A' || o->c > 'G') {
    scan_warning(o, "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = o->c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  o->c = SGS_File_GETC(f);
  if (o->c == 's')
    semitone = 2;
  else if (o->c == 'f')
    semitone = 0;
  else
    SGS_File_UNGETC(f);
  SGS_File_geti(f, &octave, false, &read_len);
  if (read_len == 0)
    octave = 4;
  else if (octave >= OCTAVES) {
    scan_warning(o, "invalid octave specified for note - valid range 0-10");
    octave = 4;
  }
  freq = o->sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
}

#define LABEL_BUFLEN 80
#define LABEL_MAXLEN_S "79"
typedef char LabelBuf[LABEL_BUFLEN];
static SGS_Symstr *scan_label(PScanner *restrict o, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  SGS_File *f = o->f;
  LabelBuf label;
  size_t len = 0;
  bool truncated;
  nolabel_msg[9] = op; /* replace ? */
  truncated = !SGS_File_getstr(f, label, LABEL_BUFLEN, &len, filter_symchar);
  if (len == 0) {
    scan_warning(o, nolabel_msg);
  }
  if (truncated) {
    o->c = SGS_File_RETC(f);
    scan_warning(o, "limiting label name to "LABEL_MAXLEN_S" characters");
    SGS_File_skipstr(f, filter_symchar);
  }
  o->c = SGS_File_RETC(f);
  return SGS_Symtab_get_symstr(o->st, label, len);
}

static bool scan_symafind(PScanner *restrict o,
                          const char *const*restrict stra,
                          size_t n, size_t *restrict found_i) {
  LabelBuf buf;
  SGS_File *f = o->f;
  size_t len = 0;
  bool truncated;
  truncated = !SGS_File_getstr(f, buf, LABEL_BUFLEN, &len, filter_symchar);
  if (len == 0) {
    scan_warning(o, "label missing");
    return false;
  }
  if (truncated) {
    scan_warning(o, "limiting label name to "LABEL_MAXLEN_S" characters");
    SGS_File_skipstr(f, filter_symchar);
  }
  const char *key = SGS_Symtab_pool_str(o->st, buf, len);
  for (size_t i = 0; i < n; ++i) {
    if (stra[i] == key) {
      o->c = SGS_File_RETC(f);
      *found_i = i;
      return true;
    }
  }
  return false;
}

static bool scan_wavetype(PScanner *restrict o, size_t *restrict found_id) {
  const char *const *names = o->sl->wave_names;
  if (scan_symafind(o, names, SGS_WAVE_TYPES, found_id))
    return true;

  scan_warning(o, "invalid wave type; available types are:");
  SGS_print_names(names, "\t", stderr);
  return false;
}

/*
 * Parser
 */

struct NestScope {
  SGS_ScriptListData *list;
  SGS_ScriptOpData *last_op;
  /* values passed for outer parameter */
  SGS_RampParam *op_sweep;
  NumSym_f numsym_f;
};

sgsArrType(NestArr, struct NestScope, )

typedef struct SGS_Parser {
  PScanner sc;
  ScanLookup sl;
  SGS_Symtab *st;
  SGS_Mempool *mp;
  NestArr nest;
  /* node state */
  SGS_ScriptEvData *events;
  SGS_ScriptEvData *last_event;
  SGS_ScriptEvData *group_start, *group_end;
} SGS_Parser;

/*
 * Default script options, used until changed in a script.
 */
static const SGS_ScriptOptions def_sopt = {
  .set = 0,
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
static void init_parser(SGS_Parser *restrict o) {
  *o = (SGS_Parser){0};
  o->mp = SGS_create_Mempool(0);
  o->st = SGS_create_Symtab(o->mp);
  o->sl.sopt = def_sopt;
  o->sl.wave_names = SGS_Symtab_pool_stra(o->st,
                       SGS_Wave_names, SGS_WAVE_TYPES);
  o->sl.ramp_names = SGS_Symtab_pool_stra(o->st,
                       SGS_Ramp_names, SGS_RAMP_TYPES);
}

/*
 * Finalize parser instance.
 */
static void fini_parser(SGS_Parser *restrict o) {
  SGS_destroy_Symtab(o->st);
  SGS_destroy_Mempool(o->mp);
  NestArr_clear(&o->nest);
}

/*
 * Scope values.
 */
enum {
  SCOPE_SAME = 0, // specially handled inner copy of parent scope (unused)
  SCOPE_GROUP,    // '{...}' or top scope
  SCOPE_BIND,     // '@[...]'
  SCOPE_NEST,     // '[...]'
};

struct ParseLevel;
typedef void (*ParseLevel_sub_f)(struct ParseLevel *pl);
static void parse_in_settings(struct ParseLevel *pl);
static void parse_in_op_step(struct ParseLevel *pl);
static void parse_in_par_sweep(struct ParseLevel *pl);

/*
 * Parse level flags.
 */
enum {
  PL_BIND_MULTIPLE = 1<<0, // previous node interpreted as set of nodes
  PL_NEW_EVENT_FORK = 1<<1,
  PL_OWN_EV = 1<<2,
  PL_OWN_OP = 1<<3,
  PL_SET_SWEEP = 1<<4, /* parameter sweep set in subscope */
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
typedef struct ParseLevel {
  SGS_Parser *o;
  struct ParseLevel *parent;
  ParseLevel_sub_f sub_f;
  uint8_t pl_flags;
  uint8_t scope, close_c;
  uint8_t use_type;
  SGS_ScriptEvData *event, *last_event;
  SGS_ScriptOpData *operator, *ev_last;
  SGS_ScriptOpData *parent_on, *on_prev;
  SGS_Symstr *set_label;
  /* timing/delay */
  SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
  float used_ampmult; /* update on node creation */
} ParseLevel;

typedef struct SGS_ScriptEvBranch {
  SGS_ScriptEvData *events;
  struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

static bool parse_waittime(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  /* FIXME: ADD_WAIT_DURATION */
  if (SGS_File_TRYC(f, 't')) {
    if (!pl->ev_last) {
      scan_warning(sc, "add wait for last duration before any parts given");
      return false;
    }
    pl->last_event->ev_flags |= SGS_SDEV_ADD_WAIT_DURATION;
  } else {
    float wait;
    uint32_t wait_ms;
    scan_num(sc, 0, &wait);
    if (wait < 0.f) {
      scan_warning(sc, "ignoring '\\' with sub-zero time");
      return false;
    }
    wait_ms = SGS_ui32rint(wait * 1000.f);
    pl->next_wait_ms += wait_ms;
  }
  return true;
}

/*
 * Node- and scope-handling functions
 */

static void end_operator(ParseLevel *restrict pl) {
  if (!(pl->pl_flags & PL_OWN_OP))
    return;
  pl->pl_flags &= ~PL_OWN_OP;
  SGS_ScriptOpData *op = pl->operator;
  SGS_ScriptOpData *pop = op->on_prev;
  if (!pop) { /* initial event should reset its parameters */
    op->op_params |= SGS_POPP_WAVE |
                     SGS_POPP_TIME |
                     SGS_POPP_SILENCE |
                     SGS_POPP_FREQ |
                     SGS_POPP_DYNFREQ |
                     SGS_POPP_PHASE |
                     SGS_POPP_AMP |
                     SGS_POPP_DYNAMP |
                     SGS_POPP_ATTR;
  } else {
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
  if ((op->freq.flags & SGS_RAP_RAMP) != 0)
    op->op_params |= SGS_POPP_ATTR;
  if ((op->amp.flags & SGS_RAP_RAMP) != 0)
    op->op_params |= SGS_POPP_ATTR;
  if (!(op->op_flags & SGS_SDOP_NESTED)) {
    op->amp.v0 *= pl->used_ampmult;
    op->amp.vt *= pl->used_ampmult;
  }
  pl->operator = NULL;
}

static void end_event(ParseLevel *restrict pl) {
  if (!(pl->pl_flags & PL_OWN_EV))
    return;
  pl->pl_flags &= ~PL_OWN_EV;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptEvData *pve;
  end_operator(pl);
  pl->ev_last = NULL;
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
    e->vo_params |= SGS_PVOP_ATTR |
                    SGS_PVOP_PAN;
  } else {
    if (e->vo_attr != pve->vo_attr)
      e->vo_params |= SGS_PVOP_ATTR;
    if (e->pan.v0 != pve->pan.v0)
      e->vo_params |= SGS_PVOP_PAN;
  }
  if ((e->pan.flags & SGS_RAP_RAMP) != 0)
    e->vo_params |= SGS_PVOP_ATTR;
  pl->last_event = e;
  pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl, bool is_compstep) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e, *pve;
  end_event(pl);
  pl->event = SGS_Mempool_alloc(o->mp, sizeof(SGS_ScriptEvData));
  e = pl->event;
  e->wait_ms = pl->next_wait_ms;
  pl->next_wait_ms = 0;
  if (pl->on_prev != NULL) {
    SGS_ScriptEvBranch *fork;
    if (pl->on_prev->op_flags & SGS_SDOP_NESTED)
      e->ev_flags |= SGS_SDEV_IMPLICIT_TIME;
    pve = pl->on_prev->event;
    pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
    fork = pve->forks;
    if (is_compstep) {
      if (pl->pl_flags & PL_NEW_EVENT_FORK) {
        if (!pl->main_ev)
          pl->main_ev = pve;
        else
          fork = pl->main_ev->forks;
        pl->main_ev->forks = calloc(1, sizeof(SGS_ScriptEvBranch));
        pl->main_ev->forks->events = e;
        pl->main_ev->forks->prev = fork;
        pl->pl_flags &= ~PL_NEW_EVENT_FORK;
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
    e->pan = pve->pan;
  } else { /* set defaults */
    e->pan.v0 = 0.5f; /* center */
    e->pan.ramp = SGS_RAMP_LIN; /* if ramp enabled */
  }
  if (!is_compstep) {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    pl->main_ev = NULL;
  }
  SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
  if (!o->group_start)
    o->group_start = group_e;
  o->group_end = group_e;
  pl->pl_flags |= PL_OWN_EV;
}

static void begin_operator(ParseLevel *restrict pl, bool is_compstep) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptOpData *op, *pop = pl->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(pl);
  pl->operator = SGS_Mempool_alloc(o->mp, sizeof(SGS_ScriptOpData));
  op = pl->operator;
  if (!is_compstep)
    pl->pl_flags |= PL_NEW_EVENT_FORK;
  pl->used_ampmult = o->sl.sopt.ampmult;
  /*
   * Initialize node.
   */
  if (pop != NULL) {
    pop->op_flags |= SGS_SDOP_LATER_USED;
    op->on_prev = pop;
    op->op_flags = pop->op_flags & (SGS_SDOP_NESTED |
                                    SGS_SDOP_MULTIPLE);
    op->time = (SGS_Time){pop->time.v_ms,
      (pop->time.flags & SGS_TIMEP_IMPLICIT)};
    op->attr = pop->attr;
    op->wave = pop->wave;
    op->freq = pop->freq;
    op->amp = pop->amp;
    op->phase = pop->phase;
    op->dynfreq = pop->dynfreq;
    op->dynamp = pop->dynamp;
    if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
      SGS_ScriptOpData *mpop = pop;
      uint32_t max_time = 0;
      do {
        if (max_time < mpop->time.v_ms) max_time = mpop->time.v_ms;
      } while ((mpop = mpop->next) != NULL);
      op->op_flags |= SGS_SDOP_MULTIPLE;
      op->time.v_ms = max_time;
      pl->pl_flags &= ~PL_BIND_MULTIPLE;
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
    op->amp.v0 = 1.0f;
    op->amp.ramp = SGS_RAMP_LIN; /* if ramp enabled */
    if (pl->use_type == SGS_POP_CARR) {
      op->freq.v0 = o->sl.sopt.def_freq;
    } else {
      op->op_flags |= SGS_SDOP_NESTED;
      op->freq.v0 = o->sl.sopt.def_ratio;
      op->attr |= SGS_POPA_FREQRATIO;
    }
    op->freq.ramp = SGS_RAMP_LIN; /* if ramp enabled */
  }
  op->event = e;
  /*
   * Add new operator to parent(s), ie. either the current event node,
   * or an operator node (either ordinary or representing multiple
   * carriers) in the case of operator linking/nesting.
   */
  struct NestScope *nest = NestArr_tip(&o->nest);
  if (pop || !nest) {
    if (!e->operators.first_on)
      e->operators.first_on = op;
    else
      pl->ev_last->next = op;
    pl->ev_last = op;
    if (!pop) {
      e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
      if (!e->op_graph.first_on)
        e->op_graph.first_on = op;
      ++e->op_graph.count;
    }
  } else {
    if (!nest->list->first_on)
      nest->list->first_on = op;
    else
      nest->last_op->next = op;
    nest->last_op = op;
    ++nest->list->count;
  }
  /*
   * Assign label?
   */
  if (pl->set_label != NULL) {
    pl->set_label->data = op;
    pl->set_label = NULL;
  }
  pl->pl_flags |= PL_OWN_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
                       SGS_ScriptOpData *restrict previous,
                       bool is_compstep) {
  pl->on_prev = previous;
  if (!pl->event || pl->next_wait_ms > 0 ||
      /* previous event implicitly ended */
      (previous || pl->use_type <= SGS_POP_CARR) ||
      is_compstep)
    begin_event(pl, is_compstep);
  begin_operator(pl, is_compstep);
}

static void flush_durgroup(SGS_Parser *restrict o) {
  if (o->group_start != NULL) {
    o->group_end->group_backref = o->group_start;
    o->group_start = o->group_end = NULL;
  }
}

static void begin_scope(SGS_Parser *restrict o, ParseLevel *restrict pl,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope, uint8_t close_c) {
  *pl = (ParseLevel){
    .o = o,
    .scope = newscope,
    .close_c = close_c,
  };
  if (parent_pl != NULL) {
    pl->parent = parent_pl;
    pl->sub_f = parent_pl->sub_f;
    if (newscope == SCOPE_SAME)
      pl->scope = parent_pl->scope;
    pl->event = parent_pl->event;
    pl->operator = parent_pl->operator;
    pl->parent_on = parent_pl->parent_on;
    if (newscope == SCOPE_NEST || newscope == SCOPE_BIND) {
      struct NestScope *nest = NestArr_tip(&o->nest);
      nest->list = SGS_Mempool_alloc(o->mp, sizeof(*nest->list));
      pl->parent_on = parent_pl->operator;
      pl->sub_f = nest->op_sweep ? parse_in_par_sweep : NULL;
      SGS_ScriptListData **list = NULL;
      if (newscope == SCOPE_NEST) switch (use_type) {
      case SGS_POP_AMOD: list = &pl->parent_on->amods; break;
      case SGS_POP_FMOD: list = &pl->parent_on->fmods; break;
      case SGS_POP_PMOD: list = &pl->parent_on->pmods; break;
      }
      if (list) {
        nest->list->prev = *list;
        *list = nest->list;
      }
    }
  }
  pl->use_type = use_type;
}

static void end_scope(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  end_operator(pl);
  if (pl->set_label != NULL) {
    scan_warning(&o->sc, "ignoring label assignment without operator");
  }
  if (!pl->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    end_event(pl);
    flush_durgroup(o);
    return;
  }
  if (pl->scope == SCOPE_GROUP) {
    end_event(pl);
  } else if (pl->scope == SCOPE_BIND) {
  } else if (pl->scope == SCOPE_NEST) {
    pl->parent->pl_flags |= pl->pl_flags & PL_SET_SWEEP;
  }
}

/*
 * Main parser functions
 */

#define PARSE_IN__HEAD(Name, GuardCond) \
  SGS_Parser *o = pl->o; \
  if (!(GuardCond)) { pl->sub_f = NULL; return; } \
  PScanner *sc = &o->sc; \
  SGS_File *f = sc->f; \
  pl->sub_f = (Name); \
  uint8_t c; \
  for (;;) { \
    c = scan_getc(sc); \
    /* switch (c) { ... default: ... goto DEFER; } */

#define PARSE_IN__TAIL() \
    /* switch (c) { ... default: ... goto DEFER; } */ \
  } \
  return; \
DEFER: \
  scan_stashc(sc, c); /* let parse_level() take care of it */

static void parse_in_settings(ParseLevel *restrict pl) {
  PARSE_IN__HEAD(parse_in_settings, true)
    switch (c) {
    case 'a':
      if (scan_num(sc, 0, &o->sl.sopt.ampmult)) {
        o->sl.sopt.set |= SGS_SOPT_AMPMULT;
      }
      break;
    case 'f':
      if (scan_num(sc, scan_note, &o->sl.sopt.def_freq)) {
        o->sl.sopt.set |= SGS_SOPT_DEF_FREQ;
      }
      if (SGS_File_TRYC(sc->f, ',') && SGS_File_TRYC(sc->f, 'n')) {
        float freq;
        if (scan_num(sc, 0, &freq)) {
          if (freq < 1.f) {
            scan_warning(sc, "ignoring tuning frequency (Hz) below 1.0");
            break;
          }
          o->sl.sopt.A4_freq = freq;
          o->sl.sopt.set |= SGS_SOPT_A4_FREQ;
        }
      }
      break;
    case 'r':
      if (scan_num(sc, 0, &o->sl.sopt.def_ratio)) {
        o->sl.sopt.set |= SGS_SOPT_DEF_RATIO;
      }
      break;
    case 't': {
      float time;
      if (scan_num(sc, 0, &time)) {
        if (time < 0.f) {
          scan_warning(sc, "ignoring 't' with sub-zero time");
          break;
        }
        o->sl.sopt.def_time_ms = SGS_ui32rint(time * 1000.f);
        o->sl.sopt.set |= SGS_SOPT_DEF_TIME;
      }
      break; }
    default:
      goto DEFER;
    }
  PARSE_IN__TAIL()
}

static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope, uint8_t close_c);

static void parse_in_par_sweep(ParseLevel *restrict pl) {
  struct NestScope *nest = NestArr_tip(&pl->o->nest);
  SGS_RampParam *rap = nest->op_sweep;
  if (!(pl->pl_flags & PL_SET_SWEEP)) {
    pl->pl_flags |= PL_SET_SWEEP;
    /* set default values */
    rap->flags |= SGS_RAP_TIME_DEFAULT;
    rap->time_ms = pl->o->sl.sopt.def_time_ms;
    rap->flags |= SGS_RAP_RAMP;
    rap->ramp = SGS_RAMP_LIN;
  }
  PARSE_IN__HEAD(parse_in_par_sweep, true)
    switch (c) {
    case 'c': {
      const char *const *names = sc->sl->ramp_names;
      size_t type;
      if (!scan_symafind(sc, names, SGS_RAMP_TYPES, &type)) {
        scan_warning(sc, "invalid curve type; available types are:");
        SGS_print_names(names, "\t", stderr);
        break;
      }
      rap->ramp = type;
      break; }
    case 't': {
      float time;
      if (scan_num(sc, 0, &time)) {
        if (time < 0.f) {
          scan_warning(sc, "ignoring 't' with sub-zero time");
          break;
        }
        rap->time_ms = SGS_ui32rint(time * 1000.f);
        rap->flags &= ~SGS_RAP_TIME_DEFAULT;
      }
      break; }
    case 'v':
      if (scan_num(sc, nest->numsym_f, &rap->vt))
        (void)0; // pl->pl_flags |= PL_SET_SWEEP;
      break;
    default:
      goto DEFER;
    }
  PARSE_IN__TAIL()
}

static bool parse_par_list(ParseLevel *restrict pl,
                          NumSym_f numsym_f,
                          SGS_RampParam *restrict op_sweep, bool ratio,
                          uint8_t use_type) {
  SGS_Parser *o = pl->o;
  struct NestScope *nest = NestArr_add(&o->nest, NULL);
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  bool clear = SGS_File_TRYC(f, '-');
  bool empty = true;
  nest->op_sweep = op_sweep;
  nest->numsym_f = numsym_f;
  while (SGS_File_TRYC(f, '[')) {
    empty = false;
    parse_level(o, pl, use_type, SCOPE_NEST, ']');
    nest = NestArr_tip(&o->nest); // re-get, array may have changed
    if (clear) clear = false;
    else {
      if (nest->list->prev)
        nest->list->count += nest->list->prev->count;
      nest->list->append = true;
    }
  }
  NestArr_drop(&o->nest);
  return empty;
}

static void parse_in_op_step(ParseLevel *restrict pl) {
  PARSE_IN__HEAD(parse_in_op_step, pl->operator)
    SGS_ScriptEvData *e = pl->event;
    SGS_ScriptOpData *op = pl->operator;
    switch (c) {
    case 'P':
      if (pl->use_type != SGS_POP_CARR)
        goto DEFER;
      if (scan_num(sc, 0, &e->pan.v0)) {
        if (!(e->pan.flags & SGS_RAP_RAMP))
          e->vo_attr &= ~SGS_PVOA_PAN_RAMP;
      }
      parse_par_list(pl, NULL, &e->pan, false, 0);
      if (pl->pl_flags & PL_SET_SWEEP) {
        pl->pl_flags &= ~PL_SET_SWEEP;
        e->vo_attr |= SGS_PVOA_PAN_RAMP;
      }
      break;
    case '\\':
      if (parse_waittime(pl)) {
        // FIXME: Buggy update node handling for carriers etc. if enabled.
        //begin_node(pl, pl->operator, false);
      }
      break;
    case 'a':
      if (scan_num(sc, 0, &op->amp.v0)) {
        op->op_params |= SGS_POPP_AMP;
        if (!(op->amp.flags & SGS_RAP_RAMP))
          op->attr &= ~SGS_POPA_AMP_RAMP;
      }
      parse_par_list(pl, NULL, &op->amp, false, 0);
      if (pl->pl_flags & PL_SET_SWEEP) {
        pl->pl_flags &= ~PL_SET_SWEEP;
        op->attr |= SGS_POPA_AMP_RAMP;
      }
      if (SGS_File_TRYC(f, ',') && SGS_File_TRYC(f, 'w')) {
        scan_num(sc, 0, &op->dynamp);
        parse_par_list(pl, NULL, NULL, false, SGS_POP_AMOD);
      }
      break;
    case 'f':
      if (scan_num(sc, scan_note, &op->freq.v0)) {
        op->attr &= ~SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (!(op->freq.flags & SGS_RAP_RAMP))
          op->attr &= ~(SGS_POPA_FREQ_RAMP |
                        SGS_POPA_FREQRATIO_RAMP);
      }
      parse_par_list(pl, scan_note, &op->freq, false, 0);
      if (pl->pl_flags & PL_SET_SWEEP) {
        pl->pl_flags &= ~PL_SET_SWEEP;
        op->attr |= SGS_POPA_FREQ_RAMP;
        op->attr &= ~SGS_POPA_FREQRATIO_RAMP;
      }
      if (SGS_File_TRYC(f, ',') && SGS_File_TRYC(f, 'w')) {
        if (scan_num(sc, 0, &op->dynfreq))
          op->attr &= ~SGS_POPA_DYNFREQRATIO;
        parse_par_list(pl, scan_note, NULL, false, SGS_POP_FMOD);
      }
      break;
    case 'p': {
      float phase;
      if (scan_num(sc, 0, &phase)) {
        op->phase = SGS_cyclepos_dtoui32(phase);
        op->op_params |= SGS_POPP_PHASE;
      }
      parse_par_list(pl, NULL, NULL, false, SGS_POP_PMOD);
      break; }
    case 'r':
      if (!(op->op_flags & SGS_SDOP_NESTED))
        goto DEFER;
      if (scan_num(sc, 0, &op->freq.v0)) {
        op->attr |= SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (!(op->freq.flags & SGS_RAP_RAMP))
          op->attr &= ~(SGS_POPA_FREQ_RAMP |
                        SGS_POPA_FREQRATIO_RAMP);
      }
      parse_par_list(pl, scan_note, &op->freq, true, 0);
      if (pl->pl_flags & PL_SET_SWEEP) {
        pl->pl_flags &= ~PL_SET_SWEEP;
        op->attr |= SGS_POPA_FREQ_RAMP |
                    SGS_POPA_FREQRATIO_RAMP;
      }
      if (SGS_File_TRYC(f, ',') && SGS_File_TRYC(f, 'w')) {
        if (scan_num(sc, 0, &op->dynfreq))
          op->attr |= SGS_POPA_DYNFREQRATIO;
        parse_par_list(pl, scan_note, NULL, true, SGS_POP_FMOD);
      }
      break;
    case 's': {
      float silence;
      if (!scan_num(sc, 0, &silence)) break;
      if (silence < 0.f) {
        scan_warning(sc, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = SGS_ui32rint(silence * 1000.f);
      break; }
    case 't':
      if (SGS_File_TRYC(f, 'd')) {
        op->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
      } else if (SGS_File_TRYC(f, 'i')) {
        if (!(op->op_flags & SGS_SDOP_NESTED)) {
          scan_warning(sc, "ignoring 'ti' (implicit time) for non-nested operator");
          break;
        }
        op->time = (SGS_Time){o->sl.sopt.def_time_ms,
          SGS_TIMEP_SET | SGS_TIMEP_IMPLICIT};
      } else {
        float time;
        if (!scan_num(sc, 0, &time)) break;
        if (time < 0.f) {
          scan_warning(sc, "ignoring 't' with sub-zero time");
          break;
        }
        op->time = (SGS_Time){SGS_ui32rint(time * 1000.f),
          SGS_TIMEP_SET};
      }
      op->op_params |= SGS_POPP_TIME;
      break;
    case 'w': {
      size_t wave;
      if (!scan_wavetype(sc, &wave))
        break;
      op->wave = wave;
      break; }
    default:
      goto DEFER;
    }
  PARSE_IN__TAIL()
}

static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope, uint8_t close_c) {
  ParseLevel pl;
  SGS_Symstr *label;
  bool endscope = false;
  begin_scope(o, &pl, parent_pl, use_type, newscope, close_c);
  uint8_t c;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  for (;;) {
    /* Use sub-parsing routine? May also happen inside nested calls. */
    if (pl.sub_f) pl.sub_f(&pl);
    c = scan_getc(sc);
    switch (c) {
    case SCAN_NEWLINE:
      break;
    case '\'':
      /*
       * Label assignment (set to what follows).
       */
      if (pl.set_label != NULL) {
        scan_warning(sc, "ignoring label assignment to label assignment");
        break;
      }
      pl.set_label = label = scan_label(sc, c);
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        scan_stashc(sc, c);
        goto RETURN;
      }
      if (pl.sub_f == parse_in_settings || !pl.event)
        goto INVALID;
      if ((pl.operator->time.flags & (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
          == (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
        scan_warning(sc, "ignoring 'ti' (implicit time) before ';' separator");
      begin_node(&pl, pl.operator, true);
      pl.sub_f = parse_in_op_step;
      break;
    case '<':
      scan_warning(sc, "opening '<' out of place");
      break;
    case '>':
      scan_warning(sc, "closing '>' without opening '<'");
      break;
    case '@':
      if (SGS_File_TRYC(f, '[')) {
        end_operator(&pl);
        NestArr_add(&o->nest, NULL);
        if (parse_level(o, &pl, pl.use_type, SCOPE_BIND, ']'))
          goto RETURN;
        struct NestScope *nest = NestArr_drop(&o->nest);
        if (!nest || !nest->list->first_on) break;
        pl.pl_flags |= PL_BIND_MULTIPLE;
        begin_node(&pl, nest->list->first_on, false);
        /*
         * Multiple-operator node now open.
         */
        pl.sub_f = parse_in_op_step;
        break;
      }
      /*
       * Label reference (get and use value).
       */
      if (pl.set_label != NULL) {
        scan_warning(sc, "ignoring label assignment to label reference");
        pl.set_label = NULL;
      }
      pl.sub_f = NULL;
      label = scan_label(sc, c);
      if (label != NULL) {
        SGS_ScriptOpData *ref = label->data;
        if (!ref)
          scan_warning(sc, "ignoring reference to undefined label");
        else {
          begin_node(&pl, ref, false);
          label->data = pl.operator; /* update */
          pl.sub_f = parse_in_op_step;
        }
      }
      break;
    case 'O': {
      size_t wave;
      if (!scan_wavetype(sc, &wave))
        break;
      struct NestScope *nest = NestArr_tip(&o->nest);
      if (!pl.use_type && nest && nest->op_sweep) {
        scan_warning(sc, "modulators not supported here");
        break;
      }
      begin_node(&pl, 0, false);
      pl.operator->wave = wave;
      pl.sub_f = parse_in_op_step;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      pl.sub_f = parse_in_settings;
      break;
    case '\\':
      if (pl.use_type != SGS_POP_CARR && pl.event != NULL)
        goto INVALID;
      parse_waittime(&pl);
      break;
    case '[':
      scan_warning(sc, "opening '[' out of place");
      break;
    case ']':
      if (c == close_c) {
        if (pl.scope == SCOPE_NEST) end_operator(&pl);
        endscope = true;
        goto RETURN;
      }
      scan_warning(sc, "closing ']' without opening '['");
      break;
    case '{':
      if (parse_level(o, &pl, pl.use_type, SCOPE_GROUP, '}'))
        goto RETURN;
      break;
    case '|':
      if (pl.use_type != SGS_POP_CARR && pl.event != NULL)
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        scan_stashc(sc, c);
        goto RETURN;
      }
      end_event(&pl);
      if (!o->group_start) {
        scan_warning(sc, "no sounds precede time separator");
        break;
      }
      flush_durgroup(o);
      pl.sub_f = NULL;
      break;
    case '}':
      if (c == close_c) goto RETURN;
      scan_warning(sc, "closing '}' without opening '{'");
      break;
    default:
    INVALID:
      if (!handle_unknown_or_end(sc)) goto FINISH;
      break;
    }
  }
FINISH:
  if (close_c == ']' && c != close_c)
    scan_warning(sc, "end of file without closing ']'s");
  if (close_c == '}' && c != close_c)
    scan_warning(sc, "end of file without closing '}'s");
RETURN:
  end_scope(&pl);
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && pl.scope != newscope);
}

/*
 * Process file.
 *
 * The file is closed after parse,
 * but the SGS_File instance is not destroyed.
 *
 * \return true if completed, false on error preventing parse
 */
static bool parse_file(SGS_Parser *restrict o, SGS_File *restrict f) {
  init_scanner(&o->sc, f, o->st, &o->sl);
  parse_level(o, 0, SGS_POP_CARR, SCOPE_GROUP, 0);
  fini_scanner(&o->sc);
  return true;
}

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SGS_ScriptEvData *restrict e_last) {
  SGS_ScriptEvData *e, *e_after = e_last->next;
  uint32_t cur_longest = 0, wait_sum = 0, wait_after = 0;
  for (e = e_last->group_backref; e != e_after; ) {
    for (SGS_ScriptOpData *op = e->operators.first_on; op; op = op->next) {
      if (cur_longest < op->time.v_ms)
        cur_longest = op->time.v_ms;
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
    for (SGS_ScriptOpData *op = e->operators.first_on; op; op = op->next) {
      if (!(op->time.flags & SGS_TIMEP_SET)) {
        /* fill in sensible default time */
        op->time.v_ms = cur_longest + wait_sum;
        op->time.flags |= SGS_TIMEP_SET;
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

static void time_operator(SGS_ScriptOpData *restrict op) {
  SGS_ScriptEvData *e = op->event;
  if (!(op->op_params & SGS_POPP_TIME))
    e->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
  if (!(op->time.flags & SGS_TIMEP_SET) &&
      (op->op_flags & SGS_SDOP_NESTED) != 0) {
    op->time.flags |= SGS_TIMEP_IMPLICIT;
    op->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
  }
  if (!(op->time.flags & SGS_TIMEP_IMPLICIT)) {
    if ((op->freq.flags & SGS_RAP_TIME_DEFAULT) != 0)
      op->freq.time_ms = op->time.v_ms;
    if ((op->amp.flags & SGS_RAP_TIME_DEFAULT) != 0)
      op->amp.time_ms = op->time.v_ms;
  }
  if (!(op->op_flags & SGS_SDOP_SILENCE_ADDED)) {
    op->time.v_ms += op->silence_ms;
    op->op_flags |= SGS_SDOP_SILENCE_ADDED;
  }
  if ((e->ev_flags & SGS_SDEV_ADD_WAIT_DURATION) != 0) {
    if (e->next != NULL)
      e->next->wait_ms += op->time.v_ms;
    e->ev_flags &= ~SGS_SDEV_ADD_WAIT_DURATION;
  }
  if (op->amods) for (SGS_ScriptOpData *subop = op->amods->first_on; subop;
       subop = subop->next) {
    time_operator(subop);
  }
  if (op->fmods) for (SGS_ScriptOpData *subop = op->fmods->first_on; subop;
       subop = subop->next) {
    time_operator(subop);
  }
  if (op->pmods) for (SGS_ScriptOpData *subop = op->pmods->first_on; subop;
       subop = subop->next) {
    time_operator(subop);
  }
}

static void time_event(SGS_ScriptEvData *restrict e) {
  /*
   * Adjust default ramp durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  // e->pan.flags &= ~SGS_RAP_TIME_DEFAULT; // TODO: revisit semantics
  for (SGS_ScriptOpData *op = e->operators.first_on; op; op = op->next) {
    time_operator(op);
  }
  /*
   * Timing for sub-events - done before event list flattened.
   */
  SGS_ScriptEvBranch *fork = e->forks;
  while (fork != NULL) {
    SGS_ScriptEvData *ce = fork->events;
    SGS_ScriptOpData *ce_op = ce->operators.first_on,
                    *ce_op_prev = ce_op->on_prev,
                    *e_op = ce_op_prev;
    e_op->time.flags |= SGS_TIMEP_SET; /* always used from now on */
    if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
      e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
    for (;;) {
      ce->wait_ms += ce_op_prev->time.v_ms;
      if (!(ce_op->time.flags & SGS_TIMEP_SET)) {
        ce_op->time.flags |= SGS_TIMEP_SET;
        if (ce_op->op_flags & SGS_SDOP_NESTED)
          ce_op->time.flags |= SGS_TIMEP_IMPLICIT;
        else
          ce_op->time.v_ms = ce_op_prev->time.v_ms - ce_op_prev->silence_ms;
      }
      time_event(ce);
      if (ce_op->time.flags & SGS_TIMEP_IMPLICIT)
        e_op->time.flags |= SGS_TIMEP_IMPLICIT;
      e_op->time.v_ms += ce_op->time.v_ms +
                         (ce->wait_ms - ce_op_prev->time.v_ms);
      ce_op->op_params &= ~SGS_POPP_TIME;
      ce_op_prev = ce_op;
      ce = ce->next;
      if (!ce) break;
      ce_op = ce->operators.first_on;
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
static void flatten_events(SGS_ScriptEvData *restrict e) {
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
static void postparse_passes(SGS_Parser *restrict o) {
  SGS_ScriptEvData *e;
  for (e = o->events; e; e = e->next) {
    if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
      e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
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
SGS_Script* SGS_read_Script(SGS_File *restrict f) {
  if (!f) return NULL;

  SGS_Parser pr;
  init_parser(&pr);
  const char *name = f->path;
  SGS_Script *o = NULL;
  if (!parse_file(&pr, f)) {
    goto DONE;
  }
  postparse_passes(&pr);
  o = SGS_Mempool_alloc(pr.mp, sizeof(SGS_Script));
  o->mp = pr.mp;
  o->st = pr.st;
  o->events = pr.events;
  o->name = name;
  o->sopt = pr.sl.sopt;
  pr.mp = NULL; // keep with result
  pr.st = NULL; // keep with result

DONE:
  fini_parser(&pr);
  return o;
}

/**
 * Destroy instance.
 */
void SGS_discard_Script(SGS_Script *restrict o) {
  if (!o)
    return;
  SGS_destroy_Symtab(o->st);
  SGS_destroy_Mempool(o->mp);
}
