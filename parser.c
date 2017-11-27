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

static void *memdup(const void *src, size_t size) {
  void *ret = malloc(size);
  if (!ret) return 0;
  memcpy(ret, src, size);
  return ret;
}

#ifdef strdup /* deal with libc issues when already provided */
# undef strdup
#endif
#define strdup _strdup
static char *_strdup(const char *src) {
  size_t len = strlen(src);
  char *ret;
  if (!len) return 0;
  ret = memdup(src, len + 1);
  if (!ret) return 0;
  ret[len] = '\0';
  return ret;
}

#define IS_WHITESPACE(c) \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

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

static int32_t getinum(FILE *f) {
  char c;
  int32_t num = -1;
  c = getc(f);
  if (c >= '0' && c <= '9') {
    num = c - '0';
    for (;;) {
      c = getc(f);
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  ungetc(c, f);
  return num;
}

static int32_t strfind(FILE *f, const char *const*str) {
  int32_t search, ret;
  uint32_t i, len, pos, matchpos;
  char c, undo[256];
  uint32_t strc;
  const char **s;
  for (len = 0, strc = 0; str[strc]; ++strc)
    if ((i = strlen(str[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = str[i];
  search = ret = -1;
  pos = matchpos = 0;
  while ((c = getc(f)) != EOF) {
    undo[pos] = c;
    for (i = 0; i < strc; ++i) {
      if (!s[i]) continue;
      else if (!s[i][pos]) {
        s[i] = 0;
        if (search == (int32_t)i) {
          ret = i;
          matchpos = pos-1;
        }
      } else if (c != s[i][pos]) {
        s[i] = 0;
        if (search == (int32_t)i) {
          search = -1;
        }
      } else
        search = i;
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
  while ((c = getc(f)) == ' ' || c == '\t') ;
  ungetc(c, f);
}

/*
 * Parser
 */

struct SGSParser {
	FILE *f;
	const char *fn;
	uint32_t line;
	uint32_t calllevel;
	uint32_t scopeid;
	char c, nextc;
	/* script data */
	SGSSymtab *st;
	SGSParseEventData *events;
	SGSParseEventData *last_event;
	SGSParseScriptOptions sopt;
	/* all results */
	SGSPList results;
};

#define VI_TIME_DEFAULT (-1) /* for valits only; masks SGS_TIME_INF */

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
  SGSParser *o;
  struct ParseScopeData *parent;
  uint32_t ps_flags;
  char scope;
  SGSParseEventData *event, *last_event;
  SGSParseOperatorData *operator, *first_operator, *last_operator;
  SGSParseOperatorData *parent_on, *on_prev;
  uint8_t linktype;
  uint8_t last_linktype; /* FIXME: kludge */
  char *set_label; /* label assigned to next node */
  /* timing/delay */
  SGSParseEventData *group_from; /* where to begin for group_events() */
  SGSParseEventData *composite; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
} ParseScopeData;

#define NEWLINE '\n'
static char read_char(SGSParser *o) {
  char c;
  eatws(o->f);
  if (o->nextc) {
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

static void read_ws(SGSParser *o) {
  char c;
  do {
    c = getc(o->f);
    if (c == ' ' || c == '\t')
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

static float read_num_r(SGSParser *o, float (*read_symbol)(SGSParser *o),
                        char *buf, uint32_t len, uint8_t pri, uint32_t level) {
  char *p = buf;
  bool dot = false;
  float num;
  char c;
  c = getc(o->f);
  if (level) read_ws(o);
  if (c == '(') {
    return read_num_r(o, read_symbol, buf, len, 255, level+1);
  }
  if (read_symbol &&
      ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    ungetc(c, o->f);
    num = read_symbol(o);
    if (num == num) /* not NAN; was recognized */
      goto LOOP;
  }
  if (c == '-') {
    *p++ = c;
    c = getc(o->f);
    if (level) read_ws(o);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = getc(o->f);
  }
  ungetc(c, o->f);
  if (p == buf) return NAN;
  *p = '\0';
  num = strtod(buf, 0);
LOOP:
  if (level) read_ws(o);
  for (;;) {
    c = getc(o->f);
    if (level) read_ws(o);
    switch (c) {
    case '(':
      num *= read_num_r(o, read_symbol, buf, len, 255, level+1);
      break;
    case ')':
      if (pri < 255)
        ungetc(c, o->f);
      return num;
      break;
    case '^':
      num = exp(log(num) * read_num_r(o, read_symbol, buf, len, 0, level));
      break;
    case '*':
      num *= read_num_r(o, read_symbol, buf, len, 1, level);
      break;
    case '/':
      num /= read_num_r(o, read_symbol, buf, len, 1, level);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += read_num_r(o, read_symbol, buf, len, 2, level);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= read_num_r(o, read_symbol, buf, len, 2, level);
      break;
    default:
      ungetc(c, o->f);
      return num;
    }
    if (num != num) {
      ungetc(c, o->f);
      return num;
    }
  }
}
static bool read_num(SGSParser *o, float (*read_symbol)(SGSParser *o),
                      float *var) {
  char buf[64];
  float num = read_num_r(o, read_symbol, buf, 64, 254, 0);
  if (num != num)
    return false;
  *var = num;
  return true;
}

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void warning(SGSParser *o, const char *str) {
  char buf[4] = {'\'', o->c, '\'', 0};
  printf("warning: %s [line %d, at %s] - %s\n", o->fn, o->line,
         (o->c == EOF ? "EOF" : buf), str);
}
#define WARN_INVALID "invalid character"

#define OCTAVES 11
static float read_note(SGSParser *o) {
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
static bool read_label(SGSParser *o, LabelBuf label, char op) {
  uint32_t i = 0;
  char nolabel_msg[] = "ignoring ? without label name";
  nolabel_msg[9] = op; /* replace ? */
  for (;;) {
    o->c = getc(o->f);
    if (IS_WHITESPACE(o->c) || o->c == EOF) {
      ungetc(o->c, o->f);
      if (i == 0)
        warning(o, nolabel_msg);
      else END_OF_LABEL: {
        label[i] = '\0';
        return true;
      }
      break;
    } else if (i == LABEL_LEN) {
      warning(o, "ignoring label name from "LABEL_LEN_A"th digit");
      goto END_OF_LABEL;
    }
    label[i++] = o->c;
  }
  return false;
}

static int32_t read_wavetype(SGSParser *o) {
  int32_t wave = strfind(o->f, SGSOsc_lut_names);
  if (wave < 0) {
    warning(o, "invalid wave type; available types are:");
    SGS_wave_t i = 0;
    printf("\t%s", SGSOsc_lut_names[i]);
    while (++i < SGS_WAVE_TYPES) {
      printf(", %s", SGSOsc_lut_names[i]);
    }
    printf("\n");
  }
  return wave;
}

static bool read_valit(SGSParser *o, float (*read_symbol)(SGSParser *o),
                        SGSProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  char c;
  bool goal = false;
  int32_t type;
  vi->time_ms = VI_TIME_DEFAULT;
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
  SGSParser *o = ps->o;
  /* FIXME: ADD_WAIT_DURATION */
  if (tryc('t', o->f)) {
    if (!ps->last_operator) {
      warning(o, "add wait for last duration before any parts given");
      return false;
    }
    ps->last_event->ed_flags |= SGS_PSED_ADD_WAIT_DURATION;
  } else {
    float wait;
    int32_t wait_ms;
    read_num(o, 0, &wait);
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
static void destroy_operator(SGSParseOperatorData *op) {
	SGS_plist_clear(&op->on_next);
	size_t i;
	SGSParseOperatorData **ops;
	ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->fmods);
	for (i = op->fmods.copy_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_plist_clear(&op->fmods);
	ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->pmods);
	for (i = op->pmods.copy_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_plist_clear(&op->pmods);
	ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->amods);
	for (i = op->amods.copy_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_plist_clear(&op->amods);
	if (op->od_flags & SGS_PSOD_LABEL_ALLOC) {
		free((char*) op->label);
	}
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event(SGSParseEventData *e) {
	size_t i;
	SGSParseOperatorData **ops;
	ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&e->operators);
	for (i = e->operators.copy_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_plist_clear(&e->operators);
	SGS_plist_clear(&e->graph);
	free(e);
}

static void end_operator(ParseScopeData *ps) {
  SGSParser *o = ps->o;
  SGSParseOperatorData *op = ps->operator;
  if (!op)
    return; /* nothing to do */
  if (!op->on_prev) { /* initial event should reset its parameters */
    op->operator_params |= SGS_P_ADJCS |
                           SGS_P_WAVE |
                           SGS_P_TIME |
                           SGS_P_SILENCE |
                           SGS_P_FREQ |
                           SGS_P_DYNFREQ |
                           SGS_P_PHASE |
                           SGS_P_AMP |
                           SGS_P_DYNAMP |
                           SGS_P_OPATTR;
  } else {
    SGSParseOperatorData *pop = op->on_prev;
    if (op->attr != pop->attr)
      op->operator_params |= SGS_P_OPATTR;
    if (op->wave != pop->wave)
      op->operator_params |= SGS_P_WAVE;
    /* SGS_TIME set when time set */
    if (op->silence_ms)
      op->operator_params |= SGS_P_SILENCE;
    /* SGS_FREQ set when freq set */
    if (op->dynfreq != pop->dynfreq)
      op->operator_params |= SGS_P_DYNFREQ;
    /* SGS_PHASE set when phase set */
    /* SGS_AMP set when amp set */
    if (op->dynamp != pop->dynamp)
      op->operator_params |= SGS_P_DYNAMP;
  }
  if (op->valitfreq.type)
    op->operator_params |= SGS_P_OPATTR |
                           SGS_P_VALITFREQ;
  if (op->valitamp.type)
    op->operator_params |= SGS_P_OPATTR |
                           SGS_P_VALITAMP;
  if (!(ps->ps_flags & PSSD_NESTED_SCOPE))
    op->amp *= o->sopt.ampmult;
  ps->operator = NULL;
  ps->last_operator = op;
}

static void end_event(ParseScopeData *ps) {
  SGSParseEventData *e = ps->event;
  SGSParseEventData *pve;
  if (!e)
    return; /* nothing to do */
  end_operator(ps);
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->voice_params |= SGS_P_VOATTR |
                       SGS_P_GRAPH |
                       SGS_P_PANNING;
  } else {
    if (e->panning != pve->panning)
      e->voice_params |= SGS_P_PANNING;
  }
  if (e->valitpanning.type)
    e->voice_params |= SGS_P_VOATTR |
                       SGS_P_VALITPANNING;
  ps->last_event = e;
  ps->event = NULL;
}

static void begin_event(ParseScopeData *ps, uint8_t linktype, bool composite) {
  SGSParser *o = ps->o;
  SGSParseEventData *e, *pve;
  end_event(ps);
  ps->event = calloc(1, sizeof(SGSParseEventData));
  e = ps->event;
  e->wait_ms = ps->next_wait_ms;
  ps->next_wait_ms = 0;
  if (ps->on_prev) {
    pve = ps->on_prev->event;
    pve->ed_flags |= SGS_PSED_VOICE_LATER_USED;
    if (pve->composite && !composite) {
      SGSParseEventData *last_ce;
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
  SGSParser *o = ps->o;
  SGSParseEventData *e = ps->event;
  SGSParseOperatorData *op, *pop = ps->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(ps);
  ps->operator = calloc(1, sizeof(SGSParseOperatorData));
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
    SGS_plist_copy(&op->fmods, &pop->fmods);
    SGS_plist_copy(&op->pmods, &pop->pmods);
    SGS_plist_copy(&op->amods, &pop->amods);
    if (ps->ps_flags & PSSD_BIND_MULTIPLE) {
      SGSParseOperatorData *mpop = pop;
      int32_t max_time = 0;
      do { 
        if (max_time < mpop->time_ms) max_time = mpop->time_ms;
        SGS_plist_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound));
      op->od_flags |= SGS_PSOD_MULTIPLE_OPERATORS;
      op->time_ms = max_time;
      ps->ps_flags &= ~PSSD_BIND_MULTIPLE;
    } else {
      SGS_plist_add(&pop->on_next, op);
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
      op->attr |= SGS_ATTR_FREQRATIO;
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
    SGS_plist_add(&e->operators, op);
    if (linktype == NL_GRAPH) {
      e->voice_params |= SGS_P_GRAPH;
      SGS_plist_add(&e->graph, op);
    }
  } else {
    SGSPList *list = NULL;
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
    ps->parent_on->operator_params |= SGS_P_ADJCS;
    SGS_plist_add(list, op);
  }
  /*
   * Assign label. If no new label but previous node (for a non-composite)
   * has one, update label to point32_t to new node, but keep pointer (and flag
   * exclusively for safe deallocation) in previous node.
   */
  if (ps->set_label) {
    SGS_symtab_set(o->st, ps->set_label, op);
    op->od_flags |= SGS_PSOD_LABEL_ALLOC;
    op->label = ps->set_label;
    ps->set_label = 0;
  } else if (!composite && pop && pop->label) {
    SGS_symtab_set(o->st, pop->label, op);
    op->label = pop->label;
  }
}

/*
 * Assign label to next node (specifically, the next operator).
 */
static void label_next_node(ParseScopeData *ps, const char *label) {
  if (ps->set_label || !label) free((char*)ps->set_label);
  ps->set_label = strdup(label);
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
static void begin_node(ParseScopeData *ps, SGSParseOperatorData *previous,
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

static void begin_scope(SGSParser *o, ParseScopeData *ps,
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
  SGSParser *o = ps->o;
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
    SGSParseEventData *group_to;
    end_event(ps);
    group_to = (ps->composite) ?  ps->composite : ps->last_event;
    if (group_to)
      group_to->groupfrom = ps->group_from;
  }
  if (ps->set_label) {
    free((char*)ps->set_label);
    warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseScopeData *ps) {
  SGSParser *o = ps->o;
  char c;
  enter_defaults(ps);
  leave_current_node(ps);
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'a':
      if (read_num(o, 0, &o->sopt.ampmult)) {
        o->sopt.changed |= SGS_PSSO_AMPMULT;
      }
      break;
    case 'f':
      if (read_num(o, read_note, &o->sopt.def_freq)) {
        o->sopt.changed |= SGS_PSSO_DEF_FREQ;
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
        o->sopt.changed |= SGS_PSSO_A4_FREQ;
      }
      break; }
    case 'r':
      if (read_num(o, 0, &o->sopt.def_ratio)) {
        o->sopt.def_ratio = 1.f / o->sopt.def_ratio;
        o->sopt.changed |= SGS_PSSO_DEF_RATIO;
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

static bool parse_level(SGSParser *o, ParseScopeData *parentnd,
                        uint8_t linktype, char newscope);

static bool parse_step(ParseScopeData *ps) {
  SGSParser *o = ps->o;
  SGSParseEventData *e = ps->event;
  SGSParseOperatorData *op = ps->operator;
  char c;
  leave_defaults(ps);
  enter_current_node(ps);
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'P':
      if (ps->ps_flags & PSSD_NESTED_SCOPE)
        goto UNKNOWN;
      if (tryc('[', o->f)) {
        if (read_valit(o, 0, &e->valitpanning))
          e->voice_attr |= SGS_ATTR_VALITPANNING;
      } else if (read_num(o, 0, &e->panning)) {
        if (!e->valitpanning.type)
          e->voice_attr &= ~SGS_ATTR_VALITPANNING;
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
      if (tryc('!', o->f)) {
        if (!testc('<', o->f)) {
          read_num(o, 0, &op->dynamp);
        }
        if (tryc('<', o->f)) {
          if (op->amods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_plist_clear(&op->amods);
          }
          parse_level(o, ps, NL_AMODS, SCOPE_NEST);
        }
      } else if (tryc('[', o->f)) {
        if (read_valit(o, 0, &op->valitamp))
          op->attr |= SGS_ATTR_VALITAMP;
      } else {
        read_num(o, 0, &op->amp);
        op->operator_params |= SGS_P_AMP;
        if (!op->valitamp.type)
          op->attr &= ~SGS_ATTR_VALITAMP;
      }
      break;
    case 'f':
      if (tryc('!', o->f)) {
        if (!testc('<', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (tryc('<', o->f)) {
          if (op->fmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_plist_clear(&op->fmods);
          }
          parse_level(o, ps, NL_FMODS, SCOPE_NEST);
        }
      } else if (tryc('[', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->attr |= SGS_ATTR_VALITFREQ;
          op->attr &= ~SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, read_note, &op->freq)) {
        op->attr &= ~SGS_ATTR_FREQRATIO;
        op->operator_params |= SGS_P_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_ATTR_VALITFREQ |
                        SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 'p':
      if (tryc('!', o->f)) {
        if (tryc('<', o->f)) {
          if (op->pmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_plist_clear(&op->pmods);
          }
          parse_level(o, ps, NL_PMODS, SCOPE_NEST);
        } else
          goto UNKNOWN;
      } else if (read_num(o, 0, &op->phase)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->operator_params |= SGS_P_PHASE;
      }
      break;
    case 'r':
      if (!(ps->ps_flags & PSSD_NESTED_SCOPE))
        goto UNKNOWN;
      if (tryc('!', o->f)) {
        if (!testc('<', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->dynfreq = 1.f / op->dynfreq;
            op->attr |= SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (tryc('<', o->f)) {
          if (op->fmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_plist_clear(&op->fmods);
          }
          parse_level(o, ps, NL_FMODS, SCOPE_NEST);
        }
      } else if (tryc('[', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->valitfreq.goal = 1.f / op->valitfreq.goal;
          op->attr |= SGS_ATTR_VALITFREQ |
                      SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, 0, &op->freq)) {
        op->freq = 1.f / op->freq;
        op->attr |= SGS_ATTR_FREQRATIO;
        op->operator_params |= SGS_P_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_ATTR_VALITFREQ |
                        SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 's': {
      float silence;
      read_num(o, 0, &silence);
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = lrint(silence * 1000.f);
      break; }
    case 't':
      if (tryc('*', o->f)) {
        op->od_flags |= SGS_PSOD_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = o->sopt.def_time_ms;
      } else if (tryc('i', o->f)) {
        if (!(ps->ps_flags & PSSD_NESTED_SCOPE)) {
          warning(o, "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        read_num(o, 0, &time);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
        op->time_ms = lrint(time * 1000.f);
      }
      op->operator_params |= SGS_P_TIME;
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
  HANDLE_DEFER = 1<<1,
  DEFERRED_STEP = 1<<2,
  DEFERRED_SETTINGS = 1<<4
};
static bool parse_level(SGSParser *o, ParseScopeData *parentps,
                        uint8_t linktype, char newscope) {
  char c;
  bool endscope = false;
  uint8_t flags = 0;
  LabelBuf label;
  ParseScopeData ps;
  begin_scope(o, &ps, parentps, linktype, newscope);
  ++o->calllevel;
  while ((c = read_char(o)) != EOF) {
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
        label_next_node(&ps, NULL);
      }
      leave_defaults(&ps);
      leave_current_node(&ps);
      if (read_label(o, label, ':')) {
        SGSParseOperatorData *ref = SGS_symtab_get(o->st, label);
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
      int32_t wave = read_wavetype(o);
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
      read_label(o, label, '\'');
      label_next_node(&ps, label);
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
        SGSParseEventData *group_to = (ps.composite) ?
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

static void postparse_passes(SGSParser *o);

/*
 * Default script options, used until changed by the current script.
 */
static const SGSParseScriptOptions def_sopt = {
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
 * \return instance
 */
SGSParser *SGS_create_parser(void) {
	SGSParser *o = calloc(1, sizeof(SGSParser));
	return o;
}

/**
 * Destroy instance. Frees parse results.
 */
void SGS_destroy_parser(SGSParser *o) {
	SGS_parser_clear(o);
	free(o);
}

/**
 * Process file and return result, also adding it to the
 * result list.
 *
 * The result is freed when the parser is destroyed or
 * SGS_parser_clear() is called.
 */
SGSParseResult *SGS_parser_process(SGSParser *o, const char *fname) {
	if (!(o->f = fopen(fname, "r"))) return NULL;
	o->fn = fname;
	o->line = 1;
	o->st = SGS_create_symtab();
	o->sopt = def_sopt;
	parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
	fclose(o->f);
	o->f = NULL;

	SGSParseResult *result = calloc(1, sizeof(SGSParseResult));
	if (o->events) {
		postparse_passes(o);
		result->events = o->events;
	}
	result->name = fname;
	result->sopt = o->sopt;
	SGS_plist_add(&o->results, result);

	SGS_destroy_symtab(o->st);
	o->st = NULL;
	o->events = NULL;
	o->last_event = NULL;

	return result;
}

/**
 * Get parse result list, setting it to \p dst.
 *
 * The list assigned to \p dst will be freed when the parser
 * instance is destroyed or SGS_parser_clear() is called,
 * unless dst is added to.
 */
void SGS_parser_get_results(SGSParser *o, SGSPList *dst) {
	if (!dst) return;
	SGS_plist_copy(dst, &o->results);
}

/**
 * Clear state of parser. Destroys parse results.
 */
void SGS_parser_clear(SGSParser *o) {
	SGSParseResult **results = (SGSParseResult**) SGS_PLIST_ITEMS(&o->results);
	for (size_t i = 0; i < o->results.count; ++i) {
		SGSParseResult *result = results[i];
		SGSParseEventData *e = result->events;
		while (e) {
			SGSParseEventData *next_e = e->next;
			destroy_event(e);
			e = next_e;
		}
		free(result);
	}
	SGS_plist_clear(&o->results);
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SGSParseEventData *to) {
  SGSParseEventData *e, *e_after = to->next;
  size_t i;
  int32_t wait = 0, waitcount = 0;
  for (e = to->groupfrom; e != e_after; ) {
    SGSParseOperatorData **ops;
    ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGSParseOperatorData *op = ops[i];
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
    SGSParseOperatorData **ops;
    ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGSParseOperatorData *op = ops[i];
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

static void time_operator(SGSParseOperatorData *op) {
  SGSParseEventData *e = op->event;
  if (op->valitfreq.time_ms == VI_TIME_DEFAULT)
    op->valitfreq.time_ms = op->time_ms;
  if (op->valitamp.time_ms == VI_TIME_DEFAULT)
    op->valitamp.time_ms = op->time_ms;
  if ((op->od_flags & (SGS_PSOD_TIME_DEFAULT | SGS_PSOD_OPERATOR_NESTED)) ==
                      (SGS_PSOD_TIME_DEFAULT | SGS_PSOD_OPERATOR_NESTED)) {
    op->od_flags &= ~SGS_PSOD_TIME_DEFAULT;
    op->time_ms = SGS_TIME_INF;
  }
  if (op->time_ms >= 0 && !(op->od_flags & SGS_PSOD_SILENCE_ADDED)) {
    op->time_ms += op->silence_ms;
    op->od_flags |= SGS_PSOD_SILENCE_ADDED;
  }
  if (e->ed_flags & SGS_PSED_ADD_WAIT_DURATION) {
    if (e->next)
      ((SGSParseEventData*)e->next)->wait_ms += op->time_ms;
    e->ed_flags &= ~SGS_PSED_ADD_WAIT_DURATION;
  }
  size_t i;
  SGSParseOperatorData **ops;
  ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->fmods);
  for (i = op->fmods.copy_count; i < op->fmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->pmods);
  for (i = op->pmods.copy_count; i < op->pmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&op->amods);
  for (i = op->amods.copy_count; i < op->amods.count; ++i) {
    time_operator(ops[i]);
  }
}

static void time_event(SGSParseEventData *e) {
  /*
   * Fill in blank valit durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->valitpanning.time_ms == VI_TIME_DEFAULT)
    e->valitpanning.time_ms = 1000; /* FIXME! */
  size_t i;
  SGSParseOperatorData **ops;
  ops = (SGSParseOperatorData**) SGS_PLIST_ITEMS(&e->operators);
  for (i = e->operators.copy_count; i < e->operators.count; ++i) {
    time_operator(ops[i]);
  }
  /*
   * Timing for composites - done before event list flattened.
   */
  if (e->composite) {
    SGSParseEventData *ce = e->composite, *ce_prev = e;
    SGSParseOperatorData *ce_op, *ce_op_prev, *e_op;
    ce_op = (SGSParseOperatorData*) SGS_PLIST_GET(&ce->operators, 0);
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
      ce_op->operator_params &= ~SGS_P_TIME;
      ce_op_prev = ce_op;
      ce = ce->next;
      if (!ce) break;
      ce_op = (SGSParseOperatorData*) SGS_PLIST_GET(&ce->operators, 0);
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
static void flatten_events(SGSParseEventData *e) {
  SGSParseEventData *ce = e->composite;
  SGSParseEventData *se = e->next, *se_prev = e;
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
      SGSParseEventData *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      SGSParseEventData *se_next, *ce_next;
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
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SGSParser *o) {
  SGSParseEventData *e;
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
