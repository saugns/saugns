#include "mgensys.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSParser {
  FILE *f;
  const char *fn;
} MGSParser;

static MGSProgramNode* MGSProgram_add_node(MGSProgram *o, uchar end) {
  MGSProgramNode *n = calloc(1, sizeof(MGSProgramNode));
  if (!o->steps) {
    o->steps = n;
    n->pfirst = n;
  } else if (end) {
    o->last->pfirst->snext = n;
    n->pfirst = n;
  } else {
    o->last->pnext = n;
    n->pfirst = o->last->pfirst;
  }
  o->last = n;
  return n;
}

static double getnum(FILE *f) {
  char buf[64];
  char *p = buf;
  while ((*p = getc(f)) >= '0' && *p <= '9' || *p == '.') ++p;
  ungetc(*p, f);
  *p = '\0';
  return atof(buf);
}

static int strfind(FILE *f, const char *const*str) {
  int search, ret;
  uint i, pos, matchpos;
  char c, undo[256];
  uint strc;
  const char **s;

  for (strc = 0; str[strc]; ++strc) ;
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
        if (search == i) {
          ret = i;
          matchpos = pos-1;
        }
      } else if (c != s[i][pos]) {
        s[i] = 0;
        search = -1;
      } else
        search = i;
    }
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

static MGSProgram* parse(MGSParser *o) {
  char c;
  uint line = 1;
  MGSProgram *program = calloc(1, sizeof(MGSProgram));
  MGSProgramNode *node;
  uchar setdef = 0;
  uchar end = 0;
  uchar mode = MGS_MODE_CENTER; /* default until changed */
  float amp = 1.f; /* default until changed */
  float time = 1.f; /* default until changed */
  float freq = 100.f; /* default until changed */
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case ' ':
    case '\t':
      eatws(o->f);
      break;
    case '#':
      while ((c = getc(o->f)) != '\n' && c != EOF) ;
      ++line;
      break;
    case 'C':
      mode = MGS_MODE_CENTER;
      break;
    case 'D':
      setdef = 1;
      break;
    case 'E':
      if (!node)
        printf("warning: %s - E (end of step) before any parts given", o->fn);
      else
        end = 1;
      break;
    case 'L':
      mode = MGS_MODE_LEFT;
      break;
    case 'R':
      mode = MGS_MODE_RIGHT;
      break;
    case 'Q':
      goto FINISH;
    case 'S': {
      const char *simples[] = {
        "sin",
        "sqr",
        "tri",
        "saw",
        0
      };
      int type = strfind(o->f, simples) + MGS_TYPE_SIN;
      if (type < MGS_TYPE_SIN)
        printf("warning: %s - invalid sequence follows S in file; sin, sqr, tri, saw available\n", o->fn);
      else {
        node = MGSProgram_add_node(program, end);
        node->type = type;
        node->mode = mode;
        node->amp = amp;
        node->time = time;
        node->freq = freq;
        ++program->componentc;
      }
      end = 0;
      setdef = 0;
      break; }
    case 'W':
      node = MGSProgram_add_node(program, 1);
      node->type = MGS_TYPE_WAIT;
      node->time = time;
      end = 1;
      setdef = 0;
      break;
    case 'a':
      if (setdef)
        amp = getnum(o->f);
      else if (node->type == MGS_TYPE_WAIT)
        printf("warning: %s - W (wait) does not have any amplitude parameter\n", o->fn);
      else
        node->amp = getnum(o->f);
      break;
    case 'f':
      if (setdef)
        freq = getnum(o->f);
      else if (node->type == MGS_TYPE_WAIT)
        printf("warning: %s - W (wait) does not have any frequency parameter\n", o->fn);
      else
        node->freq = getnum(o->f);
      break;
    case 't':
      if (setdef)
        time = getnum(o->f);
      else
        node->time = getnum(o->f);
      break;
    case '\n':
      ++line;
      break;
    default:
      printf("warning: %s - ignoring invalid character '%c' on line %d\n",
             o->fn, c, line);
      break;
    }
  }
  printf("warning: %s - no terminating Q in file.\n", o->fn);
FINISH:
  program->components = calloc(program->componentc, sizeof(MGSProgramComponent));
  return program;
}

MGSProgram* MGSProgram_create(const char *filename) {
  MGSProgram *o;
  MGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  p.f = f;
  p.fn = filename;
  o = parse(&p);
  fclose(p.f);
  return o;
}

void MGSProgram_destroy(MGSProgram *o) {
}
