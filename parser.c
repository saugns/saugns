#include "mgensys.h"
#include "program.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SYMKEY_LEN 256
#define MODC_MAX 256

typedef struct MGSParser {
  FILE *f;
  const char *fn;
  MGSProgram *prg;
  MGSSymtab *st;
  char setsymkey[SYMKEY_LEN];
  uchar setsym;
  uint line;
  /* node state */
  uint nest;
  uint setdef, setnode;
  MGSProgramNode *last, *last_play;
  /* settings/ops */
  uchar n_mode;
  float n_amp;
  float n_delay;
  float n_time;
  float n_freq, n_ratio;
  MGSProgramNode *n_begin;
  uchar n_end;
  uchar n_time_delay;
  float n_add_delay; /* added to n_delay when n_delay set */
  float n_next_add_delay;
} MGSParser;

static void end_node(MGSParser *o) {
  MGSProgramNode *n = o->last;
  if (!n) return;
  if (n->ref && !n->free_mods) {
  }
}

static MGSProgramNode* make_node(MGSParser *o) {
  MGSProgram *p = o->prg;
  MGSProgramNode *n = calloc(1, sizeof(MGSProgramNode));
  if (!p->steps)
    p->steps = n;
  else {
    o->last->next = n;
    if (o->last->flag & MGS_FLAG_PLAY)
      o->last_play = o->last;
    end_node(o);
  }

  /* settings/ops */
  n->mode = o->n_mode;
  n->amp = o->n_amp;
  o->n_add_delay = o->n_next_add_delay;
  if (o->n_time_delay) {
    if (o->last_play)
      o->n_add_delay += o->last_play->time;
    o->n_time_delay = 0;
  }
  if (!o->n_begin)
    o->n_begin = n;
  else if (o->n_end) {
    double delay = 0.f;
    MGSProgramNode *step;
    for (step = o->n_begin; step != n; step = step->next) {
      if (step->flag & MGS_FLAG_PLAY) {
        if (delay < step->time)
          delay = step->time;
        delay -= step->next->delay;
      }
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

  n->id = p->stepc;
  ++p->stepc;

  if (o->setsym) {
    o->setsym = 0;
    MGSSymtab_set(o->st, o->setsymkey, n);
  }

  return n;
}

static double getnum_r(FILE *f, char *buf, uint len, uchar pri) {
  char *p = buf;
  uchar dot = 0;
  double num;
  char c;
  do {
    c = getc(f);
  } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
  if (c == '(') {
    return getnum_r(f, buf, len, 255);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = getc(f);
  }
  if (p == buf) {
    ungetc(c, f);
    return NAN;
  }
  *p = '\0';
  num = strtod(buf, 0);
  for (;;) {
    while (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      c = getc(f);
    switch (c) {
    case '(':
      num *= getnum_r(f, buf, len, 255);
      break;
    case ')':
      if (pri < 255)
        ungetc(c, f);
      return num;
      break;
    case '^':
      num = exp(log(num) * getnum_r(f, buf, len, 0));
      break;
    case '*':
      num *= getnum_r(f, buf, len, 1);
      break;
    case '/':
      num /= getnum_r(f, buf, len, 1);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += getnum_r(f, buf, len, 2);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= getnum_r(f, buf, len, 2);
      break;
    default:
      return num;
    }
    if (num != num) {
      ungetc(c, f);
      return num;
    }
    c = getc(f);
  }
}
static double getnum(FILE *f) {
  char buf[64];
  char *p = buf;
  uchar dot = 0;
  if ((*p = getc(f)) == '(')
    return getnum_r(f, buf, 64, 255);
  do {
    if ((*p >= '0' && *p <= '9') || (!dot && (dot = (*p == '.'))))
      ++p;
    else
      break;
  } while ((*p = getc(f)) && p < (buf+64));
  ungetc(*p, f);
  *p = '\0';
  return strtod(buf, 0);
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

static void warning(MGSParser *o, const char *s, char c) {
  char buf[4] = {'\'', c, '\'', 0};
  if (c == EOF) strcpy(buf, "EOF");
  printf("warning: %s [line %d, at %s] - %s\n", o->fn, o->line, buf, s);
}

static void parse_level(MGSParser *o, MGSProgramNode *node, MGSProgramNode *mods[MODC_MAX], uchar *modc, uchar modtype);

static MGSProgram* parse(FILE *f, const char *fn, MGSParser *o) {
  o->f = f;
  o->fn = fn;
  o->prg = calloc(1, sizeof(MGSProgram));
  o->st = MGSSymtab_create();
  o->setsym = 0;
  o->line = 1;
  o->nest = 0;
  o->setnode = o->setdef = 0;
  o->last = 0;
  o->n_mode = MGS_MODE_CENTER; /* default until changed */
  o->n_amp = 1.f; /* default until changed */
  o->n_delay = 0.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_ratio = 1.f; /* default until changed */
  o->n_begin = 0;
  o->n_time_delay = o->n_end = 0;
  o->n_next_add_delay = o->n_add_delay = 0.f;
  parse_level(o, 0, 0, 0, 0);
  MGSSymtab_destroy(o->st);
  return o->prg;
}

static void parse_level(MGSParser *o, MGSProgramNode *node, MGSProgramNode *mods[MODC_MAX], uchar *modc, uchar modtype) {
  char c;
  uint entrynest = o->nest;
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case '\n':
    EOL:
      if (!modc) {
        if (o->setdef > o->nest)
          o->setdef = (o->nest) ? (o->nest - 1) : 0;
        else if (o->setnode > o->nest)
          o->setnode = (o->nest) ? (o->nest - 1) : 0;
      }
      ++o->line;
      break;
    case '\t':
    case ' ':
      eatws(o->f);
      break;
    case '#':
      while ((c = getc(o->f)) != '\n' && c != EOF) ;
      goto EOL;
      break;
    case '/':
      if (o->setdef > o->setnode) goto INVALID;
      if (testgetc('t', o->f))
        o->n_time_delay = 1;
      else {
        o->n_time_delay = 0;
        o->n_next_add_delay = getnum(o->f);
      }
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '}':
      if (!modc)
        goto INVALID;
      //warning or change to recursion on '<'?
      //if (o->nest != entrynest)
      return;
    case '<':
      ++o->nest;
      break;
    case '>':
      if (!o->nest) {
        warning(o, "closing marker without opening '<'", c);
        break;
      }
      if (o->setdef > o->nest)
        o->setdef = (o->nest) ? (o->nest - 1) : 0;
      else if (o->setnode > o->nest)
        o->setnode = (o->nest) ? (o->nest - 1) : 0;
      --o->nest;
      break;
    case 'C':
      o->n_mode = MGS_MODE_CENTER;
      break;
    case 'E':
      node = make_node(o);
      node->type = MGS_TYPE_ENV;
      o->setnode = o->nest + 1;
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
      int wave;
      wave = strfind(o->f, simples) + MGS_WAVE_SIN;
      if (wave < MGS_WAVE_SIN) {
        warning(o, "invalid wave type follows W in file; sin, sqr, tri, saw available", c);
        break;
      }
      if (modc && *modc == 255) {
        warning(o, "ignoring 256th modulator for one node (max 255)", c);
        break;
      }
      node = make_node(o);
      node->type = MGS_TYPE_WAVE;
      node->wave = wave;
      if (modc)
        mods[(*modc)++] = node;
      else
        node->flag |= MGS_FLAG_PLAY;
      o->setnode = o->nest + 1;
      break; }
    case '|':
      if (!o->n_begin)
        warning(o, "end of sequence before any parts given", c);
      else
        o->n_end = 1;
      break;
    case '\\':
      if (o->setdef > o->setnode)
        o->n_delay = getnum(o->f);
      else
        node->delay = getnum(o->f) + o->n_add_delay;
      break;
    case '\'': {
      uint i = 0;
      if (o->setsym)
        warning(o, "ignoring label assignment to label assignment", c);
      for (;;) {
        c = getc(o->f);
        if (c == ' ' || c == '\t' || c == '\n') {
          if (i == 0)
            warning(o, "ignoring ' without symbol name", c);
          else {
            o->setsymkey[i] = '\0';
            o->setsym = 1;
          }
          break;
        }
        o->setsymkey[i++] = c;
      }
      break; }
    case ':': {
      uint i = 0;
      if (o->setsym)
        warning(o, "ignoring label assignment to label reference", c);
      for (;;) {
        c = getc(o->f);
        if (c == ' ' || c == '\t' || c == '\n') {
          if (i == 0)
            warning(o, "ignoring : without symbol name", c);
          else {
            if (modc && *modc == 255) {
              warning(o, "ignoring 256th modulator for one node (max 255)", c);
              break;
            }
            o->setsymkey[i] = '\0';
            MGSProgramNode *ref = MGSSymtab_get(o->st, o->setsymkey);
            if (!ref)
              warning(o, "ignoring reference to undefined label", c);
            else {
              o->setsym = 1; /* update */
              node = make_node(o);
              node->ref = ref;
              node->type = node->ref->type;
              node->wave = node->ref->wave;
              node->mode = node->ref->mode;
              node->amp = node->ref->amp;
              node->dynamp = node->ref->dynamp;
              node->freq = node->ref->freq;
              node->dynfreq = node->ref->dynfreq;
              node->flag |= MGS_FLAG_REFTIME;
              node->pmodc = node->ref->pmodc;
              node->pmods = node->ref->pmods;
              node->fmodc = node->ref->fmodc;
              node->fmods = node->ref->fmods;
              node->amodc = node->ref->amodc;
              node->amods = node->ref->amods;
              if (modc) {
                node->freq = o->n_ratio;
                node->flag |= MGS_FLAG_FREQRATIO;
                mods[(*modc)++] = node;
              } else
                node->flag |= MGS_FLAG_PLAY;
              o->setnode = o->nest + 1;
            }
          }
          break;
        }
        o->setsymkey[i++] = c;
      }
      break; }
    case 'a':
      if (o->setdef > o->setnode)
        o->n_amp = getnum(o->f);
      else if (o->setnode > 0) {
        if (modtype == MGS_AMODS ||
            modtype == MGS_FMODS)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            node->dynamp = getnum(o->f);
          }
          if (testgetc('{', o->f)) {
            MGSProgramNode *mods[MODC_MAX];
            uchar modc = 0;
            parse_level(o, node, mods, &modc, MGS_AMODS);
            if (node->amodc && node->free_mods & MGS_AMODS) {
              free(node->amods);
              node->amods = 0;
            }
            node->amodc = modc;
            if (modc) {
              uint size = modc * sizeof(MGSProgramNode*);
              node->free_mods |= MGS_AMODS;
              node->amods = malloc(size);
              memcpy(node->amods, mods, size);
            }
          }
        } else {
          node->amp = getnum(o->f);
        }
      } else
        goto INVALID;
      break;
    case 'f':
      if (o->setdef > o->setnode)
        o->n_freq = getnum(o->f);
      else if (o->setnode > 0) {
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            node->dynfreq = getnum(o->f);
            node->flag &= ~MGS_FLAG_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            MGSProgramNode *mods[MODC_MAX];
            uchar modc = 0;
            parse_level(o, node, mods, &modc, MGS_FMODS);
            if (node->fmodc && node->free_mods & MGS_FMODS) {
              free(node->fmods);
              node->fmods = 0;
            }
            node->fmodc = modc;
            if (modc) {
              uint size = modc * sizeof(MGSProgramNode*);
              node->free_mods |= MGS_FMODS;
              node->fmods = malloc(size);
              memcpy(node->fmods, mods, size);
            }
          }
        } else {
          node->freq = getnum(o->f);
          node->flag &= ~MGS_FLAG_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 'm': {
      MGSProgramNode *mods[MODC_MAX];
      uchar modc = 0;
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (node->pmodc && node->free_mods & MGS_PMODS) {
        free(node->pmods);
        node->pmods = 0;
        node->pmodc = 0;
      }
      /* require {} nesting syntax; keeps parsing simple */
      if (!testgetc('{', o->f)) {
        warning(o, "modulation parameter without {contents}", c);
        break;
      }
      parse_level(o, node, mods, &modc, MGS_PMODS);
      if (modc) {
        uint size = modc * sizeof(MGSProgramNode*);
        node->pmodc = modc;
        node->free_mods |= MGS_PMODS;
        node->pmods = malloc(size);
        memcpy(node->pmods, mods, size);
      }
      break; }
    case 'r':
      if (o->setdef > o->setnode)
        o->n_ratio = 1.f / getnum(o->f);
      else if (o->setnode > 0) {
        if (!modc)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            node->dynfreq = 1.f / getnum(o->f);
            node->flag |= MGS_FLAG_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            MGSProgramNode *mods[MODC_MAX];
            uchar modc = 0;
            parse_level(o, node, mods, &modc, MGS_FMODS);
            if (node->fmodc && node->free_mods & MGS_FMODS) {
              free(node->fmods);
              node->fmods = 0;
            }
            node->fmodc = modc;
            if (modc) {
              uint size = modc * sizeof(MGSProgramNode*);
              node->free_mods |= MGS_FMODS;
              node->fmods = malloc(size);
              memcpy(node->fmods, mods, size);
            }
          }
        } else {
          node->freq = 1.f / getnum(o->f);
          node->flag |= MGS_FLAG_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode)
        o->n_time = getnum(o->f);
      else if (o->setnode > 0) {
        node->time = getnum(o->f);
        node->flag &= ~MGS_FLAG_REFTIME;
      } else
        goto INVALID;
      break;
    default:
    INVALID:
      warning(o, "invalid character", c);
      break;
    }
  }
FINISH:
  if (o->nest)
    warning(o, "end of file without closing '>'s", c);
  end_node(o);
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
    if (n->free_mods & MGS_PMODS)
      free(n->pmods);
    if (n->free_mods & MGS_FMODS)
      free(n->fmods);
    if (n->free_mods & MGS_AMODS)
      free(n->amods);
    free(n);
    n = nn;
  }
}
