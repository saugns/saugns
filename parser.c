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
#include "mempool.h"
#include "script.h"
#include "math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * File-reading functions
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

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

static uint8_t test_symchar(SGS_File *f SGS__maybe_unused, uint8_t c) {
  return IS_SYMCHAR(c);
}

static bool read_sym(SGS_File *f, char *buf, size_t buf_len,
                     size_t *sym_len) {
  size_t i = 0;
  size_t max_len = buf_len - 1;
  bool truncate = false;
  for (;;) {
    if (i == max_len) {
      truncate = true;
      break;
    }
    uint8_t c = SGS_File_GETC(f);
    if (!IS_SYMCHAR(c)) {
      SGS_File_UNGETC(f);
      break;
    }
    buf[i++] = c;
  }
  buf[i] = '\0';
  *sym_len = i;
  return !truncate;
}

static int32_t read_strfind(SGS_File *f, const char *const*str) {
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
    uint8_t c = SGS_File_GETC(f);
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
    if (c <= SGS_FILE_MARKER) break;
    if (pos == len) break;
    ++pos;
  }
  free(s);
  SGS_File_UNGETN(f, (pos-matchpos));
  return ret;
}

/*
 * Parser
 */

typedef struct SGS_Parser {
  SGS_File *f;
  SGS_Symtab *st;
  SGS_Mempool *mp;
  uint32_t line;
  uint32_t call_level;
  uint32_t scope_id;
  uint8_t c, next_c;
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
static void init_parser(SGS_Parser *o) {
  *o = (SGS_Parser){0};
  o->mp = SGS_create_Mempool(0);
  o->st = SGS_create_Symtab();
  o->sopt = def_sopt;
}

/*
 * Finalize parser instance.
 */
static void fini_parser(SGS_Parser *o) {
  SGS_destroy_Symtab(o->st);
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
  SDPL_OWN_EV = 1<<3,
  SDPL_OWN_OP = 1<<4,
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
  uint8_t linktype;
  SGS_ScriptEvData *event, *last_event;
  SGS_ScriptListData *nest_list;
  SGS_ScriptOpData *operator, *scope_first, *ev_last, *nest_last;
  SGS_ScriptOpData *parent_on, *on_prev;
  const char *set_label;
  /* timing/delay */
  SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
  float used_ampmult; /* update on node creation */
} ParseLevel;

typedef struct SGS_ScriptEvBranch {
  SGS_ScriptEvData *events;
  struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void SGS__noinline scan_warning(SGS_Parser *o, const char *str) {
  SGS_File *f = o->f;
  uint8_t c = o->c;
  if (IS_VISIBLE(c)) {
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
static uint8_t scan_char(SGS_Parser *o) {
  uint8_t c;
  SGS_File_skipspace(o->f);
  if (o->next_c != 0) {
    c = o->next_c;
    o->next_c = 0;
  } else {
    c = SGS_File_GETC(o->f);
  }
  if (c == '#') {
    SGS_File_skipline(o->f);
    c = SGS_File_GETC(o->f);
  }
  if (c == '\n') {
    SGS_File_TRYC(o->f, '\r');
    c = SCAN_NEWLINE;
  } else if (c == '\r') {
    c = SCAN_NEWLINE;
  } else {
    SGS_File_skipspace(o->f);
  }
  o->c = c;
  return c;
}

static void scan_ws(SGS_Parser *o) {
  for (;;) {
    uint8_t c = SGS_File_GETC(o->f);
    if (IS_SPACE(c))
      continue;
    if (c == '\n') {
      ++o->line;
      SGS_File_TRYC(o->f, '\r');
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      SGS_File_skipline(o->f);
      c = SGS_File_GETC(o->f);
    } else {
      SGS_File_UNGETC(o->f);
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
static bool handle_unknown_or_end(SGS_Parser *o) {
  if (SGS_File_AT_EOF(o->f) ||
      SGS_File_AFTER_EOF(o->f)) {
    return false;
  }
  scan_warning(o, "invalid character");
  return true;
}

typedef float (*NumSym_f)(SGS_Parser *o);

typedef struct NumParser {
  SGS_Parser *pr;
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
static double scan_num_r(NumParser *o, uint8_t pri, uint32_t level) {
  SGS_Parser *pr = o->pr;
  double num;
  uint8_t c;
  if (level > 0) scan_ws(pr);
  c = SGS_File_GETC(pr->f);
  if (c == '(') {
    num = scan_num_r(o, NUMEXP_SUB, level+1);
  } else if (c == '+' || c == '-') {
    num = scan_num_r(o, NUMEXP_ADT, level);
    if (isnan(num)) goto DEFER;
    if (c == '-') num = -num;
  } else if (o->numsym_f && IS_ALPHA(c)) {
    SGS_File_UNGETC(pr->f);
    num = o->numsym_f(pr);
    if (isnan(num)) goto REJECT;
  } else {
    size_t read_len;
    SGS_File_UNGETC(pr->f);
    SGS_File_getd(pr->f, &num, false, &read_len);
    if (read_len == 0) goto REJECT;
  }
  if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
  for (;;) {
    bool rpar_mlt = false;
    if (isinf(num)) o->has_infnum = true;
    if (level > 0) scan_ws(pr);
    c = SGS_File_GETC(pr->f);
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
        SGS_File_UNGETC(pr->f);
        double rval = scan_num_r(o, NUMEXP_MLT, level);
        if (isnan(rval)) goto ACCEPT;
        num *= rval;
        break;
      }
      if (pri == NUMEXP_SUB && level > 0) {
        scan_warning(pr, "numerical expression has '(' without closing ')'");
      }
      goto DEFER;
    }
    if (isnan(num)) goto DEFER;
  }
DEFER:
  SGS_File_UNGETC(pr->f);
ACCEPT:
  if (0)
REJECT: {
    num = NAN;
  }
  return num;
}
static bool scan_num(SGS_Parser *o, NumSym_f scan_numsym, float *var) {
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
static float scan_note(SGS_Parser *o) {
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
  o->c = SGS_File_GETC(o->f);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  size_t read_len;
  if (o->c >= 'a' && o->c <= 'g') {
    subnote = o->c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    o->c = SGS_File_GETC(o->f);
  }
  if (o->c < 'A' || o->c > 'G') {
    scan_warning(o, "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = o->c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  o->c = SGS_File_GETC(o->f);
  if (o->c == 's')
    semitone = 2;
  else if (o->c == 'f')
    semitone = 0;
  else
    SGS_File_UNGETC(o->f);
  SGS_File_geti(o->f, &octave, false, &read_len);
  if (read_len == 0)
    octave = 4;
  else if (octave >= OCTAVES) {
    scan_warning(o, "invalid octave specified for note - valid range 0-10");
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
static size_t scan_label(SGS_Parser *o, LabelBuf label, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  size_t len = 0;
  nolabel_msg[9] = op; /* replace ? */
  bool truncated = !read_sym(o->f, label, LABEL_LEN, &len);
  if (len == 0) {
    scan_warning(o, nolabel_msg);
  }
  if (truncated) {
    scan_warning(o, "ignoring label name from "LABEL_LEN_A"th character");
    SGS_File_skipstr(o->f, test_symchar);
  }
  o->c = SGS_File_RETC(o->f);
  return len;
}

static int32_t scan_wavetype(SGS_Parser *o) {
  int32_t wave = read_strfind(o->f, SGS_Wave_names);
  if (wave < 0) {
    scan_warning(o, "invalid wave type; available types are:");
    uint8_t i = 0;
    fprintf(stderr, "\t%s", SGS_Wave_names[i]);
    while (++i < SGS_WAVE_TYPES) {
      fprintf(stderr, ", %s", SGS_Wave_names[i]);
    }
    putc('\n', stderr);
  }
  return wave;
}

static bool scan_ramp(SGS_Parser *o, NumSym_f scan_numsym,
                      SGS_Ramp *ramp, bool ratio) {
  bool goal = false;
  int32_t type;
  bool time_set = false;
  ramp->type = SGS_RAMP_LIN; /* default */
  for (;;) {
    uint8_t c = scan_char(o);
    switch (c) {
    case SCAN_NEWLINE:
      ++o->line;
      break;
    case 'c':
      // "state" (type 0) disallowed
      type = read_strfind(o->f, SGS_Ramp_names + 1) + 1;
      if (type <= 0) {
        scan_warning(o, "invalid curve type; available types are:");
        uint8_t i = 1;
        fprintf(stderr, "\t%s", SGS_Ramp_names[i]);
        while (++i < SGS_RAMP_TYPES) {
          fprintf(stderr, ", %s", SGS_Ramp_names[i]);
        }
        putc('\n', stderr);
        break;
      }
      ramp->type = type;
      break;
    case 't': {
      float time;
      if (scan_num(o, 0, &time)) {
        if (time < 0.f) {
          scan_warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        ramp->time_ms = SGS_ui32rint(time * 1000.f);
        time_set = true;
      }
      break; }
    case 'v':
      if (scan_num(o, scan_numsym, &ramp->goal))
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
    ramp->type = SGS_RAMP_STATE;
    return false;
  }
  if (time_set)
    ramp->flags &= ~SGS_RAMP_TIME_DEFAULT;
  else {
    ramp->flags |= SGS_RAMP_TIME_DEFAULT;
    ramp->time_ms = o->sopt.def_time_ms; // initial default
  }
  return true;
}

static bool parse_waittime(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  /* FIXME: ADD_WAIT_DURATION */
  if (SGS_File_TRYC(o->f, 't')) {
    if (!pl->ev_last) {
      scan_warning(o, "add wait for last duration before any parts given");
      return false;
    }
    pl->last_event->ev_flags |= SGS_SDEV_ADD_WAIT_DURATION;
  } else {
    float wait;
    uint32_t wait_ms;
    scan_num(o, 0, &wait);
    if (wait < 0.f) {
      scan_warning(o, "ignoring '\\' with sub-zero time");
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

static void end_operator(ParseLevel *pl) {
  if (!(pl->pl_flags & SDPL_OWN_OP))
    return;
  pl->pl_flags &= ~SDPL_OWN_OP;
  SGS_ScriptOpData *op = pl->operator;
  if (!op->on_prev) { /* initial event should reset its parameters */
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
  if (op->ramp_freq.type != SGS_RAMP_STATE)
    op->op_params |= SGS_POPP_ATTR |
                     SGS_POPP_RAMP_FREQ;
  if (op->ramp_amp.type != SGS_RAMP_STATE)
    op->op_params |= SGS_POPP_ATTR |
                     SGS_POPP_RAMP_AMP;
  if (!(op->op_flags & SGS_SDOP_NESTED)) {
    op->amp *= pl->used_ampmult;
    op->ramp_amp.goal *= pl->used_ampmult;
  }
  pl->operator = NULL;
}

static void end_event(ParseLevel *pl) {
  if (!(pl->pl_flags & SDPL_OWN_EV))
    return;
  pl->pl_flags &= ~SDPL_OWN_EV;
  SGS_Parser *o = pl->o;
  SGS_ScriptEvData *e = pl->event;
  SGS_ScriptEvData *pve;
  end_operator(pl);
  pl->scope_first = pl->ev_last = NULL;
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
    e->vo_params |= SGS_PVOP_ATTR |
                    SGS_PVOP_PAN;
  } else {
    if (e->pan != pve->pan)
      e->vo_params |= SGS_PVOP_PAN;
  }
  if (e->ramp_pan.type != SGS_RAMP_STATE)
    e->vo_params |= SGS_PVOP_ATTR |
                    SGS_PVOP_RAMP_PAN;
  pl->last_event = e;
  pl->event = NULL;
  SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
  if (!o->group_start)
    o->group_start = group_e;
  o->group_end = group_e;
}

static void begin_event(ParseLevel *pl, bool is_compstep) {
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
    e->pan = pve->pan;
    e->ramp_pan = pve->ramp_pan;
  } else { /* set defaults */
    e->pan = 0.5f; /* center */
  }
  if (!is_compstep) {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    pl->main_ev = NULL;
  }
  pl->pl_flags |= SDPL_OWN_EV;
}

static void begin_operator(ParseLevel *pl, bool is_compstep) {
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
    pl->pl_flags |= SDPL_NEW_EVENT_FORK;
  pl->used_ampmult = o->sopt.ampmult;
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
    op->dynfreq = pop->dynfreq;
    op->phase = pop->phase;
    op->amp = pop->amp;
    op->dynamp = pop->dynamp;
    op->ramp_freq = pop->ramp_freq;
    op->ramp_amp = pop->ramp_amp;
    if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
      SGS_ScriptOpData *mpop = pop;
      uint32_t max_time = 0;
      do {
        if (max_time < mpop->time.v_ms) max_time = mpop->time.v_ms;
      } while ((mpop = mpop->next) != NULL);
      op->op_flags |= SGS_SDOP_MULTIPLE;
      op->time.v_ms = max_time;
      pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->time = (SGS_Time){o->sopt.def_time_ms, 0};
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
   * Add new operator to parent(s), ie. either the current event node,
   * or an operator node (either ordinary or representing multiple
   * carriers) in the case of operator linking/nesting.
   */
  if (pop || !pl->nest_list) {
    if (!e->operators.first_on)
      e->operators.first_on = op;
    else
      pl->ev_last->next = op;
    pl->ev_last = op;
    if (!pop) {
      e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
      if (!e->op_graph.first_on)
        e->op_graph.first_on = op;
    }
  } else {
    if (!pl->nest_list->first_on)
      pl->nest_list->first_on = op;
    else
      pl->nest_last->next = op;
    pl->nest_last = op;
  }
  if (!pl->scope_first)
    pl->scope_first = op;
  /*
   * Assign label?
   */
  if (pl->set_label != NULL) {
    SGS_Symtab_set(o->st, pl->set_label, strlen(pl->set_label), op);
    pl->set_label = NULL;
  }
  pl->pl_flags |= SDPL_OWN_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *pl, SGS_ScriptOpData *previous,
                       bool is_compstep) {
  pl->on_prev = previous;
  if (!pl->event ||
      pl->location != SDPL_IN_EVENT /* previous event implicitly ended */ ||
      pl->next_wait_ms ||
      is_compstep)
    begin_event(pl, is_compstep);
  begin_operator(pl, is_compstep);
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
      pl->nest_list = SGS_Mempool_alloc(o->mp, sizeof(SGS_ScriptListData));
      switch (linktype) {
      case SGS_POP_AMOD:
        pl->parent_on->amods = pl->nest_list;
        break;
      case SGS_POP_FMOD:
        pl->parent_on->fmods = pl->nest_list;
        break;
      case SGS_POP_PMOD:
        pl->parent_on->pmods = pl->nest_list;
        break;
      }
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
    if (pl->scope_first != NULL) {
      pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
      begin_node(pl->parent, pl->scope_first, false);
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
    scan_warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  pl->location = SDPL_IN_DEFAULTS;
  for (;;) {
    uint8_t c = scan_char(o);
    switch (c) {
    case 'a':
      if (scan_num(o, 0, &o->sopt.ampmult)) {
        o->sopt.set |= SGS_SOPT_AMPMULT;
      }
      break;
    case 'f':
      if (scan_num(o, scan_note, &o->sopt.def_freq)) {
        o->sopt.set |= SGS_SOPT_DEF_FREQ;
      }
      if (SGS_File_TRYC(o->f, ',') && SGS_File_TRYC(o->f, 'n')) {
        float freq;
        if (scan_num(o, 0, &freq)) {
          if (freq < 1.f) {
            scan_warning(o, "ignoring tuning frequency (Hz) below 1.0");
            break;
          }
          o->sopt.A4_freq = freq;
          o->sopt.set |= SGS_SOPT_A4_FREQ;
        }
      }
      break;
    case 'r':
      if (scan_num(o, 0, &o->sopt.def_ratio)) {
        o->sopt.set |= SGS_SOPT_DEF_RATIO;
      }
      break;
    case 't': {
      float time;
      if (scan_num(o, 0, &time)) {
        if (time < 0.f) {
          scan_warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        o->sopt.def_time_ms = SGS_ui32rint(time * 1000.f);
        o->sopt.set |= SGS_SOPT_DEF_TIME;
      }
      break; }
    default:
    /*UNKNOWN:*/
      o->next_c = c;
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

static bool parse_level(SGS_Parser *o, ParseLevel *parent_pl,
                        uint8_t linktype, uint8_t newscope);

static bool parse_step(ParseLevel *pl) {
  SGS_Parser *o = pl->o;
  pl->location = SDPL_IN_EVENT;
  for (;;) {
    SGS_ScriptEvData *e = pl->event;
    SGS_ScriptOpData *op = pl->operator;
    uint8_t c = scan_char(o);
    switch (c) {
    case 'P':
      if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
        goto UNKNOWN;
      if (scan_num(o, 0, &e->pan)) {
        if (e->ramp_pan.type == SGS_RAMP_STATE)
          e->vo_attr &= ~SGS_PVOA_RAMP_PAN;
      }
      if (SGS_File_TRYC(o->f, '{')) {
        if (scan_ramp(o, 0, &e->ramp_pan, false))
          e->vo_attr |= SGS_PVOA_RAMP_PAN;
      }
      break;
    case '\\':
      if (parse_waittime(pl)) {
        // FIXME: Buggy update node handling for carriers etc. if enabled.
        //begin_node(pl, pl->operator, false);
      }
      break;
    case 'a':
      if (scan_num(o, 0, &op->amp)) {
        op->op_params |= SGS_POPP_AMP;
        if (op->ramp_amp.type == SGS_RAMP_STATE)
          op->attr &= ~SGS_POPA_RAMP_AMP;
      }
      if (SGS_File_TRYC(o->f, '{')) {
        if (scan_ramp(o, 0, &op->ramp_amp, false))
          op->attr |= SGS_POPA_RAMP_AMP;
      }
      if (SGS_File_TRYC(o->f, ',') && SGS_File_TRYC(o->f, 'w')) {
        if (!SGS_File_TESTC(o->f, '[')) {
          scan_num(o, 0, &op->dynamp);
        }
        if (SGS_File_TRYC(o->f, '[')) {
          parse_level(o, pl, SGS_POP_AMOD, SCOPE_NEST);
        }
      }
      break;
    case 'f':
      if (scan_num(o, scan_note, &op->freq)) {
        op->attr &= ~SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (op->ramp_freq.type == SGS_RAMP_STATE)
          op->attr &= ~(SGS_POPA_RAMP_FREQ |
                        SGS_POPA_RAMP_FREQRATIO);
      }
      if (SGS_File_TRYC(o->f, '{')) {
        if (scan_ramp(o, scan_note, &op->ramp_freq, false)) {
          op->attr |= SGS_POPA_RAMP_FREQ;
          op->attr &= ~SGS_POPA_RAMP_FREQRATIO;
        }
      }
      if (SGS_File_TRYC(o->f, ',') && SGS_File_TRYC(o->f, 'w')) {
        if (!SGS_File_TESTC(o->f, '[')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_POPA_DYNFREQRATIO;
          }
        }
        if (SGS_File_TRYC(o->f, '[')) {
          parse_level(o, pl, SGS_POP_FMOD, SCOPE_NEST);
        }
      }
      break;
    case 'p': {
      float phase;
      if (scan_num(o, 0, &phase)) {
        op->phase = SGS_cyclepos_dtoui32(phase);
        op->op_params |= SGS_POPP_PHASE;
      }
      if (SGS_File_TRYC(o->f, '[')) {
        parse_level(o, pl, SGS_POP_PMOD, SCOPE_NEST);
      }
      break; }
    case 'r':
      if (!(op->op_flags & SGS_SDOP_NESTED))
        goto UNKNOWN;
      if (scan_num(o, 0, &op->freq)) {
        op->attr |= SGS_POPA_FREQRATIO;
        op->op_params |= SGS_POPP_FREQ;
        if (op->ramp_freq.type == SGS_RAMP_STATE)
          op->attr &= ~(SGS_POPA_RAMP_FREQ |
                        SGS_POPA_RAMP_FREQRATIO);
      }
      if (SGS_File_TRYC(o->f, '{')) {
        if (scan_ramp(o, scan_note, &op->ramp_freq, true)) {
          op->attr |= SGS_POPA_RAMP_FREQ |
                      SGS_POPA_RAMP_FREQRATIO;
        }
      }
      if (SGS_File_TRYC(o->f, ',') && SGS_File_TRYC(o->f, 'w')) {
        if (!SGS_File_TESTC(o->f, '[')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->attr |= SGS_POPA_DYNFREQRATIO;
          }
        }
        if (SGS_File_TRYC(o->f, '[')) {
          parse_level(o, pl, SGS_POP_FMOD, SCOPE_NEST);
        }
      }
      break;
    case 's': {
      float silence;
      if (!scan_num(o, 0, &silence)) break;
      if (silence < 0.f) {
        scan_warning(o, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = SGS_ui32rint(silence * 1000.f);
      break; }
    case 't':
      if (SGS_File_TRYC(o->f, 'd')) {
        op->time = (SGS_Time){o->sopt.def_time_ms, 0};
      } else if (SGS_File_TRYC(o->f, 'i')) {
        if (!(op->op_flags & SGS_SDOP_NESTED)) {
          scan_warning(o, "ignoring 'ti' (implicit time) for non-nested operator");
          break;
        }
        op->time = (SGS_Time){o->sopt.def_time_ms,
          SGS_TIMEP_SET | SGS_TIMEP_IMPLICIT};
      } else {
        float time;
        if (!scan_num(o, 0, &time)) break;
        if (time < 0.f) {
          scan_warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->time = (SGS_Time){SGS_ui32rint(time * 1000.f),
          SGS_TIMEP_SET};
      }
      op->op_params |= SGS_POPP_TIME;
      break;
    case 'w': {
      int32_t wave = scan_wavetype(o);
      if (wave < 0)
        break;
      op->wave = wave;
      break; }
    default:
    UNKNOWN:
      o->next_c = c;
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
                        uint8_t linktype, uint8_t newscope) {
  LabelBuf label;
  ParseLevel pl;
  size_t label_len;
  uint8_t flags = 0;
  bool endscope = false;
  begin_scope(o, &pl, parent_pl, linktype, newscope);
  ++o->call_level;
  for (;;) {
    uint8_t c = scan_char(o);
    flags &= ~HANDLE_DEFER;
    switch (c) {
    case SCAN_NEWLINE:
      ++o->line;
      if (pl.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->call_level > 1)
          goto RETURN;
        flags = 0;
        pl.location = SDPL_IN_NONE;
      }
      break;
    case '\'':
      /*
       * Label assignment (set to what follows).
       */
      if (pl.set_label != NULL) {
        scan_warning(o, "ignoring label assignment to label assignment");
        break;
      }
      label_len = scan_label(o, label, c);
      pl.set_label = SGS_Symtab_pool_str(o->st, label, label_len);
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        o->next_c = c;
        goto RETURN;
      }
      if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
        goto INVALID;
      if ((pl.operator->time.flags & (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
          == (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
        scan_warning(o, "ignoring 'ti' (implicit time) before ';' separator");
      begin_node(&pl, pl.operator, true);
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '@':
      if (SGS_File_TRYC(o->f, '[')) {
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
        scan_warning(o, "ignoring label assignment to label reference");
        pl.set_label = NULL;
      }
      pl.location = SDPL_IN_NONE;
      label_len = scan_label(o, label, c);
      if (label_len > 0) {
        SGS_ScriptOpData *ref = SGS_Symtab_get(o->st, label, label_len);
        if (!ref)
          scan_warning(o, "ignoring reference to undefined label");
        else {
          begin_node(&pl, ref, false);
          SGS_Symtab_set(o->st, label, label_len, pl.operator); /* update */
          flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case 'O': {
      int32_t wave = scan_wavetype(o);
      if (wave < 0)
        break;
      begin_node(&pl, 0, false);
      pl.operator->wave = wave;
      flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      flags = parse_settings(&pl) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '[':
      scan_warning(o, "opening '[' out of place");
      break;
    case '\\':
      if (pl.location == SDPL_IN_DEFAULTS ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
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
      scan_warning(o, "closing ']' without opening '['");
      break;
    case '{':
      scan_warning(o, "opening '{' out of place");
      break;
    case '|':
      if (pl.location == SDPL_IN_DEFAULTS ||
          ((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        o->next_c = c;
        goto RETURN;
      }
      end_event(&pl);
      if (!o->group_start) {
        scan_warning(o, "no sounds precede time separator");
        break;
      }
      flush_durgroup(o);
      pl.location = SDPL_IN_NONE;
      break;
    case '}':
      scan_warning(o, "closing '}' without opening '{'");
      break;
    default:
    INVALID:
      if (!handle_unknown_or_end(o)) goto FINISH;
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
  }
FINISH:
  if (newscope == SCOPE_NEST || newscope == SCOPE_BIND)
    scan_warning(o, "end of file without closing ']'s");
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
static bool parse_file(SGS_Parser *o, SGS_File *f) {
  o->f = f;
  o->line = 1;
  parse_level(o, 0, SGS_POP_CARR, SCOPE_TOP);
  SGS_File_close(o->f);
  o->f = NULL; // freed by invoker
  return true;
}

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SGS_ScriptEvData *e_last) {
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

static void time_operator(SGS_ScriptOpData *op) {
  SGS_ScriptEvData *e = op->event;
  if (!(op->op_params & SGS_POPP_TIME))
    e->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
  if (!(op->time.flags & SGS_TIMEP_SET) &&
      (op->op_flags & SGS_SDOP_NESTED) != 0) {
    op->time.flags |= SGS_TIMEP_IMPLICIT;
    op->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
  }
  if (!(op->time.flags & SGS_TIMEP_IMPLICIT)) {
    if (op->ramp_freq.flags & SGS_RAMP_TIME_DEFAULT)
      op->ramp_freq.time_ms = op->time.v_ms;
    if (op->ramp_amp.flags & SGS_RAMP_TIME_DEFAULT)
      op->ramp_amp.time_ms = op->time.v_ms;
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

static void time_event(SGS_ScriptEvData *e) {
  /*
   * Adjust default ramp durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  // e->ramp_pan.flags &= ~SGS_RAMP_TIME_DEFAULT; // TODO: revisit semantics
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
SGS_Script* SGS_load_Script(SGS_File *f) {
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
  o->events = pr.events;
  o->name = name;
  o->sopt = pr.sopt;

DONE:
  fini_parser(&pr);
  return o;
}

/**
 * Destroy instance.
 */
void SGS_discard_Script(SGS_Script *o) {
  if (!o)
    return;
  SGS_destroy_Mempool(o->mp);
}
