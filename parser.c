/* sgensys: Script parser module.
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

#include "streamf.h"
#include "program.h"
#include "parser.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * General-purpose functions
 */

#define IS_WHITESPACE(c) \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

#define IS_SYMCHAR(c) \
  (((c) >= 'a' && (c) <= 'z') || \
   ((c) >= 'A' && (c) <= 'Z') || \
   ((c) >= '0' && (c) <= '9') || \
   ((c) == '_'))

#define SYMKEY_LEN 80
#define SYMKEY_LEN_A "80"
typedef char SymBuf_t[SYMKEY_LEN];
static bool read_sym(SGS_CBuf *fbuf, SymBuf_t sym, uint32_t *sym_len) {
  uint32_t i = 0;
  bool error = false;
  for (;;) {
    uint8_t c = SGS_CBuf_GETC(fbuf);
    if (!IS_SYMCHAR(c)) {
      SGS_CBuf_UNGETC(fbuf);
      break;
    } else if (i == SYMKEY_LEN) {
      error = true;
    }
    sym[i++] = c;
  }
  sym[i] = '\0';
  *sym_len = i;
  return !error;
}

static int32_t read_inum(SGS_CBuf *fbuf) {
  char c;
  int32_t num = -1;
  c = SGS_CBuf_GETC(fbuf);
  if (c >= '0' && c <= '9') {
    num = c - '0';
    for (;;) {
      c = SGS_CBuf_GETC(fbuf);
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  SGS_CBuf_UNGETC(fbuf);
  return num;
}

/*
 * Read string if it matches an entry in a NULL-pointer terminated array.
 */
static int32_t read_astrfind(SGS_CBuf *fbuf, const char *const*astr) {
  int32_t ret;
  uint32_t i, len, pos, matchpos;
  char c;
  uint32_t strc;
  const char **s;
  for (len = 0, strc = 0; astr[strc]; ++strc)
    if ((i = strlen(astr[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = astr[i];
  ret = -1;
  pos = matchpos = 0;
  while ((c = SGS_CBuf_GETC(fbuf)) != 0) {
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
  SGS_CBuf_UNGETN(fbuf, (pos-matchpos));
  return ret;
}

static void read_skipws(SGS_CBuf *fbuf) {
  char c;
  while ((c = SGS_CBuf_GETC(fbuf)) == ' ' || c == '\t') ;
  SGS_CBuf_UNGETC(fbuf);
}

/*
 * Parser
 */

struct SGS_Parser {
	SGS_Stream fstream;
	uint32_t line;
	uint32_t calllevel;
	uint32_t scopeid;
	char c, nextc;
	/* script data */
	SGS_SymTab *st;
	SGS_ParseEventData *events;
	SGS_ParseEventData *last_event;
	SGS_ParseScriptOptions sopt;
	/* all results */
	SGS_PList results;
};

enum {
  /* parsing scopes */
  SCOPE_SAME = 0,
  SCOPE_TOP = 1,
  SCOPE_BIND = '{',
  SCOPE_NEST = '<'
};

enum {
  PSSD_IN_DEFAULTS = 1<<0, /* adjusting default values */
  PSSD_IN_NODE = 1<<1,     /* adjusting operator and/or voice */
  PSSD_NESTED_SCOPE = 1<<2,
  PSSD_BIND_MULTIPLE = 1<<3, /* previous node interpreted as set of nodes */
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
typedef struct ParseScopeData {
  SGS_Parser *o;
  struct ParseScopeData *parent;
  uint32_t ps_flags;
  char scope;
  SGS_ParseEventData *event, *last_event;
  SGS_ParseOperatorData *operator, *first_operator, *last_operator;
  SGS_ParseOperatorData *parent_on, *on_prev;
  uint8_t linktype;
  uint8_t last_linktype; /* FIXME: kludge */
  const char *set_label; /* label assigned to next node */
  /* timing/delay */
  SGS_ParseEventData *group_from; /* where to begin for group_events() */
  SGS_ParseEventData *composite; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
} ParseScopeData;

#define NEWLINE '\n'
static char scan_char(SGS_Parser *o) {
  SGS_CBuf *fbuf = &o->fstream.buf;
  char c;
  read_skipws(fbuf);
  if (o->nextc) {
    c = o->nextc;
    o->nextc = 0;
  } else {
    c = SGS_CBuf_GETC(fbuf);
  }
  if (c == '#')
    while ((c = SGS_CBuf_GETC(fbuf)) != '\n' && c != '\r' && c != 0) ;
  if (c == '\n') {
    SGS_CBuf_TRYC(fbuf, '\r');
    c = NEWLINE;
  } else if (c == '\r') {
    c = NEWLINE;
  } else {
    read_skipws(fbuf);
  }
  o->c = c;
  return c;
}

static void scan_ws(SGS_Parser *o) {
  SGS_CBuf *fbuf = &o->fstream.buf;
  char c;
  do {
    c = SGS_CBuf_GETC(fbuf);
    if (c == ' ' || c == '\t')
      continue;
    if (c == '\n') {
      ++o->line;
      SGS_CBuf_TRYC(fbuf, '\r');
    } else if (c == '\r') {
      ++o->line;
    } else if (c == '#') {
      while ((c = SGS_CBuf_GETC(fbuf)) != '\n' && c != '\r' && c != 0) ;
    } else {
      SGS_CBuf_UNGETC(fbuf);
      break;
    }
  } while (c != 0);
}

static float scan_num_r(SGS_Parser *o, float (*scan_symbol)(SGS_Parser *o),
		char *buf, uint32_t len, uint8_t pri, uint32_t level) {
  SGS_CBuf *fbuf = &o->fstream.buf;
  char *p = buf;
  bool dot = false;
  float num;
  char c;
  c = SGS_CBuf_GETC(fbuf);
  if (level) scan_ws(o);
  if (c == '(') {
    return scan_num_r(o, scan_symbol, buf, len, 255, level+1);
  }
  if (scan_symbol &&
      ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    SGS_CBuf_UNGETC(fbuf);
    num = scan_symbol(o);
    if (num == num) /* not NAN; was recognized */
      goto LOOP;
  }
  if (c == '-') {
    *p++ = c;
    c = SGS_CBuf_GETC(fbuf);
    if (level) scan_ws(o);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = SGS_CBuf_GETC(fbuf);
  }
  SGS_CBuf_UNGETC(fbuf);
  if (p == buf) return NAN;
  *p = '\0';
  num = strtod(buf, 0);
LOOP:
  if (level) scan_ws(o);
  for (;;) {
    c = SGS_CBuf_GETC(fbuf);
    if (level) scan_ws(o);
    switch (c) {
    case '(':
      num *= scan_num_r(o, scan_symbol, buf, len, 255, level+1);
      break;
    case ')':
      if (pri < 255)
        SGS_CBuf_UNGETC(fbuf);
      return num;
      break;
    case '^':
      num = exp(log(num) * scan_num_r(o, scan_symbol, buf, len, 0, level));
      break;
    case '*':
      num *= scan_num_r(o, scan_symbol, buf, len, 1, level);
      break;
    case '/':
      num /= scan_num_r(o, scan_symbol, buf, len, 1, level);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += scan_num_r(o, scan_symbol, buf, len, 2, level);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= scan_num_r(o, scan_symbol, buf, len, 2, level);
      break;
    default:
      SGS_CBuf_UNGETC(fbuf);
      return num;
    }
    if (num != num) {
      SGS_CBuf_UNGETC(fbuf);
      return num;
    }
  }
}
static bool scan_num(SGS_Parser *o, float (*scan_symbol)(SGS_Parser *o),
		float *var) {
  char buf[64];
  float num = scan_num_r(o, scan_symbol, buf, 64, 254, 0);
  if (num != num)
    return false;
  *var = num;
  return true;
}

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void warning(SGS_Parser *o, const char *str) {
  char buf[4] = {'\'', o->c, '\'', 0};
  fprintf(stderr, "warning: %s [line %d, at %s] - %s\n", o->fstream.name, o->line,
         (o->c == 0 ? "EOF" : buf), str);
}
#define WARN_INVALID "invalid character"

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
  SGS_CBuf *fbuf = &o->fstream.buf;
  float freq;
  o->c = SGS_CBuf_GETC(fbuf);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  if (o->c >= 'a' && o->c <= 'g') {
    subnote = o->c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    o->c = SGS_CBuf_GETC(fbuf);
  }
  if (o->c < 'A' || o->c > 'G') {
    warning(o, "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = o->c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  o->c = SGS_CBuf_GETC(fbuf);
  if (o->c == 's')
    semitone = 2;
  else if (o->c == 'f')
    semitone = 0;
  else
    SGS_CBuf_UNGETC(fbuf);
  octave = read_inum(fbuf);
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

static uint32_t scan_label(SGS_Parser *o, SymBuf_t sym, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  SGS_CBuf *fbuf = &o->fstream.buf;
  uint32_t len = 0;
  bool error;
  nolabel_msg[9] = op; /* replace ? */
  error = !read_sym(fbuf, sym, &len);
  o->c = SGS_CBuf_RETC(fbuf);
  if (len == 0) {
    warning(o, nolabel_msg);
  }
  if (error) {
    warning(o, "ignoring label name from "SYMKEY_LEN_A"th character");
  }
  return len;
}

static int32_t scan_wavetype(SGS_Parser *o) {
  SGS_CBuf *fbuf = &o->fstream.buf;
  int32_t wave = read_astrfind(fbuf, SGS_Wave_names);
  if (wave < 0) {
    warning(o, "invalid wave type; available types are:");
    SGS_wave_t i = 0;
    fprintf(stderr, "\t%s", SGS_Wave_names[i]);
    while (++i < SGS_WAVE_TYPES) {
      fprintf(stderr, ", %s", SGS_Wave_names[i]);
    }
    putc('\n', stderr);
  }
  return wave;
}

static bool scan_valit(SGS_Parser *o, float (*scan_symbol)(SGS_Parser *o),
		SGS_ProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  SGS_CBuf *fbuf = &o->fstream.buf;
  char c;
  bool goal = false;
  int32_t type;
  vi->time_ms = SGS_TIME_DEFAULT;
  vi->type = SGS_VALIT_LIN; /* default */
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case NEWLINE:
      ++o->line;
      break;
    case 'c':
      type = read_astrfind(fbuf, valittypes);
      if (type >= 0) {
        vi->type = type + SGS_VALIT_LIN;
        break;
      }
      goto INVALID;
    case 't': {
      float time;
      if (scan_num(o, 0, &time)) {
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        vi->time_ms = lrint(time * 1000.f);
      }
      break; }
    case 'v':
      if (scan_num(o, scan_symbol, &vi->goal))
        goal = true;
      break;
    case ']':
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID);
      break;
    }
  }
  warning(o, "end of file without closing ']'");
RETURN:
  if (!goal) {
    warning(o, "ignoring gradual parameter change with no target value");
    vi->type = SGS_VALIT_NONE;
    return false;
  }
  return true;
}

static bool parse_waittime(ParseScopeData *ps) {
  SGS_Parser *o = ps->o;
  SGS_CBuf *fbuf = &o->fstream.buf;
  /* FIXME: ADD_WAIT_DURATION */
  if (SGS_CBuf_TRYC(fbuf, 't')) {
    if (!ps->last_operator) {
      warning(o, "add wait for last duration before any parts given");
      return false;
    }
    ps->last_event->ed_flags |= SGS_PSED_ADD_WAIT_DURATION;
  } else {
    float wait;
    uint32_t wait_ms;
    scan_num(o, 0, &wait);
    if (wait < 0.f) {
      warning(o, "ignoring '\\' with sub-zero time");
      return false;
    }
    wait_ms = lrint(wait * 1000.f);
    ps->next_wait_ms += wait_ms;
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
static void destroy_operator(SGS_ParseOperatorData *op) {
	SGS_PList_clear(&op->on_next);
	size_t i;
	SGS_ParseOperatorData **ops;
	ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PList_clear(&op->fmods);
	ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PList_clear(&op->pmods);
	ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event(SGS_ParseEventData *e) {
	size_t i;
	SGS_ParseOperatorData **ops;
	ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PList_clear(&e->operators);
	SGS_PList_clear(&e->graph);
	free(e);
}

static void end_operator(ParseScopeData *ps) {
  SGS_Parser *o = ps->o;
  SGS_ParseOperatorData *op = ps->operator;
  if (!op)
    return; /* nothing to do */
  if (!op->on_prev) { /* initial event should reset its parameters */
    op->operator_params |= SGS_OPP_ADJCS |
                           SGS_OPP_WAVE |
                           SGS_OPP_TIME |
                           SGS_OPP_SILENCE |
                           SGS_OPP_FREQ |
                           SGS_OPP_DYNFREQ |
                           SGS_OPP_PHASE |
                           SGS_OPP_AMP |
                           SGS_OPP_DYNAMP |
                           SGS_OPP_ATTR;
  } else {
    SGS_ParseOperatorData *pop = op->on_prev;
    if (op->attr != pop->attr)
      op->operator_params |= SGS_OPP_ATTR;
    if (op->wave != pop->wave)
      op->operator_params |= SGS_OPP_WAVE;
    /* SGS_TIME set when time set */
    if (op->silence_ms)
      op->operator_params |= SGS_OPP_SILENCE;
    /* SGS_FREQ set when freq set */
    if (op->dynfreq != pop->dynfreq)
      op->operator_params |= SGS_OPP_DYNFREQ;
    /* SGS_PHASE set when phase set */
    /* SGS_AMP set when amp set */
    if (op->dynamp != pop->dynamp)
      op->operator_params |= SGS_OPP_DYNAMP;
  }
  if (op->valitfreq.type)
    op->operator_params |= SGS_OPP_ATTR |
                           SGS_OPP_VALITFREQ;
  if (op->valitamp.type)
    op->operator_params |= SGS_OPP_ATTR |
                           SGS_OPP_VALITAMP;
  if (!(ps->ps_flags & PSSD_NESTED_SCOPE))
    op->amp *= o->sopt.ampmult;
  ps->operator = NULL;
  ps->last_operator = op;
}

static void end_event(ParseScopeData *ps) {
  SGS_ParseEventData *e = ps->event;
  SGS_ParseEventData *pve;
  if (!e)
    return; /* nothing to do */
  end_operator(ps);
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->voice_params |= SGS_VOP_ATTR |
                       SGS_VOP_GRAPH |
                       SGS_VOP_PANNING;
  } else {
    if (e->panning != pve->panning)
      e->voice_params |= SGS_VOP_PANNING;
  }
  if (e->valitpanning.type)
    e->voice_params |= SGS_VOP_ATTR |
                       SGS_VOP_VALITPANNING;
  ps->last_event = e;
  ps->event = NULL;
}

static void begin_event(ParseScopeData *ps, uint8_t linktype, bool composite) {
  SGS_Parser *o = ps->o;
  SGS_ParseEventData *e, *pve;
  end_event(ps);
  ps->event = calloc(1, sizeof(SGS_ParseEventData));
  e = ps->event;
  e->wait_ms = ps->next_wait_ms;
  ps->next_wait_ms = 0;
  if (ps->on_prev) {
    pve = ps->on_prev->event;
    pve->ed_flags |= SGS_PSED_VOICE_LATER_USED;
    if (pve->composite && !composite) {
      SGS_ParseEventData *last_ce;
      for (last_ce = pve->composite; last_ce->next; last_ce = last_ce->next) ;
      last_ce->ed_flags |= SGS_PSED_VOICE_LATER_USED;
    }
    e->voice_prev = pve;
    e->voice_attr = pve->voice_attr;
    e->panning = pve->panning;
    e->valitpanning = pve->valitpanning;
  } else { /* set defaults */
    e->panning = 0.5f; /* center */
  }
  if (!ps->group_from)
    ps->group_from = e;
  if (composite) {
    if (!ps->composite) {
      pve->composite = e;
      ps->composite = pve;
    } else {
      pve->next = e;
    }
  } else {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    ps->composite = 0;
  }
}

static void begin_operator(ParseScopeData *ps, uint8_t linktype, bool composite) {
  SGS_Parser *o = ps->o;
  SGS_ParseEventData *e = ps->event;
  SGS_ParseOperatorData *op, *pop = ps->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(ps);
  ps->operator = calloc(1, sizeof(SGS_ParseOperatorData));
  op = ps->operator;
  if (!ps->first_operator)
    ps->first_operator = op;
  if (!composite && ps->last_operator)
    ps->last_operator->next_bound = op;
  /*
   * Initialize node.
   */
  if (pop) {
    pop->od_flags |= SGS_PSOD_OPERATOR_LATER_USED;
    op->on_prev = pop;
    op->od_flags = pop->od_flags & (SGS_PSOD_OPERATOR_NESTED |
                                    SGS_PSOD_MULTIPLE_OPERATORS);
    if (composite)
      op->od_flags |= SGS_PSOD_TIME_DEFAULT; /* default: previous or infinite time */
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
    SGS_PList_copy(&op->fmods, &pop->fmods);
    SGS_PList_copy(&op->pmods, &pop->pmods);
    SGS_PList_copy(&op->amods, &pop->amods);
    if (ps->ps_flags & PSSD_BIND_MULTIPLE) {
      SGS_ParseOperatorData *mpop = pop;
      uint32_t max_time = 0;
      do { 
        if (max_time < mpop->time_ms) max_time = mpop->time_ms;
        SGS_PList_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound));
      op->od_flags |= SGS_PSOD_MULTIPLE_OPERATORS;
      op->time_ms = max_time;
      ps->ps_flags &= ~PSSD_BIND_MULTIPLE;
    } else {
      SGS_PList_add(&pop->on_next, op);
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->od_flags = SGS_PSOD_TIME_DEFAULT; /* default: depends on context */
    op->time_ms = o->sopt.def_time_ms;
    op->amp = 1.0f;
    if (!(ps->ps_flags & PSSD_NESTED_SCOPE)) {
      op->freq = o->sopt.def_freq;
    } else {
      op->od_flags |= SGS_PSOD_OPERATOR_NESTED;
      op->freq = o->sopt.def_ratio;
      op->attr |= SGS_OPAT_FREQRATIO;
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
    SGS_PList_add(&e->operators, op);
    if (linktype == NL_GRAPH) {
      e->voice_params |= SGS_VOP_GRAPH;
      SGS_PList_add(&e->graph, op);
    }
  } else {
    SGS_PList *list = NULL;
    switch (linktype) {
    case NL_FMODS:
      list = &ps->parent_on->fmods;
      break;
    case NL_PMODS:
      list = &ps->parent_on->pmods;
      break;
    case NL_AMODS:
      list = &ps->parent_on->amods;
      break;
    }
    ps->parent_on->operator_params |= SGS_OPP_ADJCS;
    SGS_PList_add(list, op);
  }
  /*
   * Assign label. If no new label but previous node (for a non-composite)
   * has one, update label to point to new node, but keep pointer in
   * previous node.
   */
  if (ps->set_label) {
    SGS_SymTab_set(o->st, ps->set_label, strlen(ps->set_label), op);
    op->label = ps->set_label;
    ps->set_label = NULL;
  } else if (!composite && pop && pop->label) {
    SGS_SymTab_set(o->st, pop->label, strlen(pop->label), op);
    op->label = pop->label;
  }
}

/*
 * Default values for new nodes are being set.
 */
#define in_defaults(ps) ((ps)->ps_flags & PSSD_IN_DEFAULTS)
#define enter_defaults(ps) ((void)((ps)->ps_flags |= PSSD_IN_DEFAULTS))
#define leave_defaults(ps) ((void)((ps)->ps_flags &= ~PSSD_IN_DEFAULTS))

/*
 * Values for current node are being set.
 */
#define in_current_node(ps) ((ps)->ps_flags & PSSD_IN_NODE)
#define enter_current_node(ps) ((void)((ps)->ps_flags |= PSSD_IN_NODE))
#define leave_current_node(ps) ((void)((ps)->ps_flags &= ~PSSD_IN_NODE))

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseScopeData *ps, SGS_ParseOperatorData *previous,
                       uint8_t linktype, bool composite) {
  ps->on_prev = previous;
  if (!ps->event ||
      !in_current_node(ps) /* previous event implicitly ended */ ||
      ps->next_wait_ms ||
      composite)
    begin_event(ps, linktype, composite);
  begin_operator(ps, linktype, composite);
  ps->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SGS_Parser *o, ParseScopeData *ps,
                        ParseScopeData *parent, uint8_t linktype,
                        char newscope) {
  memset(ps, 0, sizeof(ParseScopeData));
  ps->o = o;
  ps->scope = newscope;
  if (parent) {
    ps->parent = parent;
    ps->ps_flags = parent->ps_flags;
    if (newscope == SCOPE_SAME)
      ps->scope = parent->scope;
    ps->event = parent->event;
    ps->operator = parent->operator;
    ps->parent_on = parent->parent_on;
    if (newscope == SCOPE_BIND)
      ps->group_from = parent->group_from;
    if (newscope == SCOPE_NEST) {
      ps->ps_flags |= PSSD_NESTED_SCOPE;
      ps->parent_on = parent->operator;
    }
  }
  ps->linktype = linktype;
}

static void end_scope(ParseScopeData *ps) {
  SGS_Parser *o = ps->o;
  end_operator(ps);
  if (ps->scope == SCOPE_BIND) {
    if (!ps->parent->group_from)
      ps->parent->group_from = ps->group_from;
    /*
     * Begin multiple-operator node in parent scope for the operator nodes in
     * this scope, provided any are present.
     */
    if (ps->first_operator) {
      ps->parent->ps_flags |= PSSD_BIND_MULTIPLE;
      begin_node(ps->parent, ps->first_operator, ps->parent->last_linktype, false);
    }
  } else if (!ps->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    SGS_ParseEventData *group_to;
    end_event(ps);
    group_to = (ps->composite) ?  ps->composite : ps->last_event;
    if (group_to)
      group_to->groupfrom = ps->group_from;
  }
  if (ps->set_label) {
    warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseScopeData *ps) {
  SGS_Parser *o = ps->o;
  char c;
  enter_defaults(ps);
  leave_current_node(ps);
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case 'a':
      if (scan_num(o, 0, &o->sopt.ampmult)) {
        o->sopt.changed |= SGS_PSSO_AMPMULT;
      }
      break;
    case 'f':
      if (scan_num(o, scan_note, &o->sopt.def_freq)) {
        o->sopt.changed |= SGS_PSSO_DEF_FREQ;
      }
      break;
    case 'n': {
      float freq;
      if (scan_num(o, 0, &freq)) {
        if (freq < 1.f) {
          warning(o, "ignoring tuning frequency (Hz) below 1.0");
          break;
        }
        o->sopt.A4_freq = freq;
        o->sopt.changed |= SGS_PSSO_A4_FREQ;
      }
      break; }
    case 'r':
      if (scan_num(o, 0, &o->sopt.def_ratio)) {
        o->sopt.def_ratio = 1.f / o->sopt.def_ratio;
        o->sopt.changed |= SGS_PSSO_DEF_RATIO;
      }
      break;
    case 't': {
      float time;
      if (scan_num(o, 0, &time)) {
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        o->sopt.def_time_ms = lrint(time * 1000.f);
        o->sopt.changed |= SGS_PSSO_DEF_TIME;
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

static bool parse_level(SGS_Parser *o, ParseScopeData *parentps,
                        uint8_t linktype, char newscope);

static bool parse_step(ParseScopeData *ps) {
  SGS_Parser *o = ps->o;
  SGS_CBuf *fbuf = &o->fstream.buf;
  SGS_ParseEventData *e = ps->event;
  SGS_ParseOperatorData *op = ps->operator;
  char c;
  leave_defaults(ps);
  enter_current_node(ps);
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case 'P':
      if (ps->ps_flags & PSSD_NESTED_SCOPE)
        goto UNKNOWN;
      if (SGS_CBuf_TRYC(fbuf, '[')) {
        if (scan_valit(o, 0, &e->valitpanning))
          e->voice_attr |= SGS_VOAT_VALITPANNING;
      } else if (scan_num(o, 0, &e->panning)) {
        if (!e->valitpanning.type)
          e->voice_attr &= ~SGS_VOAT_VALITPANNING;
      }
      break;
    case '\\':
      if (parse_waittime(ps)) {
        begin_node(ps, ps->operator, NL_REFER, false);
      }
      break;
    case 'a':
      if (ps->linktype == NL_AMODS ||
          ps->linktype == NL_FMODS)
        goto UNKNOWN;
      if (SGS_CBuf_TRYC(fbuf, '!')) {
        if (!SGS_CBuf_TESTC(fbuf, '<')) {
          scan_num(o, 0, &op->dynamp);
        }
        if (SGS_CBuf_TRYC(fbuf, '<')) {
          if (op->amods.count) {
            op->operator_params |= SGS_OPP_ADJCS;
            SGS_PList_clear(&op->amods);
          }
          parse_level(o, ps, NL_AMODS, SCOPE_NEST);
        }
      } else if (SGS_CBuf_TRYC(fbuf, '[')) {
        if (scan_valit(o, 0, &op->valitamp))
          op->attr |= SGS_OPAT_VALITAMP;
      } else {
        scan_num(o, 0, &op->amp);
        op->operator_params |= SGS_OPP_AMP;
        if (!op->valitamp.type)
          op->attr &= ~SGS_OPAT_VALITAMP;
      }
      break;
    case 'f':
      if (SGS_CBuf_TRYC(fbuf, '!')) {
        if (!SGS_CBuf_TESTC(fbuf, '<')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_OPAT_DYNFREQRATIO;
          }
        }
        if (SGS_CBuf_TRYC(fbuf, '<')) {
          if (op->fmods.count) {
            op->operator_params |= SGS_OPP_ADJCS;
            SGS_PList_clear(&op->fmods);
          }
          parse_level(o, ps, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_CBuf_TRYC(fbuf, '[')) {
        if (scan_valit(o, scan_note, &op->valitfreq)) {
          op->attr |= SGS_OPAT_VALITFREQ;
          op->attr &= ~SGS_OPAT_VALITFREQRATIO;
        }
      } else if (scan_num(o, scan_note, &op->freq)) {
        op->attr &= ~SGS_OPAT_FREQRATIO;
        op->operator_params |= SGS_OPP_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_OPAT_VALITFREQ |
                        SGS_OPAT_VALITFREQRATIO);
      }
      break;
    case 'p':
      if (SGS_CBuf_TRYC(fbuf, '!')) {
        if (SGS_CBuf_TRYC(fbuf, '<')) {
          if (op->pmods.count) {
            op->operator_params |= SGS_OPP_ADJCS;
            SGS_PList_clear(&op->pmods);
          }
          parse_level(o, ps, NL_PMODS, SCOPE_NEST);
        } else
          goto UNKNOWN;
      } else if (scan_num(o, 0, &op->phase)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->operator_params |= SGS_OPP_PHASE;
      }
      break;
    case 'r':
      if (!(ps->ps_flags & PSSD_NESTED_SCOPE))
        goto UNKNOWN;
      if (SGS_CBuf_TRYC(fbuf, '!')) {
        if (!SGS_CBuf_TESTC(fbuf, '<')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->dynfreq = 1.f / op->dynfreq;
            op->attr |= SGS_OPAT_DYNFREQRATIO;
          }
        }
        if (SGS_CBuf_TRYC(fbuf, '<')) {
          if (op->fmods.count) {
            op->operator_params |= SGS_OPP_ADJCS;
            SGS_PList_clear(&op->fmods);
          }
          parse_level(o, ps, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_CBuf_TRYC(fbuf, '[')) {
        if (scan_valit(o, scan_note, &op->valitfreq)) {
          op->valitfreq.goal = 1.f / op->valitfreq.goal;
          op->attr |= SGS_OPAT_VALITFREQ |
                      SGS_OPAT_VALITFREQRATIO;
        }
      } else if (scan_num(o, 0, &op->freq)) {
        op->freq = 1.f / op->freq;
        op->attr |= SGS_OPAT_FREQRATIO;
        op->operator_params |= SGS_OPP_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_OPAT_VALITFREQ |
                        SGS_OPAT_VALITFREQRATIO);
      }
      break;
    case 's': {
      float silence;
      scan_num(o, 0, &silence);
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = lrint(silence * 1000.f);
      break; }
    case 't':
      if (SGS_CBuf_TRYC(fbuf, '*')) {
        op->od_flags |= SGS_PSOD_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = o->sopt.def_time_ms;
      } else if (SGS_CBuf_TRYC(fbuf, 'i')) {
        if (!(ps->ps_flags & PSSD_NESTED_SCOPE)) {
          warning(o, "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        scan_num(o, 0, &time);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        op->time_ms = lrint(time * 1000.f);
      }
      op->operator_params |= SGS_OPP_TIME;
      break;
    case 'w': {
      int32_t wave = scan_wavetype(o);
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
  HANDLE_DEFER = 1<<1,
  DEFERRED_STEP = 1<<2,
  DEFERRED_SETTINGS = 1<<4
};
static bool parse_level(SGS_Parser *o, ParseScopeData *parentps,
                        uint8_t linktype, char newscope) {
  SymBuf_t label;
  ParseScopeData ps;
  uint32_t label_len;
  char c;
  uint8_t flags = 0;
  bool endscope = false;
  begin_scope(o, &ps, parentps, linktype, newscope);
  ++o->calllevel;
  while ((c = scan_char(o)) != 0) {
    flags &= ~HANDLE_DEFER;
    switch (c) {
    case NEWLINE:
      ++o->line;
      if (ps.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->calllevel > 1)
          goto RETURN;
        flags = 0;
        leave_defaults(&ps);
        if (in_current_node(&ps)) {
          leave_current_node(&ps);
        }
        ps.first_operator = 0;
      }
      break;
    case ':':
      if (ps.set_label) {
        warning(o, "ignoring label assignment to label reference");
        ps.set_label = NULL;
      }
      leave_defaults(&ps);
      leave_current_node(&ps);
      label_len = scan_label(o, label, ':');
      if (label_len) {
        SGS_ParseOperatorData *ref = SGS_SymTab_get(o->st, label, label_len);
        if (!ref)
          warning(o, "ignoring reference to undefined label");
        else {
          begin_node(&ps, ref, NL_REFER, false);
          flags = parse_step(&ps) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (in_defaults(&ps) || !ps.event)
        goto INVALID;
      begin_node(&ps, ps.operator, NL_REFER, true);
      flags = parse_step(&ps) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '<':
      if (parse_level(o, &ps, ps.linktype, '<'))
        goto RETURN;
      break;
    case '>':
      if (ps.scope != SCOPE_NEST) {
        warning(o, "closing '>' without opening '<'");
        break;
      }
      end_operator(&ps);
      endscope = true;
      goto RETURN;
    case 'O': {
      int32_t wave = scan_wavetype(o);
      if (wave < 0)
        break;
      begin_node(&ps, 0, ps.linktype, false);
      ps.operator->wave = wave;
      flags = parse_step(&ps) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      flags = parse_settings(&ps) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '\\':
      if (in_defaults(&ps) ||
          (ps.ps_flags & PSSD_NESTED_SCOPE && ps.event))
        goto INVALID;
      parse_waittime(&ps);
      break;
    case '\'':
      if (ps.set_label) {
        warning(o, "ignoring label assignment to label assignment");
        break;
      }
      label_len = scan_label(o, label, '\'');
      ps.set_label = SGS_SymTab_pool_str(o->st, label, label_len);
      break;
    case '{':
      end_operator(&ps);
      if (parse_level(o, &ps, ps.linktype, SCOPE_BIND))
        goto RETURN;
      /*
       * Multiple-operator node will now be ready for parsing.
       */
      flags = parse_step(&ps) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '|':
      if (in_defaults(&ps) ||
          (ps.ps_flags & PSSD_NESTED_SCOPE && ps.event))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (!ps.event) {
        warning(o, "end of sequence before any parts given");
        break;
      }
      if (ps.group_from) {
        SGS_ParseEventData *group_to = (ps.composite) ?
                                 ps.composite :
                                 ps.event;
        group_to->groupfrom = ps.group_from;
        ps.group_from = 0;
      }
      end_event(&ps);
      leave_current_node(&ps);
      break;
    case '}':
      if (ps.scope != SCOPE_BIND) {
        warning(o, "closing '}' without opening '{'");
        break;
      }
      endscope = true;
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID);
      break;
    }
    /* Return to sub-parsing routines. */
    if (flags && !(flags & HANDLE_DEFER)) {
      uint8_t test = flags;
      flags = 0;
      if (test & DEFERRED_STEP) {
        if (parse_step(&ps))
          flags = HANDLE_DEFER | DEFERRED_STEP;
      } else if (test & DEFERRED_SETTINGS)
        if (parse_settings(&ps))
          flags = HANDLE_DEFER | DEFERRED_SETTINGS;
    }
  }
FINISH:
  if (newscope == SCOPE_NEST)
    warning(o, "end of file without closing '>'s");
  if (newscope == SCOPE_BIND)
    warning(o, "end of file without closing '}'s");
RETURN:
  end_scope(&ps);
  --o->calllevel;
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && ps.scope != newscope);
}

static void postparse_passes(SGS_Parser *o);

/*
 * Default script options, used until changed by the current script.
 */
static const SGS_ParseScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_ratio = 1.f,
};

/**
 * Create instance.
 *
 * \return instance or NULL on failure
 */
SGS_Parser *SGS_create_Parser(void) {
	SGS_Parser *o = calloc(1, sizeof(SGS_Parser));
	SGS_init_Stream(&o->fstream);
	return o;
}

/**
 * Destroy instance. Frees parse results.
 */
void SGS_destroy_Parser(SGS_Parser *o) {
	SGS_Parser_clear(o);
	SGS_fini_Stream(&o->fstream);
	free(o);
}

/**
 * Process file and return result, also adding it to the
 * result list.
 *
 * The result is freed when the parser is destroyed or
 * SGS_parser_clear() is called.
 */
SGS_ParseResult *SGS_Parser_process(SGS_Parser *o, const char *fname) {
	if (!SGS_Stream_fopenrb(&o->fstream, fname)) {
		fprintf(stderr, "error: couldn't open script file \"%s\" for reading\n",
			fname);
		return NULL;
	}
	o->line = 1;
	o->st = SGS_create_SymTab();
	o->sopt = def_sopt;
	parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
	SGS_Stream_close(&o->fstream);

	SGS_ParseResult *result = calloc(1, sizeof(SGS_ParseResult));
	if (o->events) {
		postparse_passes(o);
		result->events = o->events;
	}
	result->name = fname;
	result->sopt = o->sopt;
	SGS_PList_add(&o->results, result);

	SGS_destroy_SymTab(o->st);
	o->st = NULL;
	o->events = NULL;
	o->last_event = NULL;

	return result;
}

/**
 * Get parse result list, setting it to \p dst.
 *
 * The list assigned to \p dst will be freed when the parser
 * instance is destroyed or SGS_Parser_clear() is called,
 * unless dst is added to.
 */
void SGS_Parser_get_results(SGS_Parser *o, SGS_PList *dst) {
	if (!dst) return;
	SGS_PList_copy(dst, &o->results);
}

/**
 * Clear state of parser. Destroys parse results.
 */
void SGS_Parser_clear(SGS_Parser *o) {
	SGS_ParseResult **results = (SGS_ParseResult**) SGS_PList_ITEMS(&o->results);
	for (size_t i = 0; i < o->results.count; ++i) {
		SGS_ParseResult *result = results[i];
		SGS_ParseEventData *e = result->events;
		while (e) {
			SGS_ParseEventData *next_e = e->next;
			destroy_event(e);
			e = next_e;
		}
		free(result);
	}
	SGS_PList_clear(&o->results);
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SGS_ParseEventData *to) {
  SGS_ParseEventData *e, *e_after = to->next;
  size_t i;
  uint32_t wait = 0, waitcount = 0;
  for (e = to->groupfrom; e != e_after; ) {
    SGS_ParseOperatorData **ops;
    ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ParseOperatorData *op = ops[i];
      if (e->next == e_after &&
          i == (e->operators.count - 1) &&
          op->od_flags & SGS_PSOD_TIME_DEFAULT) /* default for last node in group */
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
      if (wait < op->time_ms)
        wait = op->time_ms;
    }
    e = e->next;
    if (e) {
      /*wait -= e->wait_ms;*/
      waitcount += e->wait_ms;
    }
  }
  for (e = to->groupfrom; e != e_after; ) {
    SGS_ParseOperatorData **ops;
    ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGS_ParseOperatorData *op = ops[i];
      if (op->od_flags & SGS_PSOD_TIME_DEFAULT) {
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        op->time_ms = wait + waitcount; /* fill in sensible default time */
      }
    }
    e = e->next;
    if (e) {
      waitcount -= e->wait_ms;
    }
  }
  to->groupfrom = 0;
  if (e_after)
    e_after->wait_ms += wait;
}

static void time_operator(SGS_ParseOperatorData *op) {
  SGS_ParseEventData *e = op->event;
  if (op->valitfreq.time_ms == SGS_TIME_DEFAULT)
    op->valitfreq.time_ms = op->time_ms;
  if (op->valitamp.time_ms == SGS_TIME_DEFAULT)
    op->valitamp.time_ms = op->time_ms;
  if ((op->od_flags & (SGS_PSOD_TIME_DEFAULT | SGS_PSOD_OPERATOR_NESTED)) ==
                      (SGS_PSOD_TIME_DEFAULT | SGS_PSOD_OPERATOR_NESTED)) {
    op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
    op->time_ms = SGS_TIME_INF;
  }
  if (op->time_ms != SGS_TIME_INF && !(op->od_flags & SGS_PSOD_SILENCE_ADDED)) {
    op->time_ms += op->silence_ms;
    op->od_flags |= SGS_PSOD_SILENCE_ADDED;
  }
  if (e->ed_flags & SGS_PSED_ADD_WAIT_DURATION) {
    if (e->next)
      ((SGS_ParseEventData*)e->next)->wait_ms += op->time_ms;
    e->ed_flags &= ~SGS_PSED_ADD_WAIT_DURATION;
  }
  size_t i;
  SGS_ParseOperatorData **ops;
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->fmods);
  for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->pmods);
  for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&op->amods);
  for (i = op->amods.old_count; i < op->amods.count; ++i) {
    time_operator(ops[i]);
  }
}

static void time_event(SGS_ParseEventData *e) {
  /*
   * Fill in blank valit durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->valitpanning.time_ms == SGS_TIME_DEFAULT)
    e->valitpanning.time_ms = 1000; /* FIXME! */
  size_t i;
  SGS_ParseOperatorData **ops;
  ops = (SGS_ParseOperatorData**) SGS_PList_ITEMS(&e->operators);
  for (i = e->operators.old_count; i < e->operators.count; ++i) {
    time_operator(ops[i]);
  }
  /*
   * Timing for composites - done before event list flattened.
   */
  if (e->composite) {
    SGS_ParseEventData *ce = e->composite, *ce_prev = e;
    SGS_ParseOperatorData *ce_op, *ce_op_prev, *e_op;
    ce_op = (SGS_ParseOperatorData*) SGS_PList_GET(&ce->operators, 0);
    ce_op_prev = ce_op->on_prev;
    e_op = ce_op_prev;
    if (e_op->od_flags & SGS_PSOD_TIME_DEFAULT)
      e_op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
    for (;;) {
      ce->wait_ms += ce_op_prev->time_ms;
      if (ce_op->od_flags & SGS_PSOD_TIME_DEFAULT) {
        ce_op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        ce_op->time_ms = (ce_op->od_flags & SGS_PSOD_OPERATOR_NESTED && !ce->next) ?
                         SGS_TIME_INF :
                         ce_op_prev->time_ms - ce_op_prev->silence_ms;
      }
      time_event(ce);
      if (ce_op->time_ms == SGS_TIME_INF)
        e_op->time_ms = SGS_TIME_INF;
      else if (e_op->time_ms != SGS_TIME_INF)
        e_op->time_ms += ce_op->time_ms +
                         (ce->wait_ms - ce_op_prev->time_ms);
      ce_op->operator_params &= ~SGS_OPP_TIME;
      ce_op_prev = ce_op;
      ce = ce->next;
      if (!ce) break;
      ce_op = (SGS_ParseOperatorData*) SGS_PList_GET(&ce->operators, 0);
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
static void flatten_events(SGS_ParseEventData *e) {
  SGS_ParseEventData *ce = e->composite;
  SGS_ParseEventData *se = e->next, *se_prev = e;
  int32_t wait_ms = 0;
  int32_t added_wait_ms = 0;
  while (ce) {
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
      SGS_ParseEventData *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      SGS_ParseEventData *se_next, *ce_next;
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
  e->composite = 0;
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the audio generation program.
 */
static void postparse_passes(SGS_Parser *o) {
  SGS_ParseEventData *e;
  for (e = o->events; e; e = e->next) {
    time_event(e);
    if (e->groupfrom) group_events(e);
  }
  /*
   * Must be separated into pass following timing adjustments for events;
   * otherwise, flattening will fail to arrange events in the correct order
   * in some cases.
   */
  for (e = o->events; e; e = e->next) {
    if (e->composite) flatten_events(e);
  }
}
