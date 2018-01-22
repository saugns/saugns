#include "sgensys.h"
#include "symtab.h"
#include <string.h>
#include <stdlib.h>

/* a plain linked list is sufficient at present */

typedef struct SGSSymnode {
  char *key;
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
    free(n->key);
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

static SGSSymnode* SGSSymnode_alloc(const char *key, void *value) {
  SGSSymnode *o = malloc(sizeof(SGSSymnode));
  int len = strlen(key);
  o->key = malloc(len);
  strcpy(o->key, key);
  o->value = value;
  o->next = 0;
  return o;
}

void* SGS_symtab_set(SGSSymtab *o, const char *key, void *value) {
  SGSSymnode *n = o->node;
  if (!n) {
    o->node = SGSSymnode_alloc(key, value);
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
  n->next = SGSSymnode_alloc(key, value);
  return 0;
}
