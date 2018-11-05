/* sgensys: Script file parser.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "scanner.h"
#include "symtab.h"
#include "script.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

#define STRBUF_LEN 256

typedef struct LookupData {
  SGS_ScriptOptions sopt;
  SGS_SymTab *st;
  const char *const*wave_names;
  const char *const*slope_names;
  uint8_t strbuf[STRBUF_LEN];
} LookupData;

/*
 * Default script options, used until changed in a script.
 */
static const SGS_ScriptOptions def_sopt = {
  .changed = 0,
  .ampmult = 1.f,
  .A4_freq = 444.f,
  .def_time_ms = 1000,
  .def_freq = 444.f,
  .def_ratio = 1.f,
};

static void destroy_LookupData(LookupData *o) {
  if (o->st) SGS_destroy_SymTab(o->st);
  free(o);
}

static LookupData *create_LookupData(void) {
  LookupData *o = calloc(1, sizeof(LookupData));
  if (!o) return NULL;
  o->sopt = def_sopt;
  SGS_SymTab *st = SGS_create_SymTab();
  if (!st) goto ERROR;
  o->st = st;
  o->wave_names = SGS_SymTab_pool_stra(st, SGS_Wave_names, SGS_WAVE_TYPES);
  if (!o->wave_names) goto ERROR;
  o->slope_names = SGS_SymTab_pool_stra(st, SGS_Slope_names, SGS_SLOPE_TYPES);
  if (!o->slope_names) goto ERROR;
  return o;

ERROR:
  destroy_LookupData(o);
  return NULL;
}

/*
 * Read identifier string. If a valid symbol string was read,
 * the copy set to \p strp will be the unique copy stored
 * in the symbol table. If no string was read,
 * \p strp will be set to NULL. \p lenp will be set to
 * the length of the string, or 0 if none.
 *
 * \return true if string not truncated
 */
bool scan_syms(SGS_Scanner *restrict o,
               const void **restrict strp, uint32_t *restrict lenp) {
  LookupData *ld = o->data;
  bool truncated = !SGS_Scanner_getsyms(o, ld->strbuf,
                     (STRBUF_LEN - 1), lenp);
  if (*lenp == 0) {
    *strp = NULL;
    return true;
  }
  const char *pool_str = SGS_SymTab_pool_str(ld->st, ld->strbuf, *lenp);
  if (pool_str == NULL) {
    SGS_Scanner_error(o, NULL,
      "failed to register string '%s'",
      ld->strbuf);
  }
  *strp = pool_str;
  return !truncated;
}

#define OCTAVES 11
static float scan_note(SGS_Scanner *restrict o) {
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
  LookupData *ld = o->data;
  float freq;
  uint8_t c = SGS_Scanner_getc(o);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  uint32_t read_len;
  if (c >= 'a' && c <= 'g') {
    subnote = c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    c = SGS_Scanner_getc(o);
  }
  if (c < 'A' || c > 'G') {
    SGS_Scanner_warning(o, NULL,
      "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  c = SGS_Scanner_getc(o);
  if (c == 's')
    semitone = 2;
  else if (c == 'f')
    semitone = 0;
  else
    SGS_Scanner_ungetc(o);
  SGS_Scanner_geti(o, &octave, false, &read_len);
  if (read_len == 0)
    octave = 4;
  else if (octave >= OCTAVES) {
    SGS_Scanner_warning(o, NULL,
      "invalid octave specified for note - valid range 0-10");
    octave = 4;
  }
  freq = ld->sopt.A4_freq * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
}

static const char *scan_label(SGS_Scanner *restrict o,
                              uint32_t *restrict len, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  const void *s = NULL;
  nolabel_msg[9] = op; /* replace ? */
  scan_syms(o, &s, len);
  if (*len == 0) {
    SGS_Scanner_warning(o, NULL, nolabel_msg);
  }
  return s;
}

static int32_t scan_symafind(SGS_Scanner *restrict o,
                             const char *const*restrict stra, size_t n) {
  const void *key = NULL;
  uint32_t len;
  scan_syms(o, &key, &len);
  if (len == 0) {
    SGS_Scanner_warning(o, NULL, "name missing");
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    if (stra[i] == key) {
      return i;
    }
  }
  return -1;
}

static int32_t scan_wavetype(SGS_Scanner *restrict o) {
  SGS_ScanFrame sf = o->sf;
  LookupData *ld = o->data;
  const char *const *names = ld->wave_names;
  int32_t wave = scan_symafind(o, names, SGS_WAVE_TYPES);
  if (wave < 0) {
    SGS_Scanner_warning(o, &sf,
      "invalid wave type; available types are:");
    uint8_t i = 0;
    fprintf(stderr, "\t%s", names[i]);
    while (++i < SGS_WAVE_TYPES) {
      fprintf(stderr, ", %s", names[i]);
    }
    putc('\n', stderr);
  }
  return wave;
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool handle_unknown_or_end(SGS_Scanner *restrict o, uint8_t c) {
  if (c == 0)
    return false;
  if (IS_VISIBLE(c)) {
    SGS_Scanner_warning(o, NULL, "invalid character '%c'", c);
  } else {
    SGS_Scanner_warning(o, NULL, "invalid character (value 0x%02hhX)", c);
  }
  return true;
}

/*
 * Parser
 */

typedef struct SGS_Parser {
  LookupData *ld;
  SGS_Scanner *sc;
  SGS_SymTab *st;
  uint32_t call_level;
  uint32_t scope_id;
  /* node state */
  SGS_ScriptEvData *events;
  SGS_ScriptEvData *last_event;
} SGS_Parser;

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static void init_Parser(SGS_Parser *restrict o) {
  *o = (SGS_Parser){0};
  o->ld = create_LookupData();
  o->sc = SGS_create_Scanner();
  o->sc->data = o->ld;
}

/*
 * Finalize parser instance.
 */
static void fini_Parser(SGS_Parser *restrict o) {
  destroy_LookupData(o->ld);
  SGS_destroy_Scanner(o->sc);
}

enum {
  /* parsing scopes */
  SCOPE_SAME = 0,
  SCOPE_TOP = 1,
  SCOPE_BIND = '{',
  SCOPE_NEST = '<',
};

enum {
  SDPL_IN_DEFAULTS = 1<<0, /* adjusting default values */
  SDPL_IN_NODE = 1<<1,     /* adjusting operator and/or voice */
  SDPL_NESTED_SCOPE = 1<<2,
  SDPL_BIND_MULTIPLE = 1<<3, /* previous node interpreted as set of nodes */
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
  SGS_ScriptEvData *group_from; /* where to begin for group_events() */
  SGS_ScriptEvData *composite; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

typedef float (*ScanSym_f)(SGS_Scanner *o);

static float parse_num_r(SGS_Parser *restrict o, ScanSym_f scan_symbol,
                         uint8_t pri, uint32_t level) {
  SGS_Scanner *sc = o->sc;
  float num;
  bool minus = false;
  uint32_t read_len;
  char c;
  if (level > 0) SGS_Scanner_skipws(sc);
  c = SGS_Scanner_getc(sc);
  if (c == '(') {
    num = parse_num_r(o, scan_symbol, 255, level+1);
    if (level == 0) return num;
    goto LOOP;
  }
  if (c == '-') {
    if (level > 0) SGS_Scanner_skipws(sc);
    c = SGS_Scanner_getc(sc);
    minus = true;
  }
  if (IS_DIGIT(c) || c == '.') {
    SGS_Scanner_ungetc(sc);
    double dnum;
    SGS_Scanner_getd(sc, &dnum, false, &read_len);
    if (read_len == 0)
      return NAN;
    num = (float) dnum;
    if (minus) num = -num;
  } else if (scan_symbol && IS_ALPHA(c)) {
    SGS_Scanner_ungetc(sc);
    num = scan_symbol(sc);
    if (num != num) /* not recognized */
      return NAN;
  } else {
    return NAN;
  }
LOOP:
  for (;;) {
    if (level > 0) SGS_Scanner_skipws(sc);
    c = SGS_Scanner_getc(sc);
    switch (c) {
    case '(':
      num *= parse_num_r(o, scan_symbol, 255, level+1);
      break;
    case ')':
      if (pri < 255)
        SGS_Scanner_ungetc(sc);
      return num;
      break;
    case '^':
      num = exp(log(num) * parse_num_r(o, scan_symbol, 0, level));
      break;
    case '*':
      num *= parse_num_r(o, scan_symbol, 1, level);
      break;
    case '/':
      num /= parse_num_r(o, scan_symbol, 1, level);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += parse_num_r(o, scan_symbol, 2, level);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= parse_num_r(o, scan_symbol, 2, level);
      break;
    default:
      SGS_Scanner_ungetc(sc);
      return num;
    }
    if (num != num) {
      SGS_Scanner_ungetc(sc);
      return num;
    }
  }
}
static bool parse_num(SGS_Parser *restrict o, ScanSym_f scan_symbol,
                      float *restrict var, bool mul_inv) {
  SGS_ScanFrame sf = o->sc->sf;
  float num = parse_num_r(o, scan_symbol, 254, 0);
  if (mul_inv) num = 1.f / num;
  if (fabs(num) == INFINITY) {
    SGS_Scanner_warning(o->sc, &sf, "discarding infinite number");
    return false;
  }
  if (num != num)
    return false;
  *var = num;
  return true;
}

static bool parse_time(SGS_Parser *restrict o, float *restrict var) {
  SGS_ScanFrame sf = o->sc->sf;
  float num;
  if (!parse_num(o, NULL, &num, false))
    return false;
  if (num < 0.f) {
    SGS_Scanner_warning(o->sc, &sf, "discarding negative time value");
    return false;
  }
  *var = num;
  return true;
}

static bool parse_tpar_state(SGS_Parser *restrict o,
                             ScanSym_f scan_symbol,
                             SGS_TimedParam *restrict tpar, bool ratio) {
  if (!parse_num(o, scan_symbol, &tpar->v0, ratio))
    return false;
  if (ratio) {
    tpar->flags |= SGS_TPAR_STATE_RATIO;
  } else {
    tpar->flags &= ~SGS_TPAR_STATE_RATIO;
  }
  tpar->flags |= SGS_TPAR_STATE;
  return true;
}

static bool parse_tpar_slope(SGS_Parser *restrict o,
                             ScanSym_f scan_symbol,
                             SGS_TimedParam *restrict tpar, bool ratio) {
  SGS_Scanner *sc = o->sc;
  LookupData *ld = o->ld;
  bool goal = false;
  float vt;
  uint32_t time_ms = SGS_TIME_DEFAULT;
  uint8_t slope = tpar->slope; // has default
  if ((tpar->flags & SGS_TPAR_SLOPE) != 0) {
    // allow partial change
    if (((tpar->flags & SGS_TPAR_SLOPE_RATIO) != 0) == ratio) {
      goal = true;
      vt = tpar->vt;
    }
    time_ms = tpar->time_ms;
  }
  for (;;) {
    uint8_t c;
    SGS_Scanner_skipspace(sc);
    c = SGS_Scanner_getc(sc);
    switch (c) {
    case SGS_SCAN_LNBRK:
      break;
    case 'c': {
      SGS_ScanFrame sf = sc->sf;
      const char *const *names = ld->slope_names;
      int32_t type = scan_symafind(sc, names, SGS_SLOPE_TYPES);
      if (type < 0) {
        SGS_Scanner_warning(sc, &sf,
          "invalid slope change type; available types are:");
        uint8_t i = 0;
        fprintf(stderr, "\t%s", names[i]);
        while (++i < SGS_SLOPE_TYPES) {
          fprintf(stderr, ", %s", names[i]);
        }
        putc('\n', stderr);
        break;
      }
      slope = type;
      break; }
    case 't': {
      float time;
      if (parse_time(o, &time)) {
        time_ms = lrint(time * 1000.f);
      }
      break; }
    case 'v':
      if (!parse_num(o, scan_symbol, &vt, ratio))
        break;
      goal = true;
      break;
    case ']':
      goto RETURN;
    default:
      if (!handle_unknown_or_end(sc, c)) goto FINISH;
      break;
    }
  }
FINISH:
  SGS_Scanner_warning(sc, NULL, "end of file without closing ']'");
RETURN:
  if (!goal) {
    SGS_Scanner_warning(sc, NULL, "ignoring value slope with no target value");
    return false;
  }
  tpar->vt = vt;
  tpar->time_ms = time_ms;
  tpar->slope = slope;
  if (ratio) {
    tpar->flags |= SGS_TPAR_SLOPE_RATIO;
  } else {
    tpar->flags &= ~SGS_TPAR_SLOPE_RATIO;
  }
  tpar->flags |= SGS_TPAR_SLOPE;
  return true;
}

static bool parse_waittime(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  SGS_Scanner *sc = o->sc;
  /* FIXME: ADD_WAIT_DURATION */
  if (SGS_Scanner_tryc(sc, 't')) {
    if (!pl->last_operator) {
      SGS_Scanner_warning(sc, NULL,
        "add wait for last duration before any parts given");
      return false;
    }
    pl->last_event->ev_flags |= SGS_SDEV_ADD_WAIT_DURATION;
  } else {
    float wait;
    uint32_t wait_ms;
    if (parse_time(o, &wait)) {
      wait_ms = lrint(wait * 1000.f);
      pl->next_wait_ms += wait_ms;
    }
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
static void destroy_operator(SGS_ScriptOpData *restrict op) {
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
static void destroy_event_node(SGS_ScriptEvData *restrict e) {
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

static void end_operator(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  LookupData *ld = o->ld;
  SGS_ScriptOpData *op = pl->operator;
  if (!op)
    return; /* nothing to do */
  if (SGS_TimedParam_ENABLED(&op->freq))
    op->op_params |= SGS_POPP_FREQ;
  if (SGS_TimedParam_ENABLED(&op->amp)) {
    op->op_params |= SGS_POPP_AMP;
    if (!(pl->pl_flags & SDPL_NESTED_SCOPE))
      op->amp.v0 *= ld->sopt.ampmult;
  }
  SGS_ScriptOpData *pop = op->on_prev;
  if (!pop) {
    /*
     * Reset remaining operator state for initial event.
     */
    op->op_params |= SGS_POPP_ADJCS |
                     SGS_POPP_WAVE |
                     SGS_POPP_TIME |
                     SGS_POPP_SILENCE |
                     SGS_POPP_DYNFREQ |
                     SGS_POPP_PHASE |
                     SGS_POPP_DYNAMP;
  } else {
    if (op->wave != pop->wave)
      op->op_params |= SGS_POPP_WAVE;
    /* SGS_TIME set when time set */
    if (op->silence_ms != 0)
      op->op_params |= SGS_POPP_SILENCE;
    if (op->dynfreq != pop->dynfreq)
      op->op_params |= SGS_POPP_DYNFREQ;
    /* SGS_PHASE set when phase set */
    if (op->dynamp != pop->dynamp)
      op->op_params |= SGS_POPP_DYNAMP;
  }
  pl->operator = NULL;
  pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
  SGS_ScriptEvData *e = pl->event;
  if (!e)
    return; /* nothing to do */
  end_operator(pl);
  if (SGS_TimedParam_ENABLED(&e->pan))
    e->vo_params |= SGS_PVOP_PAN;
  SGS_ScriptEvData *pve = e->voice_prev;
  if (!pve) {
    /*
     * Reset remaining voice state for initial event.
     */
    e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
  }
  pl->last_event = e;
  pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl, uint8_t linktype,
                        bool is_composite) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e, *pve;
  end_event(pl);
  pl->event = calloc(1, sizeof(SGS_ScriptEvData));
  e = pl->event;
  e->wait_ms = pl->next_wait_ms;
  pl->next_wait_ms = 0;
  SGS_TimedParam_reset(&e->pan);
  if (pl->on_prev != NULL) {
    pve = pl->on_prev->event;
    pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
    if (pve->composite != NULL && !is_composite) {
      SGS_ScriptEvData *last_ce;
      for (last_ce = pve->composite; last_ce->next; last_ce = last_ce->next) ;
      last_ce->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
    }
    e->voice_prev = pve;
  } else {
    /*
     * New voice with initial parameter values.
     */
    e->pan.v0 = 0.5f; /* center */
    e->pan.flags |= SGS_TPAR_STATE;
  }
  if (!pl->group_from)
    pl->group_from = e;
  if (is_composite) {
    if (!pl->composite) {
      pve->composite = e;
      pl->composite = pve;
    } else {
      pve->next = e;
    }
  } else {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    pl->composite = NULL;
  }
}

static void begin_operator(ParseLevel *restrict pl, uint8_t linktype,
                           bool is_composite) {
  SGS_Parser *o = pl->o;
  LookupData *ld = o->ld;
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
  if (!is_composite && pl->last_operator != NULL)
    pl->last_operator->next_bound = op;
  /*
   * Initialize node.
   */
  SGS_TimedParam_reset(&op->freq);
  SGS_TimedParam_reset(&op->amp);
  if (pop != NULL) {
    pop->op_flags |= SGS_SDOP_LATER_USED;
    op->on_prev = pop;
    op->op_flags = pop->op_flags & (SGS_SDOP_NESTED |
                                    SGS_SDOP_MULTIPLE);
    if (is_composite)
      op->op_flags |= SGS_SDOP_TIME_DEFAULT; /* default: previous or infinite time */
    op->time_ms = pop->time_ms;
    op->wave = pop->wave;
    op->phase = pop->phase;
    op->dynfreq = pop->dynfreq;
    op->dynamp = pop->dynamp;
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
    op->time_ms = ld->sopt.def_time_ms;
    if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
      op->freq.v0 = ld->sopt.def_freq;
    } else {
      op->op_flags |= SGS_SDOP_NESTED;
      op->freq.v0 = ld->sopt.def_ratio;
      op->freq.flags |= SGS_TPAR_STATE_RATIO;
    }
    op->freq.flags |= SGS_TPAR_STATE;
    op->amp.v0 = 1.0f;
    op->amp.flags |= SGS_TPAR_STATE;
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
   * Assign label. If no new label but previous node (for a non-composite)
   * has one, update label to point to new node, but keep pointer in
   * previous node.
   */
  if (pl->set_label != NULL) {
    SGS_SymTab_set(ld->st, pl->set_label, strlen(pl->set_label), op);
    op->label = pl->set_label;
    pl->set_label = NULL;
  } else if (!is_composite && pop != NULL && pop->label != NULL) {
    SGS_SymTab_set(ld->st, pop->label, strlen(pop->label), op);
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
static void begin_node(ParseLevel *restrict pl,
                       SGS_ScriptOpData *restrict previous,
                       uint8_t linktype, bool is_composite) {
  pl->on_prev = previous;
  if (!pl->event ||
      !in_current_node(pl) /* previous event implicitly ended */ ||
      pl->next_wait_ms ||
      is_composite)
    begin_event(pl, linktype, is_composite);
  begin_operator(pl, linktype, is_composite);
  pl->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SGS_Parser *restrict o, ParseLevel *restrict pl,
                        ParseLevel *restrict parent_pl,
                        uint8_t linktype, uint8_t newscope) {
  *pl = (ParseLevel){0};
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
    if (newscope == SCOPE_BIND)
      pl->group_from = parent_pl->group_from;
    if (newscope == SCOPE_NEST) {
      pl->pl_flags |= SDPL_NESTED_SCOPE;
      pl->parent_on = parent_pl->operator;
    }
  }
  pl->linktype = linktype;
}

static void end_scope(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  end_operator(pl);
  if (pl->scope == SCOPE_BIND) {
    if (!pl->parent->group_from)
      pl->parent->group_from = pl->group_from;
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
    SGS_ScriptEvData *group_to;
    end_event(pl);
    group_to = (pl->composite) ? pl->composite : pl->last_event;
    if (group_to)
      group_to->groupfrom = pl->group_from;
  }
  if (pl->set_label != NULL) {
    SGS_Scanner_warning(o->sc, NULL,
      "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  LookupData *ld = o->ld;
  SGS_Scanner *sc = o->sc;
  enter_defaults(pl);
  leave_current_node(pl);
  for (;;) {
    uint8_t c;
    SGS_Scanner_skipspace(sc);
    c = SGS_Scanner_getc(sc);
    switch (c) {
    case 'a':
      if (parse_num(o, NULL, &ld->sopt.ampmult, false)) {
        ld->sopt.changed |= SGS_SOPT_AMPMULT;
      }
      break;
    case 'f':
      if (parse_num(o, scan_note, &ld->sopt.def_freq, false)) {
        ld->sopt.changed |= SGS_SOPT_DEF_FREQ;
      }
      break;
    case 'n': {
      float freq;
      if (parse_num(o, NULL, &freq, false)) {
        if (freq < 1.f) {
          SGS_Scanner_warning(sc, NULL,
            "ignoring tuning frequency (Hz) below 1.0");
          break;
        }
        ld->sopt.A4_freq = freq;
        ld->sopt.changed |= SGS_SOPT_A4_FREQ;
      }
      break; }
    case 'r':
      if (parse_num(o, NULL, &ld->sopt.def_ratio, true)) {
        ld->sopt.changed |= SGS_SOPT_DEF_RATIO;
      }
      break;
    case 't': {
      float time;
      if (parse_time(o, &time)) {
        ld->sopt.def_time_ms = lrint(time * 1000.f);
        ld->sopt.changed |= SGS_SOPT_DEF_TIME;
      }
      break; }
    default:
      SGS_Scanner_ungetc(sc);
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t linktype, uint8_t newscope);

static bool parse_step(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  LookupData *ld = o->ld;
  SGS_Scanner *sc = o->sc;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptOpData *op = pl->operator;
  leave_defaults(pl);
  enter_current_node(pl);
  for (;;) {
    uint8_t c;
    SGS_Scanner_skipspace(sc);
    c = SGS_Scanner_getc(sc);
    switch (c) {
    case 'P':
      if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
        goto UNKNOWN;
      if (SGS_Scanner_tryc(sc, '[')) {
        parse_tpar_slope(o, NULL, &e->pan, false);
      } else {
        parse_tpar_state(o, NULL, &e->pan, false);
      }
      break;
    case '\\':
      if (parse_waittime(pl)) {
        begin_node(pl, pl->operator, NL_REFER, false);
      }
      break;
    case 'a':
      if (SGS_Scanner_tryc(sc, '!')) {
        if (!SGS_File_TESTC(sc->f, '<')) {
          parse_num(o, NULL, &op->dynamp, false);
        }
        if (SGS_Scanner_tryc(sc, '<')) {
          if (op->amods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->amods);
          }
          parse_level(o, pl, NL_AMODS, SCOPE_NEST);
        }
      } else if (SGS_Scanner_tryc(sc, '[')) {
        parse_tpar_slope(o, NULL, &op->amp, false);
      } else {
        parse_tpar_state(o, NULL, &op->amp, false);
      }
      break;
    case 'f':
      if (SGS_Scanner_tryc(sc, '!')) {
        if (!SGS_File_TESTC(sc->f, '<')) {
          parse_num(o, NULL, &op->dynfreq, false);
        }
        if (SGS_Scanner_tryc(sc, '<')) {
          if (op->fmods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->fmods);
          }
          parse_level(o, pl, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_Scanner_tryc(sc, '[')) {
        parse_tpar_slope(o, scan_note, &op->freq, false);
      } else {
        parse_tpar_state(o, scan_note, &op->freq, false);
      }
      break;
    case 'p':
      if (SGS_Scanner_tryc(sc, '+')) {
        if (SGS_Scanner_tryc(sc, '<')) {
          if (op->pmods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->pmods);
          }
          parse_level(o, pl, NL_PMODS, SCOPE_NEST);
        } else {
          SGS_Scanner_ungetc(sc);
          goto UNKNOWN;
        }
      } else if (parse_num(o, NULL, &op->phase, false)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->op_params |= SGS_POPP_PHASE;
      }
      break;
    case 'r':
      if (!(pl->pl_flags & SDPL_NESTED_SCOPE))
        goto UNKNOWN;
      if (SGS_Scanner_tryc(sc, '!')) {
        if (!SGS_File_TESTC(sc->f, '<')) {
          parse_num(o, NULL, &op->dynfreq, true);
        }
        if (SGS_Scanner_tryc(sc, '<')) {
          if (op->fmods.count > 0) {
            op->op_params |= SGS_POPP_ADJCS;
            SGS_PtrList_clear(&op->fmods);
          }
          parse_level(o, pl, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_Scanner_tryc(sc, '[')) {
        parse_tpar_slope(o, NULL, &op->freq, true);
      } else {
        parse_tpar_state(o, NULL, &op->freq, true);
      }
      break;
    case 's': {
      float silence;
      if (parse_time(o, &silence)) {
        op->silence_ms = lrint(silence * 1000.f);
      }
      break; }
    case 't':
      if (SGS_Scanner_tryc(sc, '*')) {
        op->op_flags |= SGS_SDOP_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = ld->sopt.def_time_ms;
      } else if (SGS_Scanner_tryc(sc, 'i')) {
        if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
          SGS_Scanner_warning(sc, NULL,
            "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        if (parse_time(o, &time)) {
          op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
          op->time_ms = lrint(time * 1000.f);
        }
      }
      op->op_params |= SGS_POPP_TIME;
      break;
    case 'w': {
      int32_t wave = scan_wavetype(sc);
      if (wave < 0)
        break;
      op->wave = wave;
      break; }
    default:
    UNKNOWN:
      SGS_Scanner_ungetc(sc);
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

enum {
  HANDLE_DEFER = 1<<1,
  DEFERRED_STEP = 1<<2,
  DEFERRED_SETTINGS = 1<<4
};
static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t linktype, uint8_t newscope) {
  ParseLevel pl;
  LookupData *ld = o->ld;
  const char *label;
  uint32_t label_len;
  uint8_t flags = 0;
  bool endscope = false;
  begin_scope(o, &pl, parent_pl, linktype, newscope);
  ++o->call_level;
  SGS_Scanner *sc = o->sc;
  for (;;) {
    uint8_t c;
    SGS_Scanner_skipspace(sc);
    c = SGS_Scanner_getc(sc);
    switch (c) {
    case SGS_SCAN_LNBRK:
      if (pl.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->call_level > 1)
          goto RETURN;
        flags = 0;
        leave_defaults(&pl);
        if (in_current_node(&pl)) {
          leave_current_node(&pl);
        }
        pl.first_operator = NULL;
      }
      break;
    case ':':
      if (pl.set_label != NULL) {
        SGS_Scanner_warning(sc, NULL,
          "ignoring label assignment to label reference");
        pl.set_label = NULL;
      }
      leave_defaults(&pl);
      leave_current_node(&pl);
      label = scan_label(sc, &label_len, ':');
      if (label_len > 0) {
        SGS_ScriptOpData *ref = SGS_SymTab_get(ld->st, label, label_len);
        if (!ref)
          SGS_Scanner_warning(sc, NULL,
            "ignoring reference to undefined label");
        else {
          begin_node(&pl, ref, NL_REFER, false);
          flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        SGS_Scanner_ungetc(sc);
        goto RETURN;
      }
      if (in_defaults(&pl) || !pl.event)
        goto INVALID;
      begin_node(&pl, pl.operator, NL_REFER, true);
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '<':
      if (parse_level(o, &pl, pl.linktype, '<'))
        goto RETURN;
      break;
    case '>':
      if (pl.scope != SCOPE_NEST) {
        SGS_Scanner_warning(sc, NULL, "closing '>' without opening '<'");
        break;
      }
      end_operator(&pl);
      endscope = true;
      goto RETURN;
    case 'O': {
      int32_t wave = scan_wavetype(sc);
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
    case '\\':
      if (in_defaults(&pl) ||
          (pl.pl_flags & SDPL_NESTED_SCOPE && pl.event))
        goto INVALID;
      parse_waittime(&pl);
      break;
    case '\'':
      if (pl.set_label != NULL) {
        SGS_Scanner_warning(sc, NULL,
          "ignoring label assignment to label assignment");
        break;
      }
      label = scan_label(sc, &label_len, '\'');
      pl.set_label = label;
      break;
    case '{':
      end_operator(&pl);
      if (parse_level(o, &pl, pl.linktype, SCOPE_BIND))
        goto RETURN;
      /*
       * Multiple-operator node will now be ready for parsing.
       */
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '|':
      if (in_defaults(&pl) ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        SGS_Scanner_ungetc(sc);
        goto RETURN;
      }
      if (!pl.event) {
        SGS_Scanner_warning(sc, NULL,
          "end of sequence before any parts given");
        break;
      }
      if (pl.group_from != NULL) {
        SGS_ScriptEvData *group_to = (pl.composite) ?
                                     pl.composite :
                                     pl.event;
        group_to->groupfrom = pl.group_from;
        pl.group_from = NULL;
      }
      end_event(&pl);
      leave_current_node(&pl);
      break;
    case '}':
      if (pl.scope != SCOPE_BIND) {
        SGS_Scanner_warning(sc, NULL, "closing '}' without opening '{'");
        break;
      }
      endscope = true;
      goto RETURN;
    default:
    INVALID:
      if (!handle_unknown_or_end(sc, c)) goto FINISH;
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
  if (newscope == SCOPE_NEST)
    SGS_Scanner_warning(sc, NULL, "end of file without closing '>'s");
  if (newscope == SCOPE_BIND)
    SGS_Scanner_warning(sc, NULL, "end of file without closing '}'s");
RETURN:
  end_scope(&pl);
  --o->call_level;
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && pl.scope != newscope);
}

/*
 * Process file.
 *
 * \return true if completed, false on error preventing parse
 */
static bool parse_file(SGS_Parser *restrict o, const char *restrict fname) {
  SGS_Scanner *sc = o->sc;
  if (!SGS_Scanner_fopenrb(sc, fname)) {
    return false;
  }
  parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
  SGS_Scanner_close(sc);
  return true;
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SGS_ScriptEvData *restrict to) {
  SGS_ScriptEvData *e, *e_after = to->next;
  size_t i;
  uint32_t wait = 0, waitcount = 0;
  for (e = to->groupfrom; e != e_after; ) {
    SGS_ScriptOpData **ops;
    ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ScriptOpData *op = ops[i];
      if (e->next == e_after &&
          i == (e->operators.count - 1) &&
          (op->op_flags & SGS_SDOP_TIME_DEFAULT) != 0) /* default for last node in group */
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
      if (wait < op->time_ms)
        wait = op->time_ms;
    }
    e = e->next;
    if (e != NULL) {
      /*wait -= e->wait_ms;*/
      waitcount += e->wait_ms;
    }
  }
  for (e = to->groupfrom; e != e_after; ) {
    SGS_ScriptOpData **ops;
    ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ScriptOpData *op = ops[i];
      if ((op->op_flags & SGS_SDOP_TIME_DEFAULT) != 0) {
        op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
        op->time_ms = wait + waitcount; /* fill in sensible default time */
      }
    }
    e = e->next;
    if (e != NULL) {
      waitcount -= e->wait_ms;
    }
  }
  to->groupfrom = NULL;
  if (e_after != NULL)
    e_after->wait_ms += wait;
}

static void time_operator(SGS_ScriptOpData *restrict op) {
  SGS_ScriptEvData *e = op->event;
  if (op->freq.time_ms == SGS_TIME_DEFAULT)
    op->freq.time_ms = op->time_ms;
  if (op->amp.time_ms == SGS_TIME_DEFAULT)
    op->amp.time_ms = op->time_ms;
  if ((op->op_flags & (SGS_SDOP_TIME_DEFAULT | SGS_SDOP_NESTED)) ==
                      (SGS_SDOP_TIME_DEFAULT | SGS_SDOP_NESTED)) {
    op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
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

static void time_event(SGS_ScriptEvData *restrict e) {
  /*
   * Fill in blank slope durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->pan.time_ms == SGS_TIME_DEFAULT)
    e->pan.time_ms = 1000; /* FIXME! */
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrList_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    time_operator(ops[i]);
  }
  /*
   * Timing for composites - done before event list flattened.
   */
  if (e->composite != NULL) {
    SGS_ScriptEvData *ce = e->composite;
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
  }
}

/*
 * Deals with events that are "composite" (attached to a main event as
 * successive "sub-events" rather than part of the big, linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SGS_ScriptEvData *restrict e) {
  SGS_ScriptEvData *ce = e->composite;
  SGS_ScriptEvData *se = e->next, *se_prev = e;
  int32_t wait_ms = 0;
  int32_t added_wait_ms = 0;
  while (ce != NULL) {
    if (!se) {
      /*
       * No more events in the ordinary sequence, so append all composites.
       */
      se_prev->next = ce;
      break;
    }
    /*
     * If several events should pass in the ordinary sequence before the next
     * composite is inserted, skip ahead.
     */
    wait_ms += se->wait_ms;
    if (se->next &&
        (wait_ms + se->next->wait_ms) <= (ce->wait_ms + added_wait_ms)) {
      se_prev = se;
      se = se->next;
      continue;
    }
    /*
     * Insert next composite before or after the next event of the ordinary
     * sequence.
     */
    if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
      SGS_ScriptEvData *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      SGS_ScriptEvData *se_next, *ce_next;
      se_next = se->next;
      ce_next = ce->next;
      ce->wait_ms -= wait_ms;
      added_wait_ms += ce->wait_ms;
      wait_ms = 0;
      se->next = ce;
      ce->next = se_next;
      se_prev = ce;
      se = se_next;
      ce = ce_next;
    }
  }
  e->composite = NULL;
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
    time_event(e);
    if (e->groupfrom != NULL) group_events(e);
  }
  /*
   * Must be separated into pass following timing adjustments for events;
   * otherwise, flattening will fail to arrange events in the correct order
   * in some cases.
   */
  for (e = o->events; e; e = e->next) {
    if (e->composite != NULL) flatten_events(e);
  }
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SGS_Script* SGS_load_Script(const char *restrict fname) {
  SGS_Parser pr;
  init_Parser(&pr);
  SGS_Script *o = NULL;
  if (!parse_file(&pr, fname)) {
    goto DONE;
  }

  postparse_passes(&pr);
  o = calloc(1, sizeof(SGS_Script));
  o->events = pr.events;
  o->name = fname;
  o->sopt = pr.ld->sopt;

DONE:
  fini_Parser(&pr);
  return o;
}

/**
 * Destroy instance.
 */
void SGS_discard_Script(SGS_Script *restrict o) {
  SGS_ScriptEvData *e;
  for (e = o->events; e; ) {
    SGS_ScriptEvData *e_next = e->next;
    destroy_event_node(e);
    e = e_next;
  }
  free(o);
}
