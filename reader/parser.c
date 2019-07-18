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
 * <https://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include "file.h"
#include "../script.h"
#include "../help.h"
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

enum {
  SGS_SYM_VAR = 0,
  SGS_SYM_RAMP_ID,
  SGS_SYM_WAVE_ID,
  SGS_SYM_TYPES
};

typedef struct ScanLookup {
  SGS_ScriptOptions sopt;
} ScanLookup;

typedef struct PScanner {
  SGS_File *f;
  uint32_t line;
  uint8_t c, next_c;
  bool newline;
  bool stash;
  SGS_SymTab *st;
  ScanLookup *sl;
} PScanner;

static void init_scanner(PScanner *restrict o,
                         SGS_File *restrict f,
                         SGS_SymTab *restrict st,
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

static SGS_SymItem *scan_sym(PScanner *restrict o, uint32_t type_id, char op);

typedef double (*NumSym_f)(PScanner *restrict o);

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
    num = scan_num_r(o, NUMEXP_ADT, level+1);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (c == '$') {
    SGS_SymItem *var = scan_sym(sc, SGS_SYM_VAR, c);
    if (!var) goto REJECT;
    if (var->data_use != SGS_SYM_DATA_NUM) {
      scan_warning(sc,
"variable used in numerical expression doesn't hold a number");
      goto REJECT;
    }
    num = var->data.num;
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
static bool sgsNoinline scan_num(PScanner *restrict o,
                                 NumSym_f scan_numsym, double *restrict var) {
  NumParser np = {o, scan_numsym, false, false};
  double num = scan_num_r(&np, NUMEXP_SUB, 0);
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
static double scan_note(PScanner *restrict o) {
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
  double freq;
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
static SGS_SymItem *scan_sym(PScanner *restrict o, uint32_t type_id, char op) {
  LabelBuf buf;
  SGS_File *f = o->f;
  size_t len = 0;
  bool truncated;
  char nolabel_msg[] = "ignoring '?' without name";
  nolabel_msg[10] = op; /* replace ? */
  truncated = !SGS_File_getstr(f, buf, LABEL_BUFLEN, &len, filter_symchar);
  if (len == 0) {
    scan_warning(o, nolabel_msg);
    return NULL;
  }
  if (truncated) {
    scan_warning(o, "limiting symbol name to "LABEL_MAXLEN_S" characters");
    SGS_File_skipstr(f, filter_symchar);
  }
  SGS_SymStr *s = SGS_SymTab_get_symstr(o->st, buf, len);
  SGS_SymItem *item = SGS_SymTab_find_item(o->st, s, type_id);
  if (!item && type_id == SGS_SYM_VAR)
    item = SGS_SymTab_add_item(o->st, s, SGS_SYM_VAR);
  if (!item)
    return NULL;
  o->c = SGS_File_RETC(f);
  return item;
}

static bool scan_wavetype(PScanner *restrict o, size_t *restrict found_id,
                          char op) {
  SGS_SymItem *item = scan_sym(o, SGS_SYM_WAVE_ID, op);
  if (item) {
    *found_id = item->data.id;
    return true;
  }
  const char *const *names = SGS_Wave_names;
  scan_warning(o, "invalid wave type; available types are:");
  SGS_print_names(names, "\t", stderr);
  return false;
}

static bool scan_ramp_state(PScanner *restrict o,
                            NumSym_f scan_numsym,
                            SGS_Ramp *restrict ramp, bool ratio) {
  double v0;
  if (!scan_num(o, scan_numsym, &v0))
    return false;
  ramp->v0 = v0;
  if (ratio) {
    ramp->flags |= SGS_RAMPP_STATE_RATIO;
  } else {
    ramp->flags &= ~SGS_RAMPP_STATE_RATIO;
  }
  ramp->flags |= SGS_RAMPP_STATE;
  return true;
}

static bool scan_ramp(PScanner *restrict o, NumSym_f scan_numsym,
                      SGS_Ramp *restrict ramp, bool ratio) {
  bool state = scan_ramp_state(o, scan_numsym, ramp, ratio);
  if (!SGS_File_TRYC(o->f, '{'))
    return state;
  bool goal = false;
  bool time_set = (ramp->flags & SGS_RAMPP_TIME) != 0;
  double vt, time;
  uint32_t time_ms = o->sl->sopt.def_time_ms;
  uint8_t type = ramp->type; // has default
  if ((ramp->flags & SGS_RAMPP_GOAL) != 0) {
    // allow partial change
    if (((ramp->flags & SGS_RAMPP_GOAL_RATIO) != 0) == ratio) {
      goal = true;
      vt = ramp->vt;
    }
    time_ms = ramp->time_ms;
  }
  for (;;) {
    uint8_t c = scan_getc(o);
    switch (c) {
    case SCAN_NEWLINE:
      break;
    case 'c': {
      SGS_SymItem *item = scan_sym(o, SGS_SYM_RAMP_ID, c);
      if (item) {
        type = item->data.id;
        break;
      }
      const char *const *names = SGS_Ramp_names;
      scan_warning(o, "invalid ramp type; available types are:");
      SGS_print_names(names, "\t", stderr);
      break; }
    case 't':
      if (scan_num(o, 0, &time)) {
        if (time < 0.f) {
          scan_warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        time_ms = lrint(time * 1000.f);
        time_set = true;
      }
      break;
    case 'v':
      if (scan_num(o, scan_numsym, &vt))
        goal = true;
      break;
    case '}':
      goto RETURN;
    default:
      if (!handle_unknown_or_end(o)) goto FINISH;
      break;
    }
  }
FINISH:
  scan_warning(o, "end of file without closing '}'");
RETURN:
  if (!goal) {
    scan_warning(o, "ignoring value ramp with no target value");
    return false;
  }
  ramp->vt = vt;
  ramp->time_ms = time_ms;
  ramp->type = type;
  ramp->flags |= SGS_RAMPP_GOAL;
  if (ratio)
    ramp->flags |= SGS_RAMPP_GOAL_RATIO;
  else
    ramp->flags &= ~SGS_RAMPP_GOAL_RATIO;
  if (time_set)
    ramp->flags |= SGS_RAMPP_TIME;
  else
    ramp->flags &= ~SGS_RAMPP_TIME;
  return true;
}

/*
 * Parser
 */

typedef struct SGS_Parser {
  PScanner sc;
  ScanLookup sl;
  SGS_SymTab *st;
  SGS_MemPool *mp;
  uint32_t call_level;
  uint32_t scope_id;
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
  .def_relfreq = 1.f,
};

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static bool init_parser(SGS_Parser *restrict o) {
  *o = (SGS_Parser){0};
  o->mp = SGS_create_MemPool(0);
  o->st = SGS_create_SymTab(o->mp);
  if (!o->st ||
      !SGS_SymTab_add_stra(o->st, SGS_Ramp_names, SGS_RAMP_TYPES,
                          SGS_SYM_RAMP_ID) ||
      !SGS_SymTab_add_stra(o->st, SGS_Wave_names, SGS_WAVE_TYPES,
                          SGS_SYM_WAVE_ID))
    return false;
  o->sl.sopt = def_sopt;
  return true;
}

/*
 * Finalize parser instance.
 */
static void fini_parser(SGS_Parser *restrict o) {
  SGS_destroy_SymTab(o->st);
  SGS_destroy_MemPool(o->mp);
}

/*
 * Scope values.
 */
enum {
  SCOPE_SAME = 0,
  SCOPE_TOP = 1,
  SCOPE_BIND,
  SCOPE_NEST,
};

/*
 * Current "location" (what is being parsed/worked on) for parse level.
 */
enum {
  SDPL_IN_NONE = 0, // no target for parameters
  SDPL_IN_DEFAULTS, // adjusting default values
  SDPL_IN_EVENT,    // adjusting operator and/or voice
};

/*
 * Parse level flags.
 */
enum {
  SDPL_BIND_MULTIPLE = 1<<0, // previous node interpreted as set of nodes
  SDPL_NESTED_SCOPE = 1<<1,
  SDPL_NEW_EVENT_FORK = 1<<2,
  SDPL_ACTIVE_EV = 1<<3,
  SDPL_ACTIVE_OP = 1<<4,
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
  uint8_t location;
  uint8_t scope;
  SGS_ScriptEvData *event;
  SGS_ScriptOpData *operator, *first_operator, *last_operator;
  SGS_ScriptOpData *parent_on, *on_prev;
  uint8_t use_type;
  uint8_t last_use_type; /* FIXME: kludge */
  SGS_SymItem *set_var; /* variable assigned to next node */
  /* timing/delay */
  SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
  float used_ampmult; /* update on node creation */
  SGS_ScriptOptions sopt_save; /* save/restore on nesting */
} ParseLevel;

typedef struct SGS_ScriptEvBranch {
  SGS_ScriptEvData *events;
  struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

static bool parse_waittime(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  double wait;
  uint32_t wait_ms;
  scan_num(sc, 0, &wait);
  if (wait < 0.f) {
    scan_warning(sc, "ignoring '\\' with sub-zero time");
    return false;
  }
  wait_ms = lrint(wait * 1000.f);
  pl->next_wait_ms += wait_ms;
  return true;
}

/*
 * Node- and scope-handling functions
 */

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SGS_ScriptOpData *restrict op) {
  SGS_PtrArr_clear(&op->on_next);
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->fmods);
  for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrArr_clear(&op->fmods);
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->pmods);
  for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrArr_clear(&op->pmods);
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->amods);
  for (i = op->amods.old_count; i < op->amods.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrArr_clear(&op->amods);
  free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SGS_ScriptEvData *restrict e) {
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_PtrArr_clear(&e->operators);
  SGS_PtrArr_clear(&e->op_graph);
  free(e);
}

static void end_operator(ParseLevel *restrict pl) {
  if (!(pl->pl_flags & SDPL_ACTIVE_OP))
    return;
  pl->pl_flags &= ~SDPL_ACTIVE_OP;
  SGS_ScriptOpData *op = pl->operator;
  if (SGS_Ramp_ENABLED(&op->freq))
    op->op_params |= SGS_POPP_FREQ;
  if (SGS_Ramp_ENABLED(&op->freq2))
    op->op_params |= SGS_POPP_FREQ2;
  if (SGS_Ramp_ENABLED(&op->amp)) {
    op->op_params |= SGS_POPP_AMP;
    op->amp.v0 *= pl->used_ampmult;
    op->amp.vt *= pl->used_ampmult;
  }
  if (SGS_Ramp_ENABLED(&op->amp2)) {
    op->op_params |= SGS_POPP_AMP2;
    op->amp2.v0 *= pl->used_ampmult;
    op->amp2.vt *= pl->used_ampmult;
  }
  SGS_ScriptOpData *pop = op->on_prev;
  if (!pop) {
    /*
     * Reset all operator state for initial event.
     */
    op->op_params = SGS_POP_PARAMS;
  } else {
    if (op->wave != pop->wave)
      op->op_params |= SGS_POPP_WAVE;
    /* SGS_TIME set when time set */
    /* SGS_PHASE set when phase set */
  }
  pl->operator = NULL;
  pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
  if (!(pl->pl_flags & SDPL_ACTIVE_EV))
    return;
  pl->pl_flags &= ~SDPL_ACTIVE_EV;
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e = pl->event;
  end_operator(pl);
  if (SGS_Ramp_ENABLED(&e->pan))
    e->vo_params |= SGS_PVOP_PAN;
  SGS_ScriptEvData *pve = e->voice_prev;
  if (!pve) {
    /*
     * Reset all voice state for initial event.
     */
    e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
    e->vo_params = SGS_PVO_PARAMS & ~SGS_PVOP_GRAPH;
  }
  pl->event = NULL;
  SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
  if (!o->group_start)
    o->group_start = group_e;
  o->group_end = group_e;
}

static void begin_event(ParseLevel *restrict pl, bool is_compstep) {
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e, *pve;
  end_event(pl);
  pl->event = calloc(1, sizeof(SGS_ScriptEvData));
  e = pl->event;
  e->wait_ms = pl->next_wait_ms;
  pl->next_wait_ms = 0;
  SGS_Ramp_reset(&e->pan);
  if (pl->on_prev != NULL) {
    SGS_ScriptEvBranch *fork;
    if (pl->on_prev->op_flags & SGS_SDOP_NESTED)
      e->ev_flags |= SGS_SDEV_IMPLICIT_TIME;
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
  } else {
    /*
     * New voice with initial parameter values.
     */
    e->pan.v0 = 0.5f; /* center */
    e->pan.flags |= SGS_RAMPP_STATE;
  }
  if (!is_compstep) {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    pl->main_ev = NULL;
  }
  pl->pl_flags |= SDPL_ACTIVE_EV;
}

static void begin_operator(ParseLevel *restrict pl, uint8_t use_type,
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
  pl->used_ampmult = o->sl.sopt.ampmult;
  /*
   * Initialize node.
   */
  SGS_Ramp_reset(&op->freq);
  SGS_Ramp_reset(&op->freq2);
  SGS_Ramp_reset(&op->amp);
  SGS_Ramp_reset(&op->amp2);
  if (pop != NULL) {
    pop->op_flags |= SGS_SDOP_LATER_USED;
    op->on_prev = pop;
    op->op_flags = pop->op_flags & (SGS_SDOP_NESTED |
                                    SGS_SDOP_MULTIPLE);
    op->time = (SGS_Time){pop->time.v_ms,
      (pop->time.flags & SGS_TIMEP_IMPLICIT)};
    op->wave = pop->wave;
    op->phase = pop->phase;
    SGS_PtrArr_soft_copy(&op->fmods, &pop->fmods);
    SGS_PtrArr_soft_copy(&op->pmods, &pop->pmods);
    SGS_PtrArr_soft_copy(&op->amods, &pop->amods);
    if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
      SGS_ScriptOpData *mpop = pop;
      uint32_t max_time = 0;
      do {
        if (max_time < mpop->time.v_ms) max_time = mpop->time.v_ms;
        SGS_PtrArr_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound) != NULL);
      op->op_flags |= SGS_SDOP_MULTIPLE;
      op->time.v_ms = max_time;
      pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
    } else {
      SGS_PtrArr_add(&pop->on_next, op);
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
    if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
      op->freq.v0 = o->sl.sopt.def_freq;
    } else {
      op->op_flags |= SGS_SDOP_NESTED;
      op->freq.v0 = o->sl.sopt.def_relfreq;
      op->freq.flags |= SGS_RAMPP_STATE_RATIO;
    }
    op->freq.flags |= SGS_RAMPP_STATE;
    op->amp.v0 = 1.0f;
    op->amp.flags |= SGS_RAMPP_STATE;
  }
  op->event = e;
  /*
   * Add new operator to parent(s), ie. either the current event node,
   * or an operator node (either ordinary or representing multiple
   * carriers) in the case of operator linking/nesting.
   */
  if (pop != NULL ||
      use_type == SGS_POP_CARR) {
    SGS_PtrArr_add(&e->operators, op);
    if (!pop) {
      e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
      SGS_PtrArr_add(&e->op_graph, op);
    }
  } else {
    SGS_PtrArr *list = NULL;
    switch (use_type) {
    case SGS_POP_FMOD:
      list = &pl->parent_on->fmods;
      break;
    case SGS_POP_PMOD:
      list = &pl->parent_on->pmods;
      break;
    case SGS_POP_AMOD:
      list = &pl->parent_on->amods;
      break;
    }
    SGS_PtrArr_add(list, op);
  }
  /*
   * Make a variable point to this?
   */
  if (pl->set_var != NULL) {
    pl->set_var->data_use = SGS_SYM_DATA_OBJ;
    pl->set_var->data.obj = op;
    pl->set_var = NULL;
  }
  pl->pl_flags |= SDPL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
                       SGS_ScriptOpData *restrict previous,
                       uint8_t use_type, bool is_compstep) {
  pl->on_prev = previous;
  if (!pl->event ||
      pl->location != SDPL_IN_EVENT /* previous event implicitly ended */ ||
      pl->next_wait_ms ||
      is_compstep)
    begin_event(pl, is_compstep);
  begin_operator(pl, use_type, is_compstep);
  pl->last_use_type = use_type; /* FIXME: kludge */
}

static void flush_durgroup(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  pl->next_wait_ms = 0; /* does not cross boundaries */
  if (o->group_start != NULL) {
    o->group_end->group_backref = o->group_start;
    o->group_start = o->group_end = NULL;
  }
}

static void begin_scope(SGS_Parser *restrict o, ParseLevel *restrict pl,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope) {
  *pl = (ParseLevel){0};
  pl->o = o;
  pl->scope = newscope;
  if (parent_pl != NULL) {
    pl->parent = parent_pl;
    pl->pl_flags = parent_pl->pl_flags &
                   (SDPL_NESTED_SCOPE | SDPL_BIND_MULTIPLE);
    pl->location = parent_pl->location;
    if (newscope == SCOPE_SAME)
      pl->scope = parent_pl->scope;
    pl->event = parent_pl->event;
    pl->operator = parent_pl->operator;
    pl->parent_on = parent_pl->parent_on;
    if (newscope == SCOPE_NEST) {
      pl->pl_flags |= SDPL_NESTED_SCOPE;
      pl->parent_on = parent_pl->operator;
      /*
       * Push script options, reset parts of state for new context.
       */
      parent_pl->sopt_save = o->sl.sopt;
      o->sl.sopt.set = 0;
      o->sl.sopt.ampmult = def_sopt.ampmult; // separate each level
    }
  }
  pl->use_type = use_type;
}

static void end_scope(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  end_operator(pl);
  if (pl->scope == SCOPE_BIND) {
    /*
     * Begin multiple-operator node in parent scope for the operator nodes in
     * this scope, provided any are present.
     */
    if (pl->first_operator != NULL) {
      pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
      begin_node(pl->parent, pl->first_operator, pl->parent->last_use_type, false);
    }
  } else if (pl->scope == SCOPE_NEST) {
    /*
     * Pop script options.
     */
    o->sl.sopt = pl->parent->sopt_save;
  } else if (!pl->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    end_event(pl);
    flush_durgroup(pl);
  }
  if (pl->set_var != NULL) {
    scan_warning(&o->sc, "ignoring variable assignment without object");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  pl->location = SDPL_IN_DEFAULTS;
  for (;;) {
    uint8_t c = scan_getc(sc);
    double val;
    switch (c) {
    case 'a':
      if (scan_num(sc, NULL, &val)) {
        o->sl.sopt.ampmult = val;
        o->sl.sopt.set |= SGS_SOPT_AMPMULT;
      }
      break;
    case 'f':
      if (scan_num(sc, scan_note, &val)) {
        o->sl.sopt.def_freq = val;
        o->sl.sopt.set |= SGS_SOPT_DEF_FREQ;
      }
      if (SGS_File_TRYC(sc->f, ',') && SGS_File_TRYC(sc->f, 'n')) {
        if (scan_num(sc, NULL, &val)) {
          if (val < 1.f) {
            scan_warning(sc, "ignoring tuning frequency (Hz) below 1.0");
            break;
          }
          o->sl.sopt.A4_freq = val;
          o->sl.sopt.set |= SGS_SOPT_A4_FREQ;
        }
      }
      break;
    case 'r':
      if (scan_num(sc, NULL, &val)) {
        o->sl.sopt.def_relfreq = val;
        o->sl.sopt.set |= SGS_SOPT_DEF_RELFREQ;
      }
      break;
    case 't':
      if (scan_num(sc, 0, &val)) {
        if (val < 0.f) {
          scan_warning(sc, "ignoring 't' with sub-zero time");
          break;
        }
        o->sl.sopt.def_time_ms = lrint(val * 1000.f);
        o->sl.sopt.set |= SGS_SOPT_DEF_TIME;
      }
      break;
    default:
    /*UNKNOWN:*/
      scan_stashc(sc, c);
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  SGS_ScriptOpData *op = pl->operator;
  scan_ramp(sc, NULL, &op->amp, false);
  if (SGS_File_TRYC(f, ',') && SGS_File_TRYC(f, 'w')) {
    scan_ramp(sc, NULL, &op->amp2, false);
    if (SGS_File_TRYC(f, '[')) {
      op->mods_set |= (1<<SGS_POP_AMOD);
      SGS_PtrArr_clear(&op->amods);
      parse_level(o, pl, SGS_POP_AMOD, SCOPE_NEST);
    }
  }
  return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  SGS_ScriptOpData *op = pl->operator;
  if (rel_freq && !(op->op_flags & SGS_SDOP_NESTED))
    return true; // reject
  NumSym_f numsym_f = rel_freq ? NULL : scan_note;
  scan_ramp(sc, numsym_f, &op->freq, rel_freq);
  if (SGS_File_TRYC(f, ',') && SGS_File_TRYC(f, 'w')) {
    scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
    if (SGS_File_TRYC(f, '[')) {
      op->mods_set |= (1<<SGS_POP_FMOD);
      SGS_PtrArr_clear(&op->fmods);
      parse_level(o, pl, SGS_POP_FMOD, SCOPE_NEST);
    }
  }
  return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  SGS_ScriptOpData *op = pl->operator;
  double val;
  if (scan_num(sc, NULL, &val)) {
    val = fmod(val, 1.f);
    if (val < 0.f)
      val += 1.f;
    op->phase = val;
    op->op_params |= SGS_POPP_PHASE;
  }
  if (SGS_File_TRYC(f, '[')) {
    op->mods_set |= (1<<SGS_POP_PMOD);
    SGS_PtrArr_clear(&op->pmods);
    parse_level(o, pl, SGS_POP_PMOD, SCOPE_NEST);
  }
  return false;
}

static bool parse_step(ParseLevel *restrict pl) {
  SGS_Parser *o = pl->o;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  pl->location = SDPL_IN_EVENT;
  for (;;) {
    SGS_ScriptEvData *e = pl->event;
    SGS_ScriptOpData *op = pl->operator;
    uint8_t c = scan_getc(sc);
    double val;
    switch (c) {
    case 'P':
      if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
        goto UNKNOWN;
      scan_ramp(sc, NULL, &e->pan, false);
      break;
    case '/':
      if (parse_waittime(pl)) {
        // FIXME: Buggy update node handling for carriers etc. if enabled.
        //begin_node(pl, pl->operator, 0, false);
      }
      break;
    case '\\':
      if (parse_waittime(pl)) {
        begin_node(pl, pl->operator, 0, true);
        pl->event->ev_flags |= SGS_SDEV_FROM_GAPSHIFT;
      }
      break;
    case 'a':
      if (parse_ev_amp(pl)) goto UNKNOWN;
      break;
    case 'f':
      if (parse_ev_freq(pl, false)) goto UNKNOWN;
      break;
    case 'p':
      if (parse_ev_phase(pl)) goto UNKNOWN;
      break;
    case 'r':
      if (parse_ev_freq(pl, true)) goto UNKNOWN;
      break;
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
        if (!scan_num(sc, 0, &val)) break;
        if (val < 0.f) {
          scan_warning(sc, "ignoring 't' with sub-zero time");
          break;
        }
        op->time = (SGS_Time){lrint(val * 1000.f),
          SGS_TIMEP_SET};
      }
      op->op_params |= SGS_POPP_TIME;
      break;
    case 'w': {
      size_t wave;
      if (!scan_wavetype(sc, &wave, c))
        break;
      op->wave = wave;
      break; }
    default:
    UNKNOWN:
      scan_stashc(sc, c);
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
static bool parse_level(SGS_Parser *restrict o,
                        ParseLevel *restrict parent_pl,
                        uint8_t use_type, uint8_t newscope) {
  ParseLevel pl;
  uint8_t flags = 0;
  bool endscope = false;
  begin_scope(o, &pl, parent_pl, use_type, newscope);
  ++o->call_level;
  PScanner *sc = &o->sc;
  SGS_File *f = sc->f;
  for (;;) {
    uint8_t c = scan_getc(sc);
    switch (c) {
    case SCAN_NEWLINE:
      if (pl.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->call_level > 1)
          goto RETURN;
        flags = 0;
        pl.location = SDPL_IN_NONE;
        pl.first_operator = NULL;
      }
      break;
    case '\'':
      /*
       * Variable assignment, part 1; set to what follows.
       */
      if (pl.set_var != NULL) {
        scan_warning(sc, "ignoring variable assignment to variable assignment");
        break;
      }
      pl.set_var = scan_sym(sc, SGS_SYM_VAR, c);
      break;
    case '/':
      if (pl.location == SDPL_IN_DEFAULTS ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
        goto INVALID;
      parse_waittime(&pl);
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        scan_stashc(sc, c);
        goto RETURN;
      }
      if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
        goto INVALID;
      if ((pl.operator->time.flags & (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
          == (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
        scan_warning(sc, "ignoring 'ti' (implicit time) before ';' separator");
      begin_node(&pl, pl.operator, 0, true);
      pl.event->ev_flags |= SGS_SDEV_WAIT_PREV_DUR;
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '=': {
      SGS_SymItem *var = pl.set_var;
      if (!var) {
        scan_warning(sc, "ignoring dangling '='");
        break;
      }
      pl.set_var = NULL; // used here
      if (scan_num(sc, NULL, &var->data.num))
        var->data_use = SGS_SYM_DATA_NUM;
      else
        scan_warning(sc, "missing right-hand value for variable '='");
      break; }
    case '@': {
      if (SGS_File_TRYC(f, '[')) {
        end_operator(&pl);
        if (parse_level(o, &pl, pl.use_type, SCOPE_BIND))
          goto RETURN;
        /*
         * Multiple-operator node now open.
         */
        flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        break;
      }
      /*
       * Variable reference (get and use object).
       */
      pl.location = SDPL_IN_NONE;
      SGS_SymItem *var = scan_sym(sc, SGS_SYM_VAR, c);
      if (var != NULL) {
	if (var->data_use == SGS_SYM_DATA_OBJ) {
          SGS_ScriptOpData *ref = var->data.obj;
          begin_node(&pl, ref, 0, false);
          ref = pl.operator;
          var->data.obj = ref;
          flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
	} else {
          scan_warning(sc, "reference doesn't point to an object");
	}
      }
      break; }
    case 'O': {
      size_t wave;
      if (!scan_wavetype(sc, &wave, c))
        break;
      begin_node(&pl, 0, pl.use_type, false);
      pl.operator->wave = wave;
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      flags = parse_settings(&pl) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '[':
      scan_warning(sc, "opening '[' out of place");
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
      scan_warning(sc, "closing ']' without opening '['");
      break;
    case '{':
      scan_warning(sc, "opening '{' out of place");
      break;
    case '|':
      if (pl.location == SDPL_IN_DEFAULTS ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        scan_stashc(sc, c);
        goto RETURN;
      }
      end_event(&pl);
      flush_durgroup(&pl);
      pl.location = SDPL_IN_NONE;
      break;
    case '}':
      scan_warning(sc, "closing '}' without opening '{'");
      break;
    default:
    INVALID:
      if (!handle_unknown_or_end(sc)) goto FINISH;
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
    scan_warning(sc, "end of file without closing ']'s");
RETURN:
  end_scope(&pl);
  --o->call_level;
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
  parse_level(o, 0, SGS_POP_CARR, SCOPE_TOP);
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
  size_t i;
  uint32_t cur_longest = 0, wait_sum = 0, wait_after = 0;
  for (e = e_last->group_backref; e != e_after; ) {
    if ((e->ev_flags & SGS_SDEV_VOICE_SET_DUR) != 0 &&
        cur_longest < e->dur_ms)
      cur_longest = e->dur_ms;
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
    ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ScriptOpData *op = ops[i];
      if (!(op->time.flags & SGS_TIMEP_SET)) {
        /* fill in sensible default time */
        op->time.v_ms = cur_longest + wait_sum;
        op->time.flags |= SGS_TIMEP_SET;
        if (e->dur_ms < op->time.v_ms)
          e->dur_ms = op->time.v_ms;
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

static inline void time_ramp(SGS_Ramp *restrict ramp,
                             uint32_t default_time_ms) {
  if (!(ramp->flags & SGS_RAMPP_TIME))
    ramp->time_ms = default_time_ms;
}

static uint32_t time_operator(SGS_ScriptOpData *restrict op) {
  uint32_t dur_ms = op->time.v_ms;
  if (!(op->op_params & SGS_POPP_TIME))
    op->event->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
  if (!(op->time.flags & SGS_TIMEP_SET)) {
    op->time.flags |= SGS_TIMEP_DEFAULT;
    if (op->op_flags & SGS_SDOP_NESTED) {
      op->time.flags |= SGS_TIMEP_IMPLICIT;
      op->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
    }
  }
  if (!(op->time.flags & SGS_TIMEP_IMPLICIT)) {
    time_ramp(&op->freq, op->time.v_ms);
    time_ramp(&op->freq2, op->time.v_ms);
    time_ramp(&op->amp, op->time.v_ms);
    time_ramp(&op->amp2, op->time.v_ms);
  }
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->fmods);
  for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->pmods);
  for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->amods);
  for (i = op->amods.old_count; i < op->amods.count; ++i) {
    time_operator(ops[i]);
  }
  return dur_ms;
}

static uint32_t time_event(SGS_ScriptEvData *restrict e) {
  uint32_t dur_ms = 0;
  // e->pan.flags |= SGS_RAMPP_TIME; // TODO: revisit semantics
  size_t i;
  SGS_ScriptOpData **ops;
  ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    uint32_t sub_dur_ms = time_operator(ops[i]);
    if (dur_ms < sub_dur_ms)
      dur_ms = sub_dur_ms;
  }
  /*
   * Timing for sub-events - done before event list flattened.
   */
  SGS_ScriptEvBranch *fork = e->forks;
  while (fork != NULL) {
    uint32_t nest_dur_ms = 0, wait_sum_ms = 0;
    SGS_ScriptEvData *ne = fork->events, *ne_prev = e;
    SGS_ScriptOpData *ne_op =
            (SGS_ScriptOpData*) SGS_PtrArr_GET(&ne->operators, 0),
            *ne_op_prev = ne_op->on_prev, *e_op = ne_op_prev;
    uint32_t first_time_ms = e_op->time.v_ms;
    SGS_Time def_time = {
      e_op->time.v_ms, (e_op->time.flags & SGS_TIMEP_IMPLICIT)
    };
    e->dur_ms = first_time_ms; /* for first value in series */
    if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
      e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
    for (;;) {
      wait_sum_ms += ne->wait_ms;
      if (!(ne_op->time.flags & SGS_TIMEP_SET)) {
        ne_op->time = def_time;
        if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT)
          ne_op->time.flags |= SGS_TIMEP_SET | SGS_TIMEP_DEFAULT;
      }
      time_event(ne);
      def_time = (SGS_Time){
        ne_op->time.v_ms, (ne_op->time.flags & SGS_TIMEP_IMPLICIT)
      };
      if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT) {
        if (ne_op_prev->time.flags & SGS_TIMEP_DEFAULT
            && !(ne_prev->ev_flags & SGS_SDEV_FROM_GAPSHIFT)) {
          ne_op_prev->time = (SGS_Time){ // gap
            0, SGS_TIMEP_SET | SGS_TIMEP_DEFAULT
          };
        }
      }
      if (ne->ev_flags & SGS_SDEV_WAIT_PREV_DUR) {
        ne->wait_ms += ne_op_prev->time.v_ms;
        ne_op_prev->time.flags &= ~SGS_TIMEP_IMPLICIT;
      }
      if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
        nest_dur_ms = wait_sum_ms + ne->dur_ms;
      first_time_ms += ne->dur_ms +
        (ne->wait_ms - ne_prev->dur_ms);
      ne_op->time.flags |= SGS_TIMEP_SET;
      ne_op->op_params |= SGS_POPP_TIME;
      ne_op_prev = ne_op;
      ne_prev = ne;
      ne = ne->next;
      if (!ne) break;
      ne_op = (SGS_ScriptOpData*) SGS_PtrArr_GET(&ne->operators, 0);
    }
    if (dur_ms < first_time_ms)
      dur_ms = first_time_ms;
    //if (dur_ms < nest_dur_ms)
    //  dur_ms = nest_dur_ms;
    fork = fork->prev;
  }
  e->dur_ms = dur_ms; /* unfinished estimate used to adjust timing */
  return dur_ms;
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
  o = calloc(1, sizeof(SGS_Script));
  o->events = pr.events;
  o->name = name;
  o->sopt = pr.sl.sopt;

DONE:
  fini_parser(&pr);
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
