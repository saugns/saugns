/* mgensys: Symbol table module (individually licensed)
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

typedef struct MGSSymnode {
  char *key;
  void *value;
  struct MGSSymnode *next;
} MGSSymnode;

struct MGSSymtab {
  MGSSymnode *node;
};

MGSSymtab* MGSSymtab_create(void) {
  MGSSymtab *o = calloc(1, sizeof(MGSSymtab));
  return o;
}

void MGSSymtab_destroy(MGSSymtab *o) {
  MGSSymnode *n = o->node;
  while (n) {
    MGSSymnode *nn = n->next;
    free(n->key);
    free(n);
    n = nn;
  }
}

void* MGSSymtab_get(MGSSymtab *o, const char *key) {
  MGSSymnode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static MGSSymnode* MGSSymnode_alloc(const char *key, void *value) {
  MGSSymnode *o = malloc(sizeof(MGSSymnode));
  int len = strlen(key);
  o->key = malloc(len);
  strcpy(o->key, key);
  o->value = value;
  o->next = 0;
  return o;
}

void* MGSSymtab_set(MGSSymtab *o, const char *key, void *value) {
  MGSSymnode *n = o->node;
  if (!n) {
    o->node = MGSSymnode_alloc(key, value);
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
  n->next = MGSSymnode_alloc(key, value);
  return 0;
}
