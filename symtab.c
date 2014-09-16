/* Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "symtab.h"
#include <string.h>
#include <stdlib.h>

/* a plain linked list is sufficient at present */

typedef struct SGSSymnode {
  const char *key;
  void *value;
  struct SGSSymnode *next;
} SGSSymnode;

struct SGSSymtab {
  SGSSymnode *node;
};

SGSSymtab* SGS_symtab_create(void) {
  SGSSymtab *o = calloc(1, sizeof(SGSSymtab));
  return o;
}

void SGS_symtab_destroy(SGSSymtab *o) {
  SGSSymnode *n = o->node;
  while (n) {
    SGSSymnode *nn = n->next;
    free(n);
    n = nn;
  }
}

void* SGS_symtab_get(SGSSymtab *o, const char *key) {
  SGSSymnode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static SGSSymnode* SGS_symnode_alloc(const char *key, void *value) {
  SGSSymnode *o = malloc(sizeof(SGSSymnode));
  o->key = key;
  o->value = value;
  o->next = 0;
  return o;
}

void* SGS_symtab_set(SGSSymtab *o, const char *key, void *value) {
  SGSSymnode *n = o->node;
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
