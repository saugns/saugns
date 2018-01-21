#include "mgensys.h"
#include "program.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSParser {
  FILE *f;
  const char *fn;
  uint line;
  /* node state */
  uint nest;
  uint setdef, setnode;
  MGSProgramNode *last;
  /* settings/ops */
  uchar n_mode;
  float n_amp;
  float n_delay;
  float n_time;
  float n_freq;
  MGSProgramNode *n_begin;
  uchar n_end;
  uchar n_time_delay;
  float n_add_delay; /* added to n_delay when n_delay set */
  float n_next_add_delay;
} MGSParser;

static MGSProgramNode* make_node(MGSParser *o, MGSProgram *p) {
  MGSProgramNode *n = calloc(1, sizeof(MGSProgramNode));
  if (!p->steps)
    p->steps = n;
  else
    o->last->next = n;

  /* settings/ops */
  n->mode = o->n_mode;
  n->amp = o->n_amp;
  o->n_add_delay = o->n_next_add_delay;
  if (o->n_time_delay) {
    if (o->last)
      o->n_add_delay += o->last->time;
    o->n_time_delay = 0;
  }
  if (!o->n_begin)
    o->n_begin = n;
  else if (o->n_end) {
    double delay = 0.f;
    MGSProgramNode *step;
    for (step = o->n_begin; step != n; step = step->next) {
      if (delay < step->time)
        delay = step->time;
      delay -= step->next->delay;
    }
    o->n_add_delay += delay;
    o->n_begin = n;
    o->n_end = 0;
  }
  o->n_next_add_delay = 0.f;
  n->delay = o->n_delay + o->n_add_delay;
  n->time = o->n_time;
  n->freq = o->n_freq;

  o->last = n;
  ++p->stepc;

  return n;
}

static double getnum(FILE *f) {
  char buf[64];
  char *p = buf;
  while (((*p = getc(f)) >= '0' && *p <= '9') || *p == '.') ++p;
  ungetc(*p, f);
  *p = '\0';
  return atof(buf);
}

static int strfind(FILE *f, const char *const*str) {
  int search, ret;
  uint i, len, pos, matchpos;
  char c, undo[256];
  uint strc;
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
        if (search == (int)i) {
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
  for (i = pos; i > matchpos; --i) ungetc(undo[i], f);
  return ret;
}

static void eatws(FILE *f) {
  char c;
  while ((c = getc(f)) == ' ' || c == '\t') ;
  ungetc(c, f);
}

static uchar testchar(FILE *f, char c) {
  char gc;
  if ((gc = getc(f)) == c) return 1;
  ungetc(gc, f);
  return 0;
}

static void warning(MGSParser *o, const char *s, char c) {
  char buf[4] = {'\'', c, '\'', 0};
  if (c == EOF) strcpy(buf, "EOF");
  printf("warning: %s [line %d, at %s] - %s\n", o->fn, o->line, buf, s);
}

static MGSProgram* parse(FILE *f, const char *fn, MGSParser *o) {
  char c;
  MGSProgram *program = calloc(1, sizeof(MGSProgram));
  MGSProgramNode *node = 0;
  o->f = f;
  o->fn = fn;
  o->line = 1;
  o->nest = 0;
  o->setnode = o->setdef = 0;
  o->last = 0;
  o->n_mode = MGS_MODE_CENTER; /* default until changed */
  o->n_amp = 1.f; /* default until changed */
  o->n_delay = 0.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_begin = 0;
  o->n_time_delay = o->n_end = 0;
  o->n_next_add_delay = o->n_add_delay = 0.f;
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case '\n':
      if (o->setdef > o->nest)
        o->setdef = (o->nest) ? (o->nest - 1) : 0;
      else if (o->setnode > o->nest)
        o->setnode = (o->nest) ? (o->nest - 1) : 0;
      ++o->line;
      break;
    case '\t':
    case ' ':
      eatws(o->f);
      break;
    case '#':
      while ((c = getc(o->f)) != '\n' && c != EOF) ;
      ++o->line;
      break;
    case '/':
      if (o->setdef > o->setnode) goto INVALID;
      if (testchar(o->f, 't'))
        o->n_time_delay = 1;
      else {
        o->n_time_delay = 0;
        o->n_next_add_delay = getnum(o->f);
      }
      break;
    case '<':
      ++o->nest;
      break;
    case '>':
      if (!o->nest)
        warning(o, "closing marker without opening '<'", c);
      else {
        if (o->setdef > o->nest)
          o->setdef = (o->nest) ? (o->nest - 1) : 0;
        else if (o->setnode > o->nest)
          o->setnode = (o->nest) ? (o->nest - 1) : 0;
        --o->nest;
      }
      break;
    case 'C':
      o->n_mode = MGS_MODE_CENTER;
      break;
    case 'E':
      if (!o->n_begin)
        warning(o, "end of sequence before any parts given", c);
      else
        o->n_end = 1;
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
      o->setdef = o->nest + 1;
      break;
    case 'W': {
      const char *simples[] = {
        "sin",
        "sqr",
        "tri",
        "saw",
        0
      };
      int wave = strfind(o->f, simples) + MGS_WAVE_SIN;
      if (wave < MGS_WAVE_SIN)
        warning(o, "invalid wave type follows W in file; sin, sqr, tri, saw available", c);
      else {
        node = make_node(o, program);
        node->wave = wave;
        ++program->componentc;
      }
      o->setnode = o->nest + 1;
      break; }
    case '\\':
      if (o->setdef > o->setnode)
        o->n_delay = getnum(o->f);
      else
        node->delay = getnum(o->f) + o->n_add_delay;
      break;
    case 'a':
      if (o->setdef > o->setnode)
        o->n_amp = getnum(o->f);
      else if (o->setnode > 0)
        node->amp = getnum(o->f);
      else
        goto INVALID;
      break;
    case 'f':
      if (o->setdef > o->setnode)
        o->n_freq = getnum(o->f);
      else if (o->setnode > 0)
        node->freq = getnum(o->f);
      else
        goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode)
        o->n_time = getnum(o->f);
      else if (o->setnode > 0)
        node->time = getnum(o->f);
      else
        goto INVALID;
      break;
    default:
    INVALID:
      warning(o, "ignoring invalid character", c);
      break;
    }
  }
  warning(o, "no terminating Q in file", c);
FINISH:
  return program;
}

MGSProgram* MGSProgram_create(const char *filename) {
  MGSProgram *o;
  MGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  o = parse(f, filename, &p);
  fclose(f);
  return o;
}

void MGSProgram_destroy(MGSProgram *o) {
  MGSProgramNode *n = o->steps;
  while (n) {
    MGSProgramNode *nn = n->next;
    free(n);
    n = nn;
  }
}
