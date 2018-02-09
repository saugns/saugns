/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2018 Joel K. Pettersson
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
#include "../mempool.h"
#include <string.h>
#include <stdlib.h>

#define INITIAL_STRTAB_ALLOC 1024

#if SGS_HASHTAB_STATS
static uint32_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StringEntry {
	struct StringEntry *prev;
	int32_t symbol_bucket;
	uint32_t len;
	char str[1]; /* sized to actual length */
} StringEntry;

#define GET_STRING_ENTRY_SIZE(str_len) \
	(offsetof(StringEntry, str) + (str_len))

typedef struct SymNode {
  const char *key;
  void *value;
  struct SymNode *next;
} SymNode;

struct SGS_SymTab {
	SGS_MemPool *mempool;
	StringEntry **strtab;
	uint32_t strtab_count;
	uint32_t strtab_alloc;
  SymNode *node;
};

SGS_SymTab* SGS_create_SymTab(void) {
	SGS_SymTab *o = calloc(1, sizeof(SGS_SymTab));
	if (o == NULL) return NULL;
	o->mempool = SGS_create_MemPool(0);
	if (o->mempool == NULL) {
		free(o);
		return NULL;
	}
	return o;
}

void SGS_destroy_SymTab(SGS_SymTab *o) {
  SymNode *n = o->node;
  while (n) {
    SymNode *nn = n->next;
    free(n);
    n = nn;
  }
#if SGS_HASHTAB_STATS
  printf("collision count: %d\n", collision_count);
#endif
	SGS_destroy_MemPool(o->mempool);
	free(o->strtab);
}

/**
 * Return the hash of the given string \p str of lenght \p len.
 * \return the hash of \p str
 */
static uint32_t hash_string(SGS_SymTab *o, const char *str, uint32_t len) {
	uint32_t i;
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
static int32_t extend_strtab(SGS_SymTab *o) {
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
 * Place a string in the string pool of the symbol table, unless already
 * present. A copy of \p str unique for the symbol table is pointed to
 * by the return value.
 *
 * \return unique copy of \p str for symtab instance, or NULL on failure
 */
const char *SGS_SymTab_pool_str(SGS_SymTab *o, const char *str, uint32_t len) {
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
	entry = SGS_MemPool_alloc(o->mempool, GET_STRING_ENTRY_SIZE(len + 1));
	if (entry == NULL) return NULL;
#if SGS_HASHTAB_STATS
	if (o->strtab[hash] != NULL) {
		++collision_count;
	}
#endif
	entry->prev = o->strtab[hash];
	o->strtab[hash] = entry;
	entry->symbol_bucket = -1; /* As-yet unused. */
	entry->len = len;
	memcpy(entry->str, str, len);
	entry->str[len] = '\0';
	++o->strtab_count;
	return entry->str;
}

void* SGS_SymTab_get(SGS_SymTab *o, const char *key) {
  SymNode *n = o->node;
  while (n) {
    if (!strcmp(n->key, key))
      return n->value;
    n = n->next;
  }
  return 0;
}

static SymNode* SymNode_alloc(const char *key, void *value) {
  SymNode *o = malloc(sizeof(SymNode));
  o->key = key;
  o->value = value;
  o->next = 0;
  return o;
}

void* SGS_SymTab_set(SGS_SymTab *o, const char *key, void *value) {
  SymNode *n = o->node;
  if (!n) {
    o->node = SymNode_alloc(key, value);
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
  n->next = SymNode_alloc(key, value);
  return 0;
}
