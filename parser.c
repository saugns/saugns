#include "mgensys.h"
#include "program.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SYMKEY_LEN 256

typedef struct MGSParser {
  FILE *f;
  const char *fn;
  MGSProgram *prg;
  MGSSymtab *st;
  char setsymkey[SYMKEY_LEN];
  uchar setsym;
  uint line;
  uint reclevel;
  /* node state */
  uint level;
  uint setdef, setnode;
  uint nestedc;
  MGSProgramNode *nested; /* list added to end of top nodes at end of parsing */
  MGSProgramNode *last_top, *last_nested;
  MGSProgramNode *undo_last;
  /* settings/ops */
  uchar n_mode;
  float n_ampmult;
  float n_time;
  float n_freq, n_ratio;
} MGSParser;

/* things that need to be separate for each nested parse_level() go here */
typedef struct NodeData {
  MGSProgramNode *node; /* state for tentative node until end_node() */
  MGSProgramNodeChain *target;
  MGSProgramNode *last;
  /* timing/delay */
  MGSProgramNode *n_begin;
  uchar n_end;
  uchar n_time_delay;
  float n_add_delay; /* added to node's delay in end_node */
  float n_next_add_delay;
} NodeData;

static void end_node(MGSParser *o, NodeData *nd);

static void new_node(MGSParser *o, NodeData *nd, MGSProgramNodeChain *target, uchar type) {
  MGSProgram *p = o->prg;
  MGSProgramNode *n;
  end_node(o, nd);
  n = nd->node = calloc(1, sizeof(MGSProgramNode));
  nd->target = target;
  n->type = type;
  /* defaults */
  n->amp = 1.f;
  n->mode = o->n_mode;
  n->time = o->n_time;
  n->freq = o->n_freq;

  /* tentative linking */
  if (!nd->target) {
    if (!p->nodelist)
      p->nodelist = n;
    else
      o->last_top->next = n;
  } else {
    if (!o->nested)
      o->nested = n;
    else
      o->last_nested->next = n;

    if (!nd->target->chain)
      nd->target->chain = n;
    else
      nd->last->spec.nested.link = n;
  }

  /* prepare timing adjustment */
  nd->n_add_delay += nd->n_next_add_delay;
  if (nd->n_time_delay) {
    if (o->last_top)
      nd->n_add_delay += o->last_top->time;
    nd->n_time_delay = 0;
  }
  if (!nd->n_begin)
    nd->n_begin = n;
  else if (nd->n_end) {
    double delay = 0.f;
    MGSProgramNode *step;
    for (step = nd->n_begin; step != n; step = step->next) {
      if (delay < step->time)
        delay = step->time;
      delay -= step->next->delay;
    }
    nd->n_add_delay += delay;
    nd->n_begin = n;
    nd->n_end = 0;
  }
  nd->n_next_add_delay = 0.f;

  if (nd->target) {
    o->undo_last = o->last_nested;
    o->last_nested = n; /* can't wait due to recursion for nesting */
  }
}

static void end_node(MGSParser *o, NodeData *nd) {
  MGSProgram *p = o->prg;
  MGSProgramNode *n = nd->node;
  if (!n)
    return; /* nothing to do */
  nd->node = 0;
  if (n->type == MGS_TYPE_SETTOP ||
      n->type == MGS_TYPE_SETNESTED) {
    /* check what the set-node changes */
    MGSProgramNode *ref = n->spec.set.ref;
    if (n->time != ref->time)
      n->spec.set.values |= MGS_TIME;
    if (n->freq != ref->freq)
      n->spec.set.values |= MGS_FREQ;
    if (n->dynfreq != ref->dynfreq)
      n->spec.set.values |= MGS_DYNFREQ;
    if (n->phase != ref->phase)
      n->spec.set.values |= MGS_PHASE;
    if (n->amp != ref->amp)
      n->spec.set.values |= MGS_AMP;
    if (n->dynamp != ref->dynamp)
      n->spec.set.values |= MGS_DYNAMP;
    if (n->attr != ref->attr)
      n->spec.set.values |= MGS_ATTR;
    if (n->amod.chain != ref->amod.chain)
      n->spec.set.mods |= MGS_AMODS;
    if (n->fmod.chain != ref->fmod.chain)
      n->spec.set.mods |= MGS_FMODS;
    if (n->pmod.chain != ref->pmod.chain)
      n->spec.set.mods |= MGS_PMODS;

    if (!n->spec.set.values && !n->spec.set.mods) {
      /* remove no-operation set node */
      if (o->last_nested == n)
        o->last_nested = o->undo_last;
      if (nd->n_begin == n)
        nd->n_begin = 0;
      if (!nd->target) {
        if (p->nodelist == n)
          p->nodelist = 0;
        else
          o->last_top->next = 0;
      } else {
        if (o->nested == n)
          o->nested = 0;
        else
          o->last_nested->next = 0;

        if (nd->target->chain == n)
          nd->target->chain = 0;
        else
          nd->last->spec.nested.link = 0;
      }
      free(n);
      return;
    }
  }

  nd->last = n;
  if (!nd->target) {
    n->flag |= MGS_FLAG_EXEC;
    o->last_top = n;
    n->id = p->topc++;
  } else {
    ++nd->target->count;
    /*o->last_nested = n;*/ /* already done */
    n->id = o->nestedc++;
  }
  ++p->nodec;

  n->amp *= o->n_ampmult;
  n->delay += nd->n_add_delay;
  nd->n_add_delay = 0.f;

  if (o->setsym) {
    o->setsym = 0;
    MGSSymtab_set(o->st, o->setsymkey, n);
  }
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

static void parse_level(MGSParser *o, MGSProgramNodeChain *chain, uchar modtype);

static MGSProgram* parse(FILE *f, const char *fn, MGSParser *o) {
  memset(o, 0, sizeof(MGSParser));
  o->f = f;
  o->fn = fn;
  o->prg = calloc(1, sizeof(MGSProgram));
  o->st = MGSSymtab_create();
  o->line = 1;
  o->n_mode = MGS_MODE_CENTER; /* default until changed */
  o->n_ampmult = 1.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_ratio = 1.f; /* default until changed */
  parse_level(o, 0, 0);
  MGSSymtab_destroy(o->st);
  /* concatenate linked lists */
  if (o->last_top)
    o->last_top->next = o->nested;
  return o->prg;
}

static void parse_level(MGSParser *o, MGSProgramNodeChain *chain, uchar modtype) {
  char c;
  NodeData nd;
  uint entrylevel = o->level;
  ++o->reclevel;
  memset(&nd, 0, sizeof(NodeData));
  if (chain) {
    chain->count = 0;
    chain->chain = 0;
  }
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case '\n':
    EOL:
      if (!chain) {
        if (o->setdef > o->level)
          o->setdef = (o->level) ? (o->level - 1) : 0;
        else if (o->setnode > o->level)
          o->setnode = (o->level) ? (o->level - 1) : 0;
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
        nd.n_time_delay = 1;
      else {
        nd.n_time_delay = 0;
        nd.n_next_add_delay += getnum(o->f);
      }
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '}':
      if (!chain)
        goto INVALID;
      if (o->level != entrylevel) {
        o->level = entrylevel;
        warning(o, "closing '}' before closing '>'s", c);
      }
      goto RETURN;
    case '<':
      ++o->level;
      break;
    case '>':
      if (!o->level) {
        warning(o, "closing '>' without opening '<'", c);
        break;
      }
      if (o->setdef > o->level)
        o->setdef = (o->level) ? (o->level - 1) : 0;
      else if (o->setnode > o->level)
        o->setnode = (o->level) ? (o->level - 1) : 0;
      --o->level;
      break;
    case 'C':
      o->n_mode = MGS_MODE_CENTER;
      break;
    case 'E':
      new_node(o, &nd, 0, MGS_TYPE_ENV);
      o->setnode = o->level + 1;
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
      o->setdef = o->level + 1;
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
      new_node(o, &nd, chain, MGS_TYPE_TOP);
      nd.node->wave = wave;
      o->setnode = o->level + 1;
      break; }
    case '|':
      end_node(o, &nd);
      if (!nd.n_begin)
        warning(o, "end of sequence before any parts given", c);
      else
        nd.n_end = 1;
      break;
    case '\\':
      if (o->setdef > o->setnode)
        goto INVALID;
      else
        nd.node->delay += getnum(o->f);
      break;
    case '\'': {
      uint i = 0;
      end_node(o, &nd);
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
      end_node(o, &nd);
      if (o->setsym)
        warning(o, "ignoring label assignment to label reference", c);
      for (;;) {
        c = getc(o->f);
        if (c == ' ' || c == '\t' || c == '\n') {
          if (i == 0)
            warning(o, "ignoring : without symbol name", c);
          else {
            o->setsymkey[i] = '\0';
            MGSProgramNode *ref = MGSSymtab_get(o->st, o->setsymkey);
            if (!ref)
              warning(o, "ignoring reference to undefined label", c);
            else {
              uint type;
              switch (ref->type) {
              case MGS_TYPE_TOP:
              case MGS_TYPE_SETTOP:
                type = MGS_TYPE_SETTOP;
                break;
              case MGS_TYPE_NESTED:
              case MGS_TYPE_SETNESTED:
                type = MGS_TYPE_SETNESTED;
                break;
              default:
                type = 0; /* silence warning */
              }
              o->setsym = 1; /* update */
              new_node(o, &nd, chain, type);
              nd.node->spec.set.ref = ref;
              nd.node->wave = ref->wave;
              nd.node->mode = ref->mode;
              nd.node->amp = ref->amp;
              nd.node->dynamp = ref->dynamp;
              nd.node->freq = ref->freq;
              nd.node->dynfreq = ref->dynfreq;
              nd.node->attr = ref->attr;
              nd.node->pmod = ref->pmod;
              nd.node->fmod = ref->fmod;
              nd.node->amod = ref->amod;
              o->setnode = o->level + 1;
            }
          }
          break;
        }
        o->setsymkey[i++] = c;
      }
      break; }
    case 'a':
      if (o->setdef > o->setnode)
        o->n_ampmult = getnum(o->f);
      else if (o->setnode > 0) {
        if (modtype == MGS_AMODS ||
            modtype == MGS_FMODS)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.node->dynamp = getnum(o->f);
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->amod, MGS_AMODS);
          }
        } else {
          nd.node->amp = getnum(o->f);
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
            nd.node->dynfreq = getnum(o->f);
            nd.node->attr &= ~MGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->fmod, MGS_FMODS);
          }
        } else {
          nd.node->freq = getnum(o->f);
          nd.node->attr &= ~MGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 'p': {
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (testgetc('!', o->f)) {
        if (o->setdef > o->setnode || o->setnode <= 0)
          goto INVALID;
        if (testgetc('{', o->f)) {
          parse_level(o, &nd.node->pmod, MGS_PMODS);
        }
      } else {
        nd.node->phase = fmod(getnum(o->f), 1.f);
        if (nd.node->phase < 0.f)
          nd.node->phase += 1.f;
      }
      break; }
    case 'r':
      if (o->setdef > o->setnode)
        o->n_ratio = 1.f / getnum(o->f);
      else if (o->setnode > 0) {
        if (!chain)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.node->dynfreq = 1.f / getnum(o->f);
            nd.node->attr |= MGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->fmod, MGS_FMODS);
          }
        } else {
          nd.node->freq = 1.f / getnum(o->f);
          nd.node->attr |= MGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode)
        o->n_time = getnum(o->f);
      else if (o->setnode > 0) {
        nd.node->time = getnum(o->f);
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
  if (o->level)
    warning(o, "end of file without closing '>'s", c);
  if (o->reclevel > 1)
    warning(o, "end of file without closing '}'s", c);
RETURN:
  end_node(o, &nd);
  --o->reclevel;
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
  MGSProgramNode *n = o->nodelist;
  while (n) {
    MGSProgramNode *nn = n->next;
    free(n);
    n = nn;
  }
}
