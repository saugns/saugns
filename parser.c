/* sgensys script parser module.
 * Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "program.h"
#include "parser.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdlib.h>

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

static uchar testc(char c, FILE *f) {
  char gc = getc(f);
  ungetc(gc, f);
  return (gc == c);
}

static uchar testgetc(char c, FILE *f) {
  char gc;
  if ((gc = getc(f)) == c) return 1;
  ungetc(gc, f);
  return 0;
}

static int getinum(FILE *f) {
  char c;
  int num = -1;
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

static int strfind(FILE *f, const char *const*str) {
  int ret;
  uint i, len, pos, matchpos;
  char c, undo[256];
  uint strc;
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
  while ((c = getc(f)) == ' ' || c == '\t') ;
  ungetc(c, f);
}

/*
 * Parsing code
 */

#define VI_TIME_DEFAULT (-1) /* for valits only; masks SGS_TIME_INF */

enum {
  /* parsing scopes */
  SCOPE_SAME = 0,
  SCOPE_TOP = 1,
  SCOPE_BIND = '{',
  SCOPE_NEST = '<'
};

enum {
  NS_IN_DEFAULTS = 1<<0, /* adjusting default values */
  NS_IN_NODE = 1<<1,     /* adjusting operator and/or voice */
  NS_NESTED_SCOPE = 1<<2,
  NS_BIND_MULTIPLE = 1<<3, /* previous node interpreted as set of nodes */
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
typedef struct NodeScope {
  SGSParser *o;
  struct NodeScope *parent;
  uint ns_flags;
  char scope;
  SGSEventNode *event, *last_event;
  SGSOperatorNode *operator, *first_operator, *last_operator;
  SGSOperatorNode *parent_on, *on_prev;
  uchar linktype;
  uchar last_linktype; /* FIXME: kludge */
  char *set_label;
  /* timing/delay */
  SGSEventNode *group_from; /* where to begin for group_events() */
  SGSEventNode *composite; /* grouping of events for a voice and/or operator */
  uint next_wait_ms; /* added for next event */
} NodeScope;

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
    testgetc('\r', o->f);
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
      testgetc('\r', o->f);
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
                        char *buf, uint len, uchar pri, uint level) {
  char *p = buf;
  uchar dot = 0;
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
static uchar read_num(SGSParser *o, float (*read_symbol)(SGSParser *o),
                      float *var) {
  char buf[64];
  float num = read_num_r(o, read_symbol, buf, 64, 254, 0);
  if (num != num)
    return 0;
  *var = num;
  return 1;
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
  int octave;
  int semitone = 1, note;
  int subnote = -1;
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
  freq = o->def_A4tuning * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
}

#define LABEL_LEN 80
#define LABEL_LEN_A "80"
typedef char LabelBuf[LABEL_LEN];
static uchar read_label(SGSParser *o, LabelBuf label, char op) {
  uint i = 0;
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
        return 1;
      }
      break;
    } else if (i == LABEL_LEN) {
      warning(o, "ignoring label name from "LABEL_LEN_A"th digit");
      goto END_OF_LABEL;
    }
    label[i++] = o->c;
  }
  return 0;
}

static int read_wavetype(SGSParser *o) {
  static const char *const wavetypes[] = {
    "sin",
    "srs",
    "tri",
    "sqr",
    "saw",
    0
  };
  int wave = strfind(o->f, wavetypes);
  if (wave < 0)
    warning(o, "invalid wave type follows; sin, sqr, tri, saw available");
  return wave;
}

static uchar read_valit(SGSParser *o, float (*read_symbol)(SGSParser *o),
                        SGSProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  char c;
  uchar goal = 0;
  int type;
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
        SET_I2FV(vi->time_ms, time*1000.f);
      }
      break; }
    case 'v':
      if (read_num(o, read_symbol, &vi->goal))
        goal = 1;
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
    return 0;
  }
  return 1;
}

static uchar read_waittime(NodeScope *ns) {
  SGSParser *o = ns->o;
  /* FIXME: ADD_WAIT_DURATION */
  if (testgetc('t', o->f)) {
    if (!ns->last_operator) {
      warning(o, "add wait for last duration before any parts given");
      return 0;
    }
    ns->last_event->en_flags |= EN_ADD_WAIT_DURATION;
  } else {
    float wait;
    int wait_ms;
    read_num(o, 0, &wait);
    if (wait < 0.f) {
      warning(o, "ignoring '\\' with sub-zero time");
      return 0;
    }
    SET_I2FV(wait_ms, wait*1000.f);
    ns->next_wait_ms += wait_ms;
  }
  return 1;
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
 * Add a node to the given node list.
 */
void SGS_node_list_add(SGSNodeList *list, struct SGSOperatorNode *n) {
  SGSOperatorNode **nl;
  ++list->count;
  if (list->count <= 2) {
    if (list->count == 1) {
      list->data = n;
    } else {
      void *tmp = list->data;
      list->data = malloc(sizeof(struct SGSOperatorNode*) * 2);
      nl = list->data;
      nl[0] = tmp;
      nl[1] = n;
    }
  } else if ((list->count - 1) == list->inactive_count) {
    void *tmp = list->data;
    list->data = malloc(sizeof(struct SGSOperatorNode*) * list->count);
    memcpy(list->data, tmp,
           sizeof(struct SGSOperatorNode*) * (list->count - 1));
    nl = list->data;
    nl[list->count - 1] = n;
  } else {
    list->data = realloc(list->data,
                         sizeof(struct SGSOperatorNode*) * list->count);
    nl = list->data;
    nl[list->count - 1] = n;
  }
}

/*
 * Clear the given node list.
 */
void SGS_node_list_clear(SGSNodeList *list) {
  if (list->count > 1 &&
      list->count > list->inactive_count) free(list->data);
  list->count = 0;
  list->inactive_count = 0;
  list->data = 0;
}

/*
 * Copy the node list src to dst; to save memory and limit fragmentation,
 * dst will actually merely reference the data in src unless/until modified.
 *
 * This is a "safe copy", meaning the copied node entries at the beginning
 * of the list will remain "inactive" - SGS_node_list_rforeach() as well as
 * the cleanup code of the owner of the list will ignore them, avoiding
 * duplicate operations.
 *
 * Manual (read-only) access of the list will still give access to the
 * "inactive" nodes, unless deliberately beginning iteration at
 * inactive_count.
 */
void SGS_node_list_safe_copy(SGSNodeList *dst, const SGSNodeList *src) {
  SGS_node_list_clear(dst);
  dst->count = src->count;
  dst->inactive_count = src->count;
  dst->data = src->data;
}

/*
 * Loop and recurse through the operator hierarchy beginning with the given
 * node list, calling the provided callback with the provided argument for
 * each active operator node before entering the next level. The callback
 * return values are summed for each level and then returned.
 */
int SGS_node_list_rforeach(SGSNodeList *list,
                           int (*callback)(struct SGSOperatorNode *op,
                                           void *arg),
                           void *arg) {
  SGSOperatorNode **nl = SGS_NODE_LIST_GET(list);
  int ret = 0;
  uint i;
  for (i = list->inactive_count; i < list->count; ++i) {
    SGSOperatorNode *op = nl[i];
    ret += callback(op, arg);
    ret += SGS_node_list_rforeach(&op->fmods, callback, arg);
    ret += SGS_node_list_rforeach(&op->pmods, callback, arg);
    ret += SGS_node_list_rforeach(&op->amods, callback, arg);
  }
  return ret;
}

/*
 * Recurse and free all active operator nodes and node lists within and
 * including the given list.
 */
static void SGS_node_list_rcleanup(SGSNodeList *list) {
  SGSOperatorNode **nl = SGS_NODE_LIST_GET(list);
  uint i;
  for (i = list->inactive_count; i < list->count; ++i) {
    SGSOperatorNode *op = nl[i];
    SGS_node_list_clear(&op->on_next);
    SGS_node_list_rcleanup(&op->fmods);
    SGS_node_list_rcleanup(&op->pmods);
    SGS_node_list_rcleanup(&op->amods);
    if (op->on_flags & ON_LABEL_ALLOC) free((char*)op->label);
    free(op);
  }
  SGS_node_list_clear(list);
}

/*
 * Destroy the given event node and all associated operator nodes.
 */
void SGS_event_node_destroy(SGSEventNode *e) {
  SGS_node_list_rcleanup(&e->operators);
  SGS_node_list_clear(&e->graph);
  free(e);
}

static void end_operator(NodeScope *ns) {
  SGSParser *o = ns->o;
  SGSOperatorNode *op = ns->operator;
  if (!op)
    return; /* nothing to do */
  if (!op->on_prev) { /* initial event should reset its parameters */
    op->operator_params |= SGS_ADJCS |
                           SGS_WAVE |
                           SGS_TIME |
                           SGS_SILENCE |
                           SGS_FREQ |
                           SGS_DYNFREQ |
                           SGS_PHASE |
                           SGS_AMP |
                           SGS_DYNAMP |
                           SGS_OPATTR;
  } else {
    SGSOperatorNode *pop = op->on_prev;
    if (op->attr != pop->attr)
      op->operator_params |= SGS_OPATTR;
    if (op->wave != pop->wave)
      op->operator_params |= SGS_WAVE;
    /* SGS_TIME set when time set */
    if (op->silence_ms)
      op->operator_params |= SGS_SILENCE;
    /* SGS_FREQ set when freq set */
    if (op->dynfreq != pop->dynfreq)
      op->operator_params |= SGS_DYNFREQ;
    /* SGS_PHASE set when phase set */
    /* SGS_AMP set when amp set */
    if (op->dynamp != pop->dynamp)
      op->operator_params |= SGS_DYNAMP;
  }
  if (op->valitfreq.type)
    op->operator_params |= SGS_OPATTR |
                           SGS_VALITFREQ;
  if (op->valitamp.type)
    op->operator_params |= SGS_OPATTR |
                           SGS_VALITAMP;
  if (!(ns->ns_flags & NS_NESTED_SCOPE))
    op->amp *= o->ampmult;
  ns->operator = 0;
  ns->last_operator = op;
}

static void end_event(NodeScope *ns) {
  SGSParser *o = ns->o;
  SGSEventNode *e = ns->event;
  SGSEventNode *pve;
  if (!e)
    return; /* nothing to do */
  end_operator(ns);
  pve = e->voice_prev;
  if (!pve) { /* initial event should reset its parameters */
    e->voice_params |= SGS_VOATTR |
                       SGS_GRAPH |
                       SGS_PANNING;
  } else {
    if (e->panning != pve->panning)
      e->voice_params |= SGS_PANNING;
  }
  if (e->valitpanning.type)
    e->voice_params |= SGS_VOATTR |
                       SGS_VALITPANNING;
  ns->last_event = e;
  ns->event = 0;
}

static void begin_event(NodeScope *ns, uchar linktype, uchar composite) {
  SGSParser *o = ns->o;
  SGSEventNode *e, *pve;
  end_event(ns);
  ns->event = calloc(1, sizeof(SGSEventNode));
  e = ns->event;
  e->wait_ms = ns->next_wait_ms;
  ns->next_wait_ms = 0;
  if (ns->on_prev) {
    pve = ns->on_prev->event;
    pve->en_flags |= EN_VOICE_LATER_USED;
    if (pve->composite && !composite) {
      SGSEventNode *last_ce;
      for (last_ce = pve->composite; last_ce->next; last_ce = last_ce->next) ;
      last_ce->en_flags |= EN_VOICE_LATER_USED;
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

static void begin_operator(NodeScope *ns, uchar linktype, uchar composite) {
  SGSParser *o = ns->o;
  SGSEventNode *e = ns->event;
  SGSOperatorNode *op, *pop = ns->on_prev;
  /*
   * It is assumed that a valid voice event exists.
   */
  end_operator(ns);
  ns->operator = calloc(1, sizeof(SGSOperatorNode));
  op = ns->operator;
  if (!ns->first_operator)
    ns->first_operator = op;
  if (!composite && ns->last_operator)
    ns->last_operator->next_bound = op;
  /*
   * Initialize node.
   */
  if (pop) {
    pop->on_flags |= ON_OPERATOR_LATER_USED;
    op->on_prev = pop;
    op->on_flags = pop->on_flags & (ON_OPERATOR_NESTED |
                                    ON_MULTIPLE_OPERATORS);
    if (composite)
      op->on_flags |= ON_TIME_DEFAULT; /* default: previous or infinite time */
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
    SGS_node_list_safe_copy(&op->fmods, &pop->fmods);
    SGS_node_list_safe_copy(&op->pmods, &pop->pmods);
    SGS_node_list_safe_copy(&op->amods, &pop->amods);
    if (ns->ns_flags & NS_BIND_MULTIPLE) {
      SGSOperatorNode *mpop = pop;
      int max_time = 0;
      do { 
        if (max_time < mpop->time_ms) max_time = mpop->time_ms;
        SGS_node_list_add(&mpop->on_next, op);
      } while ((mpop = mpop->next_bound));
      op->on_flags |= ON_MULTIPLE_OPERATORS;
      op->time_ms = max_time;
      ns->ns_flags &= ~NS_BIND_MULTIPLE;
    } else {
      SGS_node_list_add(&pop->on_next, op);
    }
  } else {
    /*
     * New operator with initial parameter values.
     */
    op->on_flags = ON_TIME_DEFAULT; /* default: depends on context */
    op->time_ms = o->def_time_ms;
    op->amp = 1.0f;
    if (!(ns->ns_flags & NS_NESTED_SCOPE)) {
      op->freq = o->def_freq;
    } else {
      op->on_flags |= ON_OPERATOR_NESTED;
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
    SGS_node_list_add(&e->operators, op);
    if (linktype == NL_GRAPH) {
      e->voice_params |= SGS_GRAPH;
      SGS_node_list_add(&e->graph, op);
    }
  } else {
    SGSNodeList *list = 0;
    switch (linktype) {
    case NL_FMODS:
      list = &ns->parent_on->fmods;
      break;
    case NL_PMODS:
      list = &ns->parent_on->pmods;
      break;
    case NL_AMODS:
      list = &ns->parent_on->amods;
      break;
    }
    ns->parent_on->operator_params |= SGS_ADJCS;
    SGS_node_list_add(list, op);
  }
  /*
   * Assign label. If no new label but previous node (for a non-composite)
   * has one, update label to point to new node, but keep pointer (and flag
   * exclusively for safe deallocation) in previous node.
   */
  if (ns->set_label) {
    SGS_symtab_set(o->st, ns->set_label, op);
    op->on_flags |= ON_LABEL_ALLOC;
    op->label = ns->set_label;
    ns->set_label = 0;
  } else if (!composite && pop && pop->label) {
    SGS_symtab_set(o->st, pop->label, op);
    op->label = pop->label;
  }
}

/*
 * Assign label to next node (specifically, the next operator).
 */
static void label_next_node(NodeScope *ns, const char *label) {
  if (ns->set_label || !label) free((char*)ns->set_label);
  ns->set_label = strdup(label);
}

/*
 * Default values for new nodes are being set.
 */
#define in_defaults(ns) ((ns)->ns_flags & NS_IN_DEFAULTS)
#define enter_defaults(ns) ((void)((ns)->ns_flags |= NS_IN_DEFAULTS))
#define leave_defaults(ns) ((void)((ns)->ns_flags &= ~NS_IN_DEFAULTS))

/*
 * Values for current node are being set.
 */
#define in_current_node(ns) ((ns)->ns_flags & NS_IN_NODE)
#define enter_current_node(ns) ((void)((ns)->ns_flags |= NS_IN_NODE))
#define leave_current_node(ns) ((void)((ns)->ns_flags &= ~NS_IN_NODE))

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(NodeScope *ns, SGSOperatorNode *previous,
                       uchar linktype, uchar composite) {
  ns->on_prev = previous;
  if (!ns->event ||
      !in_current_node(ns) /* previous event implicitly ended */ ||
      ns->next_wait_ms ||
      composite)
    begin_event(ns, linktype, composite);
  begin_operator(ns, linktype, composite);
  ns->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SGSParser *o, NodeScope *ns, NodeScope *parent,
                        uchar linktype, char newscope) {
  memset(ns, 0, sizeof(NodeScope));
  ns->o = o;
  ns->scope = newscope;
  if (parent) {
    ns->parent = parent;
    ns->ns_flags = parent->ns_flags;
    if (newscope == SCOPE_SAME)
      ns->scope = parent->scope;
    ns->event = parent->event;
    ns->operator = parent->operator;
    ns->parent_on = parent->parent_on;
    if (newscope == SCOPE_BIND)
      ns->group_from = parent->group_from;
    if (newscope == SCOPE_NEST) {
      ns->ns_flags |= NS_NESTED_SCOPE;
      ns->parent_on = parent->operator;
    }
  }
  ns->linktype = linktype;
}

static void end_scope(NodeScope *ns) {
  SGSParser *o = ns->o;
  end_operator(ns);
  if (ns->scope == SCOPE_BIND) {
    if (!ns->parent->group_from)
      ns->parent->group_from = ns->group_from;
    /*
     * Begin multiple-operator node in parent scope for the operator nodes in
     * this scope, provided any are present.
     */
    if (ns->first_operator) {
      ns->parent->ns_flags |= NS_BIND_MULTIPLE;
      begin_node(ns->parent, ns->first_operator, ns->parent->last_linktype, 0);
    }
  } else if (!ns->parent) {
    /*
     * At end of top scope, ie. at end of script - end last event and adjust
     * timing.
     */
    SGSEventNode *group_to;
    end_event(ns);
    group_to = (ns->composite) ?  ns->composite : ns->last_event;
    if (group_to)
      group_to->groupfrom = ns->group_from;
  }
  if (ns->set_label) {
    free((char*)ns->set_label);
    warning(o, "ignoring label assignment without operator");
  }
}

/*
 * Main parser functions
 */

static uchar parse_settings(NodeScope *ns) {
  SGSParser *o = ns->o;
  char c;
  enter_defaults(ns);
  leave_current_node(ns);
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'a':
      read_num(o, 0, &o->ampmult);
      break;
    case 'f':
      read_num(o, read_note, &o->def_freq);
      break;
    case 'n': {
      float freq;
      read_num(o, 0, &freq);
      if (freq < 1.f) {
        warning(o, "ignoring tuning frequency smaller than 1.0");
        break;
      }
      o->def_A4tuning = freq;
      break; }
    case 'r':
      if (read_num(o, 0, &o->def_ratio))
        o->def_ratio = 1.f / o->def_ratio;
      break;
    case 't': {
      float time;
      read_num(o, 0, &time);
      if (time < 0.f) {
        warning(o, "ignoring 't' with sub-zero time");
        break;
      }
      SET_I2FV(o->def_time_ms, time*1000.f);
      break; }
    default:
    /*UNKNOWN:*/
      o->nextc = c;
      return 1; /* let parse_level() take care of it */
    }
  }
  return 0;
}

static uchar parse_level(SGSParser *o, NodeScope *parentnd,
                         uchar linktype, char newscope);

static uchar parse_step(NodeScope *ns) {
  SGSParser *o = ns->o;
  SGSEventNode *e = ns->event;
  SGSOperatorNode *op = ns->operator;
  char c;
  leave_defaults(ns);
  enter_current_node(ns);
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'P':
      if (ns->ns_flags & NS_NESTED_SCOPE)
        goto UNKNOWN;
      if (testgetc('[', o->f)) {
        if (read_valit(o, 0, &e->valitpanning))
          e->voice_attr |= SGS_ATTR_VALITPANNING;
      } else if (read_num(o, 0, &e->panning)) {
        if (!e->valitpanning.type)
          e->voice_attr &= ~SGS_ATTR_VALITPANNING;
      }
      break;
    case '\\':
      if (read_waittime(ns)) {
        begin_node(ns, ns->operator, NL_REFER, 0);
      }
      break;
    case 'a':
      if (ns->linktype == NL_AMODS ||
          ns->linktype == NL_FMODS)
        goto UNKNOWN;
      if (testgetc('!', o->f)) {
        if (!testc('<', o->f)) {
          read_num(o, 0, &op->dynamp);
        }
        if (testgetc('<', o->f)) {
          if (op->amods.count) {
            op->operator_params |= SGS_ADJCS;
            SGS_node_list_clear(&op->amods);
          }
          parse_level(o, ns, NL_AMODS, SCOPE_NEST);
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, 0, &op->valitamp))
          op->attr |= SGS_ATTR_VALITAMP;
      } else {
        read_num(o, 0, &op->amp);
        op->operator_params |= SGS_AMP;
        if (!op->valitamp.type)
          op->attr &= ~SGS_ATTR_VALITAMP;
      }
      break;
    case 'f':
      if (testgetc('!', o->f)) {
        if (!testc('<', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (testgetc('<', o->f)) {
          if (op->fmods.count) {
            op->operator_params |= SGS_ADJCS;
            SGS_node_list_clear(&op->fmods);
          }
          parse_level(o, ns, NL_FMODS, SCOPE_NEST);
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->attr |= SGS_ATTR_VALITFREQ;
          op->attr &= ~SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, read_note, &op->freq)) {
        op->attr &= ~SGS_ATTR_FREQRATIO;
        op->operator_params |= SGS_FREQ;
        if (!op->valitfreq.type)
          op->attr &= ~(SGS_ATTR_VALITFREQ |
                        SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 'p':
      if (testgetc('!', o->f)) {
        if (testgetc('<', o->f)) {
          if (op->pmods.count) {
            op->operator_params |= SGS_ADJCS;
            SGS_node_list_clear(&op->pmods);
          }
          parse_level(o, ns, NL_PMODS, SCOPE_NEST);
        } else
          goto UNKNOWN;
      } else if (read_num(o, 0, &op->phase)) {
        op->phase = fmod(op->phase, 1.f);
        if (op->phase < 0.f)
          op->phase += 1.f;
        op->operator_params |= SGS_PHASE;
      }
      break;
    case 'r':
      if (!(ns->ns_flags & NS_NESTED_SCOPE))
        goto UNKNOWN;
      if (testgetc('!', o->f)) {
        if (!testc('<', o->f)) {
          if (read_num(o, 0, &op->dynfreq)) {
            op->dynfreq = 1.f / op->dynfreq;
            op->attr |= SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (testgetc('<', o->f)) {
          if (op->fmods.count) {
            op->operator_params |= SGS_ADJCS;
            SGS_node_list_clear(&op->fmods);
          }
          parse_level(o, ns, NL_FMODS, SCOPE_NEST);
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, read_note, &op->valitfreq)) {
          op->valitfreq.goal = 1.f / op->valitfreq.goal;
          op->attr |= SGS_ATTR_VALITFREQ |
                      SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, 0, &op->freq)) {
        op->freq = 1.f / op->freq;
        op->attr |= SGS_ATTR_FREQRATIO;
        op->operator_params |= SGS_FREQ;
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
      SET_I2FV(op->silence_ms, silence*1000.f);
      break; }
    case 't':
      if (testgetc('*', o->f)) {
        op->on_flags |= ON_TIME_DEFAULT; /* later fitted or kept to default */
        op->time_ms = o->def_time_ms;
      } else if (testgetc('i', o->f)) {
        if (!(ns->ns_flags & NS_NESTED_SCOPE)) {
          warning(o, "ignoring 'ti' (infinite time) for non-nested operator");
          break;
        }
        op->on_flags &= ~ON_TIME_DEFAULT;
        op->time_ms = SGS_TIME_INF;
      } else {
        float time;
        read_num(o, 0, &time);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time");
          break;
        }
        op->on_flags &= ~ON_TIME_DEFAULT;
        SET_I2FV(op->time_ms, time*1000.f);
      }
      op->operator_params |= SGS_TIME;
      break;
    case 'w': {
      int wave = read_wavetype(o);
      if (wave < 0)
        break;
      op->wave = wave;
      break; }
    default:
    UNKNOWN:
      o->nextc = c;
      return 1; /* let parse_level() take care of it */
    }
  }
  return 0;
}

enum {
  HANDLE_DEFER = 1<<1,
  DEFERRED_STEP = 1<<2,
  DEFERRED_SETTINGS = 1<<4
};
static uchar parse_level(SGSParser *o, NodeScope *parentns,
                         uchar linktype, char newscope) {
  char c;
  uchar endscope = 0;
  uchar flags = 0;
  LabelBuf label;
  NodeScope ns;
  begin_scope(o, &ns, parentns, linktype, newscope);
  ++o->calllevel;
  while ((c = read_char(o)) != EOF) {
    flags &= ~HANDLE_DEFER;
    switch (c) {
    case NEWLINE:
      ++o->line;
      if (ns.scope == SCOPE_TOP) {
        /*
         * On top level of script, each line has a new "subscope".
         */
        if (o->calllevel > 1)
          goto RETURN;
        flags = 0;
        leave_defaults(&ns);
        if (in_current_node(&ns)) {
          leave_current_node(&ns);
        }
        ns.first_operator = 0;
      }
      break;
    case ':':
      if (ns.set_label) {
        warning(o, "ignoring label assignment to label reference");
        label_next_node(&ns, NULL);
      }
      leave_defaults(&ns);
      leave_current_node(&ns);
      if (read_label(o, label, ':')) {
        SGSOperatorNode *ref = SGS_symtab_get(o->st, label);
        if (!ref)
          warning(o, "ignoring reference to undefined label");
        else {
          begin_node(&ns, ref, NL_REFER, 0);
          flags = parse_step(&ns) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (in_defaults(&ns) || !ns.event)
        goto INVALID;
      begin_node(&ns, ns.operator, NL_REFER, 1);
      flags = parse_step(&ns) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '<':
      if (parse_level(o, &ns, ns.linktype, '<'))
        goto RETURN;
      break;
    case '>':
      if (ns.scope != SCOPE_NEST) {
        warning(o, "closing '>' without opening '<'");
        break;
      }
      end_operator(&ns);
      endscope = 1;
      goto RETURN;
    case 'O': {
      int wave = read_wavetype(o);
      if (wave < 0)
        break;
      begin_node(&ns, 0, ns.linktype, 0);
      ns.operator->wave = wave;
      flags = parse_step(&ns) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      flags = parse_settings(&ns) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '\\':
      if (in_defaults(&ns) ||
          (ns.ns_flags & NS_NESTED_SCOPE && ns.event))
        goto INVALID;
      read_waittime(&ns);
      break;
    case '\'':
      if (ns.set_label) {
        warning(o, "ignoring label assignment to label assignment");
        break;
      }
      read_label(o, label, '\'');
      label_next_node(&ns, label);
      break;
    case '{':
      end_operator(&ns);
      if (parse_level(o, &ns, ns.linktype, SCOPE_BIND))
        goto RETURN;
      /*
       * Multiple-operator node will now be ready for parsing.
       */
      flags = parse_step(&ns) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '|':
      if (in_defaults(&ns) ||
          (ns.ns_flags & NS_NESTED_SCOPE && ns.event))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (!ns.event) {
        warning(o, "end of sequence before any parts given");
        break;
      }
      if (ns.group_from) {
        SGSEventNode *group_to = (ns.composite) ?
                                 ns.composite :
                                 ns.event;
        group_to->groupfrom = ns.group_from;
        ns.group_from = 0;
      }
      end_event(&ns);
      leave_current_node(&ns);
      break;
    case '}':
      if (ns.scope != SCOPE_BIND) {
        warning(o, "closing '}' without opening '{'");
        break;
      }
      endscope = 1;
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID);
      break;
    }
    /* Return to sub-parsing routines. */
    if (flags && !(flags & HANDLE_DEFER)) {
      uchar test = flags;
      flags = 0;
      if (test & DEFERRED_STEP) {
        if (parse_step(&ns))
          flags = HANDLE_DEFER | DEFERRED_STEP;
      } else if (test & DEFERRED_SETTINGS)
        if (parse_settings(&ns))
          flags = HANDLE_DEFER | DEFERRED_SETTINGS;
    }
  }
FINISH:
  if (newscope == SCOPE_NEST)
    warning(o, "end of file without closing '>'s");
  if (newscope == SCOPE_BIND)
    warning(o, "end of file without closing '}'s");
RETURN:
  end_scope(&ns);
  --o->calllevel;
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && ns.scope != newscope);
}

static void pp_passes(SGSParser *o);

/*
 * "Main" parsing function.
 */
void SGS_parse(SGSParser *o, FILE *f, const char *fn) {
  memset(o, 0, sizeof(SGSParser));
  o->f = f;
  o->fn = fn;
  o->st = SGS_create_symtab();
  o->line = 1;
  o->ampmult = 1.f; /* default until changed */
  o->def_time_ms = 1000; /* default until changed */
  o->def_freq = 444.f; /* default until changed */
  o->def_A4tuning = 444.f; /* default until changed */
  o->def_ratio = 1.f; /* default until changed */
  parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
  SGS_destroy_symtab(o->st);
  pp_passes(o);
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SGSEventNode *to) {
  SGSEventNode *e, *e_after = to->next;
  uint i;
  int wait = 0, waitcount = 0;
  for (e = to->groupfrom; e != e_after; ) {
    SGSOperatorNode **nl = SGS_NODE_LIST_GET(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGSOperatorNode *op = nl[i];
      if (e->next == e_after &&
          i == (uint)(e->operators.count - 1) &&
          op->on_flags & ON_TIME_DEFAULT) /* default for last node in group */
        op->on_flags &= ~ON_TIME_DEFAULT;
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
    SGSOperatorNode **nl = SGS_NODE_LIST_GET(&e->operators);
    for (i = 0; i < e->operators.count; ++i) {
      SGSOperatorNode *op = nl[i];
      if (op->on_flags & ON_TIME_DEFAULT) {
        op->on_flags &= ~ON_TIME_DEFAULT;
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

static int time_operator(SGSOperatorNode *op, void *arg) {
  SGSEventNode *e = op->event;
  if (op->valitfreq.time_ms == VI_TIME_DEFAULT)
    op->valitfreq.time_ms = op->time_ms;
  if (op->valitamp.time_ms == VI_TIME_DEFAULT)
    op->valitamp.time_ms = op->time_ms;
  if ((op->on_flags & (ON_TIME_DEFAULT | ON_OPERATOR_NESTED)) ==
                      (ON_TIME_DEFAULT | ON_OPERATOR_NESTED)) {
    op->on_flags &= ~ON_TIME_DEFAULT;
    op->time_ms = SGS_TIME_INF;
  }
  if (op->time_ms >= 0 && !(op->on_flags & ON_SILENCE_ADDED)) {
    op->time_ms += op->silence_ms;
    op->on_flags |= ON_SILENCE_ADDED;
  }
  if (e->en_flags & EN_ADD_WAIT_DURATION) {
    if (e->next)
      ((SGSEventNode*)e->next)->wait_ms += op->time_ms;
    e->en_flags &= ~EN_ADD_WAIT_DURATION;
  }
  return 0;
}

static void time_event(SGSEventNode *e) {
  /*
   * Fill in blank valit durations, handle silence as well as the case of
   * adding present event duration to wait time of next event.
   */
  if (e->valitpanning.time_ms == VI_TIME_DEFAULT)
    e->valitpanning.time_ms = 1000; /* FIXME! */
  SGS_node_list_rforeach(&e->operators, time_operator, 0);
  /*
   * Timing for composites - done before event list flattened.
   */
  if (e->composite) {
    SGSEventNode *ce = e->composite, *ce_prev = e;
    SGSOperatorNode *ce_op = SGS_NODE_LIST_GET(&ce->operators)[0],
                    *ce_op_prev = ce_op->on_prev,
                    *e_op = ce_op_prev;
    if (e_op->on_flags & ON_TIME_DEFAULT)
      e_op->on_flags &= ~ON_TIME_DEFAULT;
    for (;;) {
      ce->wait_ms += ce_op_prev->time_ms;
      if (ce_op->on_flags & ON_TIME_DEFAULT) {
        ce_op->on_flags &= ~ON_TIME_DEFAULT;
        ce_op->time_ms = (ce_op->on_flags & ON_OPERATOR_NESTED && !ce->next) ?
                         SGS_TIME_INF :
                         ce_op_prev->time_ms - ce_op_prev->silence_ms;
      }
      time_event(ce);
      if (ce_op->time_ms == SGS_TIME_INF)
        e_op->time_ms = SGS_TIME_INF;
      else if (e_op->time_ms != SGS_TIME_INF)
        e_op->time_ms += ce_op->time_ms +
                         (ce->wait_ms - ce_op_prev->time_ms);
      ce_op->operator_params &= ~SGS_TIME;
      ce_prev = ce;
      ce_op_prev = ce_op;
      ce = ce->next;
      if (!ce) break;
      ce_op = SGS_NODE_LIST_GET(&ce->operators)[0];
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
static void flatten_events(SGSEventNode *e) {
  SGSEventNode *ce = e->composite;
  SGSEventNode *se = e->next, *se_prev = e;
  int wait_ms = 0;
  int added_wait_ms = 0;
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
      SGSEventNode *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      SGSEventNode *se_next, *ce_next;
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
 */
static void pp_passes(SGSParser *o) {
  SGSEventNode *e;
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
