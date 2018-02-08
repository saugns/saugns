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

struct SGSSymNode {
  const char *key;
  void *value;
  struct SGSSymNode *next;
};

struct SGSSymTab {
  struct SGSSymNode *node;
};

SGSSymTab_t SGS_create_symtab(void) {
  SGSSymTab_t o = calloc(1, sizeof(struct SGSSymTab));
  return o;
}

void SGS_destroy_symtab(SGSSymTab_t o) {
  struct SGSSymNode *n = o->node;
  while (n) {
    struct SGSSymNode *nn = n->next;
    free(n);
    n = nn;
  }
}

void* SGS_symtab_get(SGSSymTab_t o, const char *key) {
  struct SGSSymNode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static struct SGSSymNode* SGS_symnode_alloc(const char *key, void *value) {
  struct SGSSymNode *o = malloc(sizeof(struct SGSSymNode));
  o->key = key;
  o->value = value;
  o->next = 0;
  return o;
}

void* SGS_symtab_set(SGSSymTab_t o, const char *key, void *value) {
  struct SGSSymNode *n = o->node;
  if (!n) {
    o->node = SGS_symnode_alloc(key, value);
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
  n->next = SGS_symnode_alloc(key, value);
  return 0;
}
