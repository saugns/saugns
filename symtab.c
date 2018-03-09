/* sgensys: Symbol table module.
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

#include "symtab.h"
#include <string.h>
#include <stdlib.h>

/*
 * Placeholder until a better module is created.
 */

typedef struct SGS_SymNode {
  const char *key;
  void *value;
  struct SGS_SymNode *next;
} SGS_SymNode;

struct SGS_SymTab {
  SGS_SymNode *node;
};

SGS_SymTab* SGS_create_SymTab(void) {
  SGS_SymTab *o = calloc(1, sizeof(SGS_SymTab));
  return o;
}

void SGS_destroy_SymTab(SGS_SymTab *o) {
  SGS_SymNode *n = o->node;
  while (n) {
    SGS_SymNode *nn = n->next;
    free(n);
    n = nn;
  }
}

void* SGS_SymTab_get(SGS_SymTab *o, const char *key) {
  SGS_SymNode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static SGS_SymNode* SGS_SymNode_alloc(const char *key, void *value) {
  SGS_SymNode *o = malloc(sizeof(SGS_SymNode));
  o->key = key;
  o->value = value;
  o->next = 0;
  return o;
}

void* SGS_SymTab_set(SGS_SymTab *o, const char *key, void *value) {
  SGS_SymNode *n = o->node;
  if (!n) {
    o->node = SGS_SymNode_alloc(key, value);
    return 0;
  }
  for (;;) {
    if (!strcmp(n->key, key)) {
      void *oldvalue = n->value;
      n->value = value;
      return oldvalue;
    }
    if (!n->next)
      break;
    n = n->next;
  }
  n->next = SGS_SymNode_alloc(key, value);
  return 0;
}
