/* sgensys symbol table module.
 * Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include "mempool.h"
#include <string.h>
#include <stdlib.h>

#define INITIAL_STRTAB_ALLOC 1024

static uint32_t collision_count = 0;
#include <stdio.h>

typedef struct StringEntry {
	struct StringEntry *prev;
	int32_t symbol_bucket;
	uint32_t len;
	char str[1]; /* sized to actual length */
} StringEntry;

#define GET_STRING_ENTRY_SIZE(str_len) \
	(offsetof(StringEntry, str) + (str_len))

/* a plain linked list is sufficient at present */

typedef struct SGSSymnode {
  const char *key;
  void *value;
  struct SGSSymnode *next;
} SGSSymnode;

struct SGSSymtab {
	SGSMemPool *mempool;
	StringEntry **strtab;
	uint32_t strtab_count;
	uint32_t strtab_alloc;
  SGSSymnode *node;
};

SGSSymtab* SGS_create_symtab(void) {
	SGSSymtab *o = calloc(1, sizeof(SGSSymtab));
	if (o == NULL) return NULL;
	o->mempool = SGS_create_mempool(0);
	if (o->mempool == NULL) {
		free(o);
		return NULL;
	}
	return o;
}

void SGS_destroy_symtab(SGSSymtab *o) {
  SGSSymnode *n = o->node;
  while (n) {
    SGSSymnode *nn = n->next;
    free(n);
    n = nn;
  }
  printf("collision count: %d\n", collision_count);
	SGS_destroy_mempool(o->mempool);
	free(o->strtab);
}

/**
 * Return the hash of the given string \p str of lenght \p len.
 * \return the hash of \p str
 */
static uint32_t hash_string(SGSSymtab *o, const char *str, uint32_t len) {
	uint32_t i, j;
	uint32_t hash;
	/*
	 * Calculate hash.
	 */
	hash = len;
	for (i = 0; i < len; ++i) {
		uint32_t c = str[i];
		hash = 37 * hash + c;
	}
	hash &= (o->strtab_alloc - 1); /* strtab_alloc is a power of 2 */
	return hash;
}

/**
 * Increase the size of the hash table for the string pool.
 * \return the new allocation size, or -1 upon failure
 */
static int32_t extend_strtab(SGSSymtab *o) {
	StringEntry **old_strtab = o->strtab;
	uint32_t old_strtab_alloc = o->strtab_alloc;
	uint32_t i;
	o->strtab_alloc = (o->strtab_alloc > 0) ?
		(o->strtab_alloc << 1) :
		INITIAL_STRTAB_ALLOC;
	o->strtab = calloc(o->strtab_alloc, sizeof(StringEntry*));
	if (o->strtab == NULL)
		return -1;
	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_strtab_alloc; ++i) {
		StringEntry *entry = old_strtab[i];
		while (entry) {
			StringEntry *prev_entry;
			uint32_t hash;
			hash = hash_string(o, entry->str, entry->len);
			/*
			 * Before adding the entry to the new table, set
			 * entry->prev to the previous (if any) entry with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_entry = entry->prev;
			entry->prev = o->strtab[hash];
			o->strtab[hash] = entry;
			entry = prev_entry;
		}
	}
	free(old_strtab);
	return o->strtab_alloc;
}

/**
 * Intern a string in the string pool of the symbol table. A unique copy of
 * \p str is added, unless already stored. In either case, the unique copy is
 * returned.
 *
 * \return the unique copy of \p str, or NULL if allocation fails
 */
const char *SGS_symtab_intern_str(SGSSymtab *o, const char *str, uint32_t len) {
	uint32_t hash;
	StringEntry *entry;
	if (str == NULL || len == 0) return NULL;
	if (o->strtab_count == (o->strtab_alloc / 2)) {
		if (extend_strtab(o) < 0) return NULL;
	}
	hash = hash_string(o, str, len);
	entry = o->strtab[hash];
	for (;;) {
		if (entry == NULL) break; /* missing */
		if (entry->len == len &&
			!strcmp(entry->str, str)) return entry->str; /* found */
		entry = entry->prev;
	}
	/*
	 * Register string.
	 */
	entry = SGS_mempool_alloc(o->mempool, GET_STRING_ENTRY_SIZE(len + 1));
	if (entry == NULL) return NULL;
	if (o->strtab[hash] != NULL) {
		++collision_count;
	}
	entry->prev = o->strtab[hash];
	o->strtab[hash] = entry;
	entry->symbol_bucket = -1; /* As-yet unused. */
	entry->len = len;
	memcpy(entry->str, str, len);
	entry->str[len] = '\0';
	++o->strtab_count;
	return entry->str;
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
