/* mgensys: Symbol table module.
 * Copyright (c) 2011, 2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mgensys.h"
#include "symtab.h"
#include <string.h>
#include <stdlib.h>

/* a plain linked list is sufficient at present */

typedef struct MGS_SymNode {
  char *key;
  void *value;
  struct MGS_SymNode *next;
} MGS_SymNode;

struct MGS_SymTab {
  MGS_SymNode *node;
};

MGS_SymTab* MGS_SymTab_create(void) {
  MGS_SymTab *o = calloc(1, sizeof(MGS_SymTab));
  return o;
}

void MGS_SymTab_destroy(MGS_SymTab *o) {
  MGS_SymNode *n = o->node;
  while (n) {
    MGS_SymNode *nn = n->next;
    free(n->key);
    free(n);
    n = nn;
  }
}

void* MGS_SymTab_get(MGS_SymTab *o, const char *key) {
  MGS_SymNode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static MGS_SymNode* MGS_SymNode_alloc(const char *key, void *value) {
  MGS_SymNode *o = calloc(1, sizeof(MGS_SymNode));
  int len = strlen(key);
  o->key = calloc(1, len + 1);
  strcpy(o->key, key);
  o->value = value;
  return o;
}

void* MGS_SymTab_set(MGS_SymTab *o, const char *key, void *value) {
  MGS_SymNode *n = o->node;
  if (!n) {
    o->node = MGS_SymNode_alloc(key, value);
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
  n->next = MGS_SymNode_alloc(key, value);
  return 0;
}
