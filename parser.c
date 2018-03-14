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
 * <http://www.gnu.org/licenses/>.
 */

#include "fread.h"
#include "aoalloc.h"
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
static bool read_sym(struct SGS_FRead *fr, SymBuf_t sym, uint32_t *sym_len) {
  uint32_t i = 0;
  bool error = false;
  for (;;) {
    uint8_t c = SGS_FREAD_GETC(fr);
    if (!IS_SYMCHAR(c)) {
      SGS_FREAD_UNGETC(fr);
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

static int32_t read_inum(struct SGS_FRead *fr) {
  char c;
  int32_t num = -1;
  c = SGS_FREAD_GETC(fr);
  if (c >= '0' && c <= '9') {
    num = c - '0';
    for (;;) {
      c = SGS_FREAD_GETC(fr);
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  SGS_FREAD_UNGETC(fr);
  return num;
}

/*
 * Read string if it matches an entry in a NULL-pointer terminated array.
 */
static int32_t read_astrfind(struct SGS_FRead *fr, const char *const*astr) {
  int32_t search, ret;
  uint32_t i, len, pos, matchpos;
  char c;
  uint32_t strc;
  const char **s;
  for (len = 0, strc = 0; astr[strc]; ++strc)
    if ((i = strlen(astr[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = astr[i];
  search = ret = -1;
  pos = matchpos = 0;
  while ((c = SGS_FREAD_GETC(fr)) != 0) {
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
        search = -1;
      } else
        search = i;
    }
    if (pos == len) break;
    ++pos;
  }
  free(s);
  SGS_FREAD_UNGETN(fr, (pos-matchpos));
  return ret;
}

/*
 * Read string if it matches an entry in a NULL-pointer terminated array.
 * This version requires all array entries to be the same length.
 * The length must be less than SYMKEY_LEN.
 */
static int32_t read_astrlcmp(struct SGS_FRead *fr, const char *const*astr,
		size_t len) {
  SymBuf_t buf;
  size_t getlen = len;
  if (!SGS_fread_getn(fr, buf, &getlen)) {
    /* stream shorter than len, so no match */
    SGS_FREAD_UNGETN(fr, getlen);
    return -1;
  }
  size_t i = 0;
  for (;;) {
    const char *str = astr[i];
    if (!str) break;
    if (!strncmp(str, buf, len)) return i; /* match */
  }
  SGS_FREAD_UNGETN(fr, len);
  return -1;
}

static void read_skipws(struct SGS_FRead *fr) {
  char c;
  while ((c = SGS_FREAD_GETC(fr)) == ' ' || c == '\t') ;
  SGS_FREAD_UNGETC(fr);
}

/*
 * Parser
 */

struct SGS_Parser {
  struct SGS_FRead fr;
  SGS_SymTab_t st;
  SGS_AOAlloc_t malc;
  uint32_t line;
  uint32_t calllevel;
  uint32_t scopeid;
  char c, nextc;
  /* node state */
  struct SGS_ParseEventData *events;
  struct SGS_ParseEventData *last_event;
  /* settings/ops */
  float ampmult;
  int32_t def_time_ms;
  float def_freq, def_A4tuning, def_ratio;
  /* results */
  struct SGS_ParseList *results;
  struct SGS_ParseList *last_result;
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
  PSD_IN_DEFAULTS = 1<<0, /* adjusting default values */
  PSD_IN_NODE = 1<<1,     /* adjusting operator and/or voice */
  PSD_NESTED_SCOPE = 1<<2,
  PSD_BIND_MULTIPLE = 1<<3, /* previous node interpreted as set of nodes */
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
struct ParseScopeData {
  SGS_Parser_t o;
  struct ParseScopeData *parent;
  uint32_t ps_flags;
  char scope;
  struct SGS_ParseEventData *event, *last_event;
  struct SGS_ParseOperatorData *operator, *first_operator, *last_operator;
  struct SGS_ParseOperatorData *parent_on, *on_prev;
  uint8_t linktype;
  uint8_t last_linktype; /* FIXME: kludge */
  const char *set_label; /* label assigned to next node */
  /* timing/delay */
  struct SGS_ParseEventData *group_from; /* where to begin for group_events() */
  struct SGS_ParseEventData *composite; /* grouping of events for a voice and/or operator */
  uint32_t next_wait_ms; /* added for next event */
};

#define NEWLINE '\n'
static char scan_char(SGS_Parser_t o) {
  char c;
  read_skipws(&o->fr);
  if (o->nextc) {
    c = o->nextc;
    o->nextc = 0;
  } else {
    c = SGS_FREAD_GETC(&o->fr);
  }
  if (c == '#')
    while ((c = SGS_FREAD_GETC(&o->fr)) != '\n' && c != '\r' && c != 0) ;
  if (c == '\n') {
    SGS_FREAD_TESTCGET(&o->fr, '\r');
    c = NEWLINE;
  } else if (c == '\r') {
    SGS_FREAD_TESTCGET(&o->fr, '\n');
    c = NEWLINE;
  } else {
    read_skipws(&o->fr);
  }
  o->c = c;
  return c;
}

static void scan_ws(SGS_Parser_t o) {
  char c;
  do {
    c = SGS_FREAD_GETC(&o->fr);
    if (c == ' ' || c == '\t')
      continue;
    if (c == '\n') {
      ++o->line;
      SGS_FREAD_TESTCGET(&o->fr, '\r');
    } else if (c == '\r') {
      ++o->line;
      SGS_FREAD_TESTCGET(&o->fr, '\n');
    } else if (c == '#') {
      while ((c = SGS_FREAD_GETC(&o->fr)) != '\n' && c != '\r' && c != 0) ;
    } else {
      SGS_FREAD_UNGETC(&o->fr);
      break;
    }
  } while (c != 0);
}

static float scan_num_r(SGS_Parser_t o, float (*scan_symbol)(SGS_Parser_t o),
		char *buf, uint32_t len, uint8_t pri, uint32_t level) {
  char *p = buf;
  bool dot = false;
  float num;
  char c;
  c = SGS_FREAD_GETC(&o->fr);
  if (level) scan_ws(o);
  if (c == '(') {
    return scan_num_r(o, scan_symbol, buf, len, 255, level+1);
  }
  if (scan_symbol &&
      ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    SGS_FREAD_UNGETC(&o->fr);
    num = scan_symbol(o);
    if (num == num) /* not NAN; was recognized */
      goto LOOP;
  }
  if (c == '-') {
    *p++ = c;
    c = SGS_FREAD_GETC(&o->fr);
    if (level) scan_ws(o);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = SGS_FREAD_GETC(&o->fr);
  }
  SGS_FREAD_UNGETC(&o->fr);
  if (p == buf) return NAN;
  *p = '\0';
  num = strtod(buf, 0);
LOOP:
  if (level) scan_ws(o);
  for (;;) {
    c = SGS_FREAD_GETC(&o->fr);
    if (level) scan_ws(o);
    switch (c) {
    case '(':
      num *= scan_num_r(o, scan_symbol, buf, len, 255, level+1);
      break;
    case ')':
      if (pri < 255)
        SGS_FREAD_UNGETC(&o->fr);
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
      SGS_FREAD_UNGETC(&o->fr);
      return num;
    }
    if (num != num) {
      SGS_FREAD_UNGETC(&o->fr);
      return num;
    }
  }
}
static bool scan_num(SGS_Parser_t o, float (*scan_symbol)(SGS_Parser_t o),
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
static void warning(SGS_Parser_t o, const char *str) {
  char buf[4] = {'\'', o->c, '\'', 0};
  printf("warning: %s [line %d, at %s] - %s\n", o->fr.filename, o->line,
         (o->c == 0 ? "EOF" : buf), str);
}
#define WARN_INVALID "invalid character"

#define OCTAVES 11
static float scan_note(SGS_Parser_t o) {
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
  o->c = SGS_FREAD_GETC(&o->fr);
  int32_t octave;
  int32_t semitone = 1, note;
  int32_t subnote = -1;
  if (o->c >= 'a' && o->c <= 'g') {
    subnote = o->c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    o->c = SGS_FREAD_GETC(&o->fr);
  }
  if (o->c < 'A' || o->c > 'G') {
    warning(o, "invalid note specified - should be C, D, E, F, G, A or B");
    return NAN;
  }
  note = o->c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  o->c = SGS_FREAD_GETC(&o->fr);
  if (o->c == 's')
    semitone = 2;
  else if (o->c == 'f')
    semitone = 0;
  else
    SGS_FREAD_UNGETC(&o->fr);
  octave = read_inum(&o->fr);
  if (octave < 0) /* none given, default to 4 */
    octave = 4;
  else if (octave >= OCTAVES) {
    warning(o, "invalid octave specified for note - valid range 0-10");
    octave = 4;
  }
  freq = o->def_A4tuning * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
}

static uint32_t scan_label(SGS_Parser_t o, SymBuf_t sym, char op) {
  char nolabel_msg[] = "ignoring ? without label name";
  uint32_t len = 0;
  bool error;
  nolabel_msg[9] = op; /* replace ? */
  error = !read_sym(&o->fr, sym, &len);
  o->c = SGS_FREAD_RETC(&o->fr);
  if (len == 0) {
    warning(o, nolabel_msg);
  }
  if (error) {
    warning(o, "ignoring label name from "SYMKEY_LEN_A"th character");
  }
  return len;
}


static int32_t scan_wavetype(SGS_Parser_t o) {
  static const char *const wavetypes[] = {
    "sin",
    "srs",
    "tri",
    "sqr",
    "saw",
    0
  };
  const size_t wavetype_len = 3;
  int32_t wave = read_astrlcmp(&o->fr, wavetypes, wavetype_len);
  if (wave < 0)
    warning(o, "invalid wave type follows; sin, sqr, tri, saw available");
  return wave;
}

static bool scan_valit(SGS_Parser_t o, float (*scan_symbol)(SGS_Parser_t o),
		struct SGS_ProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  const size_t valittype_len = 3;
  char c;
  bool goal = false;
  int32_t type;
  vi->time_ms = VI_TIME_DEFAULT;
  vi->type = SGS_VALIT_LIN; /* default */
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case NEWLINE:
      ++o->line;
      break;
    case 'c':
      type = read_astrlcmp(&o->fr, valittypes, valittype_len);
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

static bool scan_waittime(struct ParseScopeData *ns) {
  SGS_Parser_t o = ns->o;
  /* FIXME: ADD_WAIT_DURATION */
  if (SGS_FREAD_TESTCGET(&o->fr, 't')) {
    if (!ns->last_operator) {
      warning(o, "add wait for last duration before any parts given");
      return false;
    }
    ns->last_event->en_flags |= PED_ADD_WAIT_DURATION;
  } else {
    float wait;
    int32_t wait_ms;
    scan_num(o, 0, &wait);
    if (wait < 0.f) {
      warning(o, "ignoring '\\' with sub-zero time");
      return false;
    }
    wait_ms = lrint(wait * 1000.f);
    ns->next_wait_ms += wait_ms;
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

static void destroy_operator(struct SGS_ParseOperatorData *op) {
  SGS_ptrarr_clear(&op->on_next);
  size_t i;
  struct SGS_ParseOperatorData **ops;
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->fmods);
  for (i = op->fmods.copy_count; i < op->fmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->pmods);
  for (i = op->pmods.copy_count; i < op->pmods.count; ++i) {
    destroy_operator(ops[i]);
  }
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->amods);
  for (i = op->amods.copy_count; i < op->amods.count; ++i) {
    destroy_operator(ops[i]);
  }
  free(op);
}

/*
 * Destroy the given event node and all associated operator nodes.
 */
void SGS_event_node_destroy(struct SGS_ParseEventData *e) {
  size_t i;
  struct SGS_ParseOperatorData **ops;
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&e->operators);
  for (i = e->operators.copy_count; i < e->operators.count; ++i) {
    destroy_operator(ops[i]);
  }
  SGS_ptrarr_clear(&e->graph);
  free(e);
}

static void end_operator(struct ParseScopeData *ns) {
  SGS_Parser_t o = ns->o;
  struct SGS_ParseOperatorData *op = ns->operator;
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
    struct SGS_ParseOperatorData *pop = op->on_prev;
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
  if (!(ns->ps_flags & PSD_NESTED_SCOPE))
    op->amp *= o->ampmult;
  ns->operator = 0;
  ns->last_operator = op;
}

static void end_event(struct ParseScopeData *ns) {
  struct SGS_ParseEventData *e = ns->event;
  struct SGS_ParseEventData *pve;
  if (!e)
    return; /* nothing to do */
  end_operator(ns);
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
  ns->last_event = e;
  ns->event = 0;
}

static void begin_event(struct ParseScopeData *ns, uint8_t linktype,
		bool composite) {
  SGS_Parser_t o = ns->o;
  struct SGS_ParseEventData *e, *pve;
  end_event(ns);
  ns->event = calloc(1, sizeof(struct SGS_ParseEventData));
  e = ns->event;
  e->wait_ms = ns->next_wait_ms;
  ns->next_wait_ms = 0;
  if (ns->on_prev) {
    pve = ns->on_prev->event;
    pve->en_flags |= PED_VOICE_LATER_USED;
    if (pve->composite && !composite) {
      struct SGS_ParseEventData *last_ce;
      for (last_ce = pve->composite; last_ce->next; last_ce = last_ce->next) ;
      last_ce->en_flags |= PED_VOICE_LATER_USED;
    }
    e->voice_prev = pve;
    e->voice_attr = pve->voice_attr;
    e->panning = pve->panning;
    e->valitpanning = pve->valitpanning;
  } else { /* set defaults */
    e->panning = 0.5f; /* center */
  }
  if (!ns->group_from)
    ns->group_from = e;
  if (composite) {
    if (!ns->composite) {
      pve->composite = e;
      ns->composite = pve;
    } else {
      pve->next = e;
    }
  } else {
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
    ns->composite = 0;
  }
}

static void begin_operator(struct ParseScopeData *ns, uint8_t linktype,
		bool composite) {
  SGS_Parser_t o = ns->o;
  struct SGS_ParseEventData *e = ns->event;
  struct SGS_ParseOperatorData *op, *pop = ns->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(ns);
  ns->operator = calloc(1, sizeof(struct SGS_ParseOperatorData));
  op = ns->operator;
  if (!ns->first_operator)
    ns->first_operator = op;
  if (!composite && ns->last_operator)
    ns->last_operator->next_bound = op;
  /*
   * Initialize node.
   */
  if (pop) {
    pop->on_flags |= POD_OPERATOR_LATER_USED;
    op->on_prev = pop;
    op->on_flags = pop->on_flags & (POD_OPERATOR_NESTED |
                                    POD_MULTIPLE_OPERATORS);
    if (composite)
      op->on_flags |= POD_TIME_DEFAULT; /* default: previous or infinite time */
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
    SGS_ptrarr_copy(&op->fmods, &pop->fmods);
    SGS_ptrarr_copy(&op->pmods, &pop->pmods);
    SGS_ptrarr_copy(&op->amods, &pop->amods);
    if (ns->ps_flags & PSD_BIND_MULTIPLE) {
      struct SGS_ParseOperatorData *mpop = pop;
      int32_t max_time = 0;
      do { 
        if (max_time < mpop->time_ms) max_time = mpop->time_ms;
        SGS_ptrarr_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound));
      op->on_flags |= POD_MULTIPLE_OPERATORS;
      op->time_ms = max_time;
      ns->ps_flags &= ~PSD_BIND_MULTIPLE;
    } else {
      SGS_ptrarr_add(&pop->on_next, op);
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->on_flags = POD_TIME_DEFAULT; /* default: depends on context */
    op->time_ms = o->def_time_ms;
    op->amp = 1.0f;
    if (!(ns->ps_flags & PSD_NESTED_SCOPE)) {
      op->freq = o->def_freq;
    } else {
      op->on_flags |= POD_OPERATOR_NESTED;
      op->freq = o->def_ratio;
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
    SGS_ptrarr_add(&e->operators, op);
    if (linktype == NL_GRAPH) {
      e->voice_params |= SGS_P_GRAPH;
      SGS_ptrarr_add(&e->graph, op);
    }
  } else {
    struct SGS_PtrArr *arr = 0;
    switch (linktype) {
    case NL_FMODS:
      arr = &ns->parent_on->fmods;
      break;
    case NL_PMODS:
      arr = &ns->parent_on->pmods;
      break;
    case NL_AMODS:
      arr = &ns->parent_on->amods;
      break;
    }
    ns->parent_on->operator_params |= SGS_P_ADJCS;
    SGS_ptrarr_add(arr, op);
  }
  /*
   * Assign label. If no new label but previous node (for a non-composite)
   * has one, update label to point to new node, but keep pointer in
   * previous node.
   */
  if (ns->set_label) {
    SGS_symtab_set(o->st, ns->set_label, op);
    op->label = ns->set_label;
    ns->set_label = NULL;
  } else if (!composite && pop && pop->label) {
    SGS_symtab_set(o->st, pop->label, op);
    op->label = pop->label;
  }
}

/*
 * Default values for new nodes are being set.
 */
#define in_defaults(ns) ((ns)->ps_flags & PSD_IN_DEFAULTS)
#define enter_defaults(ns) ((void)((ns)->ps_flags |= PSD_IN_DEFAULTS))
#define leave_defaults(ns) ((void)((ns)->ps_flags &= ~PSD_IN_DEFAULTS))

/*
 * Values for current node are being set.
 */
#define in_current_node(ns) ((ns)->ps_flags & PSD_IN_NODE)
#define enter_current_node(ns) ((void)((ns)->ps_flags |= PSD_IN_NODE))
#define leave_current_node(ns) ((void)((ns)->ps_flags &= ~PSD_IN_NODE))

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(struct ParseScopeData *ns,
		struct SGS_ParseOperatorData *previous, uint8_t linktype,
		bool composite) {
  ns->on_prev = previous;
  if (!ns->event ||
      !in_current_node(ns) /* previous event implicitly ended */ ||
      ns->next_wait_ms ||
      composite)
    begin_event(ns, linktype, composite);
  begin_operator(ns, linktype, composite);
  ns->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SGS_Parser_t o, struct ParseScopeData *ns,
		struct ParseScopeData *parent, uint8_t linktype,
		char newscope) {
  memset(ns, 0, sizeof(struct ParseScopeData));
  ns->o = o;
  ns->scope = newscope;
  if (parent) {
    ns->parent = parent;
    ns->ps_flags = parent->ps_flags;
    if (newscope == SCOPE_SAME)
      ns->scope = parent->scope;
    ns->event = parent->event;
    ns->operator = parent->operator;
    ns->parent_on = parent->parent_on;
    if (newscope == SCOPE_BIND)
      ns->group_from = parent->group_from;
    if (newscope == SCOPE_NEST) {
      ns->ps_flags |= PSD_NESTED_SCOPE;
      ns->parent_on = parent->operator;
    }
  }
  ns->linktype = linktype;
}

static void end_scope(struct ParseScopeData *ns) {
  SGS_Parser_t o = ns->o;
  end_operator(ns);
  if (ns->scope == SCOPE_BIND) {
    if (!ns->parent->group_from)
      ns->parent->group_from = ns->group_from;
    /*
     * Begin multiple-operator node in parent scope for the operator nodes in
     * this scope, provided any are present.
     */
    if (ns->first_operator) {
      ns->parent->ps_flags |= PSD_BIND_MULTIPLE;
      begin_node(ns->parent, ns->first_operator, ns->parent->last_linktype, false);
    }
  } else if (!ns->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    struct SGS_ParseEventData *group_to;
    end_event(ns);
    group_to = (ns->composite) ?  ns->composite : ns->last_event;
    if (group_to)
      group_to->groupfrom = ns->group_from;
  }
  if (ns->set_label) {
    warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static bool parse_settings(struct ParseScopeData *ns) {
  SGS_Parser_t o = ns->o;
  char c;
  enter_defaults(ns);
  leave_current_node(ns);
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case 'a':
      scan_num(o, 0, &o->ampmult);
      break;
    case 'f':
      scan_num(o, scan_note, &o->def_freq);
      break;
    case 'n': {
      float freq;
      scan_num(o, 0, &freq);
      if (freq < 1.f) {
        warning(o, "ignoring tuning frequency smaller than 1.0");
        break;
      }
      o->def_A4tuning = freq;
      break; }
    case 'r':
      if (scan_num(o, 0, &o->def_ratio))
        o->def_ratio = 1.f / o->def_ratio;
      break;
    case 't': {
      float time;
      scan_num(o, 0, &time);
      if (time < 0.f) {
        warning(o, "ignoring 't' with sub-zero time");
        break;
      }
      o->def_time_ms = lrint(time * 1000.f);
      break; }
    default:
    /*UNKNOWN:*/
      o->nextc = c;
      return true; /* let parse_level() take care of it */
    }
  }
  return false;
}

static bool parse_level(SGS_Parser_t o, struct ParseScopeData *parentps,
		uint8_t linktype, char newscope);

static bool parse_step(struct ParseScopeData *ns) {
  SGS_Parser_t o = ns->o;
  struct SGS_ParseEventData *e = ns->event;
  struct SGS_ParseOperatorData *op = ns->operator;
  char c;
  leave_defaults(ns);
  enter_current_node(ns);
  while ((c = scan_char(o)) != 0) {
    switch (c) {
    case 'P':
      if (ns->ps_flags & PSD_NESTED_SCOPE)
        goto UNKNOWN;
      if (SGS_FREAD_TESTCGET(&o->fr, '[')) {
        if (scan_valit(o, 0, &e->valitpanning))
          e->voice_attr |= SGS_ATTR_VALITPANNING;
      } else if (scan_num(o, 0, &e->panning)) {
        if (!e->valitpanning.type)
          e->voice_attr &= ~SGS_ATTR_VALITPANNING;
      }
      break;
    case '\\':
      if (scan_waittime(ns)) {
        begin_node(ns, ns->operator, NL_REFER, false);
      }
      break;
    case 'a':
      if (ns->linktype == NL_AMODS ||
          ns->linktype == NL_FMODS)
        goto UNKNOWN;
      if (SGS_FREAD_TESTCGET(&o->fr, '!')) {
        if (!SGS_FREAD_TESTC(&o->fr, '<')) {
          scan_num(o, 0, &op->dynamp);
        }
        if (SGS_FREAD_TESTCGET(&o->fr, '<')) {
          if (op->amods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_ptrarr_clear(&op->amods);
          }
          parse_level(o, ns, NL_AMODS, SCOPE_NEST);
        }
      } else if (SGS_FREAD_TESTCGET(&o->fr, '[')) {
        if (scan_valit(o, 0, &op->valitamp))
          op->attr |= SGS_ATTR_VALITAMP;
      } else {
        scan_num(o, 0, &op->amp);
        op->operator_params |= SGS_P_AMP;
        if (!op->valitamp.type)
          op->attr &= ~SGS_ATTR_VALITAMP;
      }
      break;
    case 'f':
      if (SGS_FREAD_TESTCGET(&o->fr, '!')) {
        if (!SGS_FREAD_TESTC(&o->fr, '<')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (SGS_FREAD_TESTCGET(&o->fr, '<')) {
          if (op->fmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_ptrarr_clear(&op->fmods);
          }
          parse_level(o, ns, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_FREAD_TESTCGET(&o->fr, '[')) {
        if (scan_valit(o, scan_note, &op->valitfreq)) {
          op->attr |= SGS_ATTR_VALITFREQ;
          op->attr &= ~SGS_ATTR_VALITFREQRATIO;
        }
      } else if (scan_num(o, scan_note, &op->freq)) {
        op->attr &= ~SGS_ATTR_FREQRATIO;
        op->operator_params |= SGS_P_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_ATTR_VALITFREQ |
                        SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 'p':
      if (SGS_FREAD_TESTCGET(&o->fr, '!')) {
        if (SGS_FREAD_TESTCGET(&o->fr, '<')) {
          if (op->pmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_ptrarr_clear(&op->pmods);
          }
          parse_level(o, ns, NL_PMODS, SCOPE_NEST);
        } else
          goto UNKNOWN;
      } else if (scan_num(o, 0, &op->phase)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->operator_params |= SGS_P_PHASE;
      }
      break;
    case 'r':
      if (!(ns->ps_flags & PSD_NESTED_SCOPE))
        goto UNKNOWN;
      if (SGS_FREAD_TESTCGET(&o->fr, '!')) {
        if (!SGS_FREAD_TESTC(&o->fr, '<')) {
          if (scan_num(o, 0, &op->dynfreq)) {
            op->dynfreq = 1.f / op->dynfreq;
            op->attr |= SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (SGS_FREAD_TESTCGET(&o->fr, '<')) {
          if (op->fmods.count) {
            op->operator_params |= SGS_P_ADJCS;
            SGS_ptrarr_clear(&op->fmods);
          }
          parse_level(o, ns, NL_FMODS, SCOPE_NEST);
        }
      } else if (SGS_FREAD_TESTCGET(&o->fr, '[')) {
        if (scan_valit(o, scan_note, &op->valitfreq)) {
          op->valitfreq.goal = 1.f / op->valitfreq.goal;
          op->attr |= SGS_ATTR_VALITFREQ |
                      SGS_ATTR_VALITFREQRATIO;
        }
      } else if (scan_num(o, 0, &op->freq)) {
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
      scan_num(o, 0, &silence);
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time");
        break;
      }
      op->silence_ms = lrint(silence * 1000.f);
      break; }
    case 't':
      if (SGS_FREAD_TESTCGET(&o->fr, '*')) {
        op->on_flags |= POD_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = o->def_time_ms;
      } else if (SGS_FREAD_TESTCGET(&o->fr, 'i')) {
        if (!(ns->ps_flags & PSD_NESTED_SCOPE)) {
          warning(o, "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->on_flags &= ~POD_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        scan_num(o, 0, &time);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->on_flags &= ~POD_TIME_DEFAULT;
        op->time_ms = lrint(time * 1000.f);
      }
      op->operator_params |= SGS_P_TIME;
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
static bool parse_level(SGS_Parser_t o, struct ParseScopeData *parentps,
		uint8_t linktype, char newscope) {
  SymBuf_t label;
  struct ParseScopeData ps;
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
        struct SGS_ParseOperatorData *ref = SGS_symtab_get(o->st, label);
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
          (ps.ps_flags & PSD_NESTED_SCOPE && ps.event))
        goto INVALID;
      scan_waittime(&ps);
      break;
    case '\'':
      if (ps.set_label) {
        warning(o, "ignoring label assignment to label assignment");
        break;
      }
      label_len = scan_label(o, label, '\'');
      ps.set_label = SGS_symtab_pool_str(o->st, label, label_len);
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
          (ps.ps_flags & PSD_NESTED_SCOPE && ps.event))
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
        struct SGS_ParseEventData *group_to = (ps.composite) ?
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

static void postparse_passes(SGS_Parser_t o);

/*
 * Set default values, used until changed by a script.
 */
static void set_defaults(SGS_Parser_t o) {
	o->ampmult = 1.f;
	o->def_time_ms = 1000;
	o->def_freq = 444.f;
	o->def_A4tuning = 444.f;
	o->def_ratio = 1.f;
}

/**
 * Create instance.
 */
SGS_Parser_t SGS_create_parser(void) {
	SGS_Parser_t o = calloc(1, sizeof(struct SGS_Parser));
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_parser(SGS_Parser_t o) {
	SGS_parser_clear(o);
	free(o);
}

/*
 * Update parser results, adding the result for the last file.
 */
static void add_result(SGS_Parser_t o) {
	struct SGS_ParseList *result;
	struct SGS_ParseEventData *events = o->events;

	result = SGS_aoalloc_alloc(o->malc, sizeof(struct SGS_ParseList));
	if (!result) {
		o->last_result = NULL;
		return;
	}
	result->events = events;
	result->next = NULL;
	if (!o->results) {
		o->results = result;
	}
	if (o->last_result) {
		o->last_result->next = result;
	}
	o->last_result = result;
}

/**
 * Process file and return result.
 *
 * Create symbol table and set default values if not done,
 * or if state cleared.
 *
 * The result is freed when the parser is destroyed.
 */
struct SGS_ParseList *SGS_parser_process(SGS_Parser_t o, const char *filename) {
	if (!SGS_FREAD_OPEN(&o->fr, filename)) {
		fprintf(stderr, "error: couldn't open script file \"%s\" for reading\n",
			filename);
		return NULL;
	}
	if (!o->st) {
		o->st = SGS_create_symtab();
		set_defaults(o);
	}
	if (!o->malc) {
		o->malc = SGS_create_aoalloc(0);
	}
	o->line = 1;
	parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
	SGS_FREAD_CLOSE(&o->fr);
	postparse_passes(o);

	add_result(o);
  	return o->last_result;
}

/**
 * Returns parse result list.
 */
struct SGS_ParseList *SGS_parser_get_results(SGS_Parser_t o) {
	return o->results;
}

/**
 * Clear state of parser. Destroy symbol table.
 *
 * FIXME: While result list is freed here, the data contained
 * in them is freed by the program builder code.
 */
void SGS_parser_clear(SGS_Parser_t o) {
	if (o->st) {
		SGS_destroy_symtab(o->st);
		o->st = NULL;
	}
	if (o->malc) {
		SGS_destroy_aoalloc(o->malc);
		o->malc = NULL;
	}
	o->results = NULL;
	o->last_result = NULL;
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(struct SGS_ParseEventData *to) {
  struct SGS_ParseEventData *e, *e_after = to->next;
  size_t i;
  int32_t wait = 0, waitcount = 0;
  for (e = to->groupfrom; e != e_after; ) {
    struct SGS_ParseOperatorData **ops;
    ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      struct SGS_ParseOperatorData *op = ops[i];
      if (e->next == e_after &&
          i == (e->operators.count - 1) &&
          op->on_flags & POD_TIME_DEFAULT) /* default for last node in group */
        op->on_flags &= ~POD_TIME_DEFAULT;
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
    struct SGS_ParseOperatorData **ops;
    ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      struct SGS_ParseOperatorData *op = ops[i];
      if (op->on_flags & POD_TIME_DEFAULT) {
        op->on_flags &= ~POD_TIME_DEFAULT;
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

static void time_operator(struct SGS_ParseOperatorData *op) {
  struct SGS_ParseEventData *e = op->event;
  if (op->valitfreq.time_ms == VI_TIME_DEFAULT)
    op->valitfreq.time_ms = op->time_ms;
  if (op->valitamp.time_ms == VI_TIME_DEFAULT)
    op->valitamp.time_ms = op->time_ms;
  if ((op->on_flags & (POD_TIME_DEFAULT | POD_OPERATOR_NESTED)) ==
                      (POD_TIME_DEFAULT | POD_OPERATOR_NESTED)) {
    op->on_flags &= ~POD_TIME_DEFAULT;
    op->time_ms = SGS_TIME_INF;
  }
  if (op->time_ms >= 0 && !(op->on_flags & POD_SILENCE_ADDED)) {
    op->time_ms += op->silence_ms;
    op->on_flags |= POD_SILENCE_ADDED;
  }
  if (e->en_flags & PED_ADD_WAIT_DURATION) {
    if (e->next)
      ((struct SGS_ParseEventData*)e->next)->wait_ms += op->time_ms;
    e->en_flags &= ~PED_ADD_WAIT_DURATION;
  }
  size_t i;
  struct SGS_ParseOperatorData **ops;
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->fmods);
  for (i = op->fmods.copy_count; i < op->fmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->pmods);
  for (i = op->pmods.copy_count; i < op->pmods.count; ++i) {
    time_operator(ops[i]);
  }
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&op->amods);
  for (i = op->amods.copy_count; i < op->amods.count; ++i) {
    time_operator(ops[i]);
  }
}

static void time_event(struct SGS_ParseEventData *e) {
  /*
   * Fill in blank valit durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->valitpanning.time_ms == VI_TIME_DEFAULT)
    e->valitpanning.time_ms = 1000; /* FIXME! */
  size_t i;
  struct SGS_ParseOperatorData **ops;
  ops = (struct SGS_ParseOperatorData**) SGS_PTRARR_ITEMS(&e->operators);
  for (i = e->operators.copy_count; i < e->operators.count; ++i) {
    time_operator(ops[i]);
  }
  /*
   * Timing for composites - done before event list flattened.
   */
  if (e->composite) {
    struct SGS_ParseEventData *ce = e->composite, *ce_prev = e;
    struct SGS_ParseOperatorData *ce_op = (struct SGS_ParseOperatorData*)
	                     SGS_PTRARR_GET(&ce->operators, 0),
                    *ce_op_prev = ce_op->on_prev,
                    *e_op = ce_op_prev;
    if (e_op->on_flags & POD_TIME_DEFAULT)
      e_op->on_flags &= ~POD_TIME_DEFAULT;
    for (;;) {
      ce->wait_ms += ce_op_prev->time_ms;
      if (ce_op->on_flags & POD_TIME_DEFAULT) {
        ce_op->on_flags &= ~POD_TIME_DEFAULT;
        ce_op->time_ms = (ce_op->on_flags & POD_OPERATOR_NESTED && !ce->next) ?
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
      ce_op = (struct SGS_ParseOperatorData*) SGS_PTRARR_GET(&ce->operators, 0);
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
static void flatten_events(struct SGS_ParseEventData *e) {
  struct SGS_ParseEventData *ce = e->composite;
  struct SGS_ParseEventData *se = e->next, *se_prev = e;
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
      struct SGS_ParseEventData *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      struct SGS_ParseEventData *se_next, *ce_next;
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
static void postparse_passes(SGS_Parser_t o) {
  struct SGS_ParseEventData *e;
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
