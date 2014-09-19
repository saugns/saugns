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
#include "mempool.h"
#include <string.h>
#include <stdlib.h>

typedef struct StringEntry {
	int symbol_bucket;
	char string[1]; /* sized to actual length */
} StringEntry;

#define GET_STRING_ENTRY_SIZE(str_len) (sizeof(StringEntry) + str_len)

/* a plain linked list is sufficient at present */

typedef struct SGSSymnode {
  const char *key;
  void *value;
  struct SGSSymnode *next;
} SGSSymnode;

struct SGSSymtab {
	SGSMemPool *mempool;
	StringEntry **strtab;
	uint strtab_count;
	uint strtab_alloc;
  SGSSymnode *node;
};

SGSSymtab* SGS_create_symtab(void) {
	SGSSymtab *o = calloc(1, sizeof(SGSSymtab));
	if (o == NULL) return NULL;
	o->mempool = SGS_create_mempool();
	if (o->mempool == NULL) {
		free(o);
		return NULL;
	}
	/*
	 * Allocate hash table for string pool.
	 */
	o->strtab_alloc = 1403;
	o->strtab = calloc(o->strtab_alloc, sizeof(StringEntry*));

	return o;
}

void SGS_destroy_symtab(SGSSymtab *o) {
  SGSSymnode *n = o->node;
  while (n) {
    SGSSymnode *nn = n->next;
    free(n);
    n = nn;
  }
  SGS_destroy_mempool(o->mempool);
  free(o->strtab);
}

/**
 * Increase the size of the hash table for the string pool.
 * \return the new allocation size, or -1 upon failure
 */
//static int extend_strtab(SGSSymtab *o) {
//	StringEntry **new_strtab;
//	uint new_alloc;
//	uint i;
//	/*
//	 * Calculate size.
//	 */
//	if (o->strtab_alloc == 0) {
//	}
//
//}

static int get_string_hash(SGSSymtab *o, const char *str, uint len) {
	uint i;
	int hash;
	//if (o->strtab_alloc == 0) // XXX: alloc goes here if dynamic size
	/*
	 * Calculate hash.
	 */
	hash = 0;
	for (i = 0; i < len; ++i) {
		hash = 4 * hash + str[i];
	}
	hash &= ~(1<<31);
	hash %= o->strtab_alloc;
	/*
	 * Collision detection.
	 */
	i = 0;
	for (;;) {
		if (o->strtab[hash] == NULL) break;
		if (!strcmp(o->strtab[hash]->string, str)) break;
		hash += 2 * ++i - 1;
		hash &= ~(1<<31);
		hash %= o->strtab_alloc;
	}
	return hash;
}

#include <stdio.h>

/**
 * Add a string to the string pool of the symbol table, returning the id
 * of the string. The id is unique for each distinct string and symbol table
 * instance. If the string has already been added, the id of the
 * pre-existing entry will be returned.
 *
 * The string cannot be larger (in bytes) than the memory page size of the
 * system.
 * \return the id of the given string, or -1 if allocation fails
 */
int SGS_symtab_register_str(SGSSymtab *o, const char *str) {
	uint len;
	int hash;
	StringEntry *entry;
	len = strlen(str);
	//if (o->strtab_count == o->strtab_alloc - 4) {
	//	puts("hash table is filled");
	//	fflush(stdout);
	//	return -1;
	//}
	hash = get_string_hash(o, str, len);
	if (o->strtab[hash] != NULL) return hash;
	/*
	 * Register string.
	 */
	entry = SGS_mempool_add(o->mempool, NULL, GET_STRING_ENTRY_SIZE(len));
	if (entry == NULL) return -1;
	entry->symbol_bucket = -1; /* As-yet unused. */
	strcpy(entry->string, str);
	o->strtab[hash] = entry;
	++o->strtab_count;
	return hash;
}

/**
 * Lookup the string for a given id within the string pool of the symbol table,
 * returning the string if registered. The id is unique for each distinct
 * string and symbol table instance. If the id does not correspond to a string,
 * NULL is returned.
 * \return the string for the given id, or NULL if none.
 */
const char *SGS_symtab_lookup_str(SGSSymtab *o, int id) {
	StringEntry *entry;
	if (id < 0 || id >= (int)o->strtab_alloc) return NULL;
	entry = o->strtab[id];
	if (entry == NULL) return NULL;
	return entry->string; // FIXME: id numbers inherently broken design
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

/* EOF */
