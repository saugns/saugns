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
	struct StringEntry *prev;
	int symbol_bucket;
	char string[1]; /* sized to actual length */
} StringEntry;

#define GET_STRING_ENTRY_SIZE(str_len) (\
	sizeof(StringEntry*) + \
	sizeof(int) + \
	str_len \
)

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
 * Return the hash of the given string \p str of lenght \p len.
 * \return the hash of \p str
 */
static uint hash_string(SGSSymtab *o, const char *str, uint len) {
	uint i;
	uint hash;
	/*
	 * Calculate hash.
	 */
	hash = 0;
	for (i = 0; i < len; ++i) {
		hash = 37 * hash + str[i];
	}
	hash &= (o->strtab_alloc - 1); /* strtab_alloc is a power of 2 */
	return hash;
}

/**
 * Increase the size of the hash table for the string pool.
 * \return the new allocation size, or -1 upon failure
 */
static int extend_strtab(SGSSymtab *o) {
	StringEntry **old_strtab = o->strtab;
	uint old_strtab_alloc = o->strtab_alloc;
	uint i;
	o->strtab_alloc = (o->strtab_alloc > 0) ? (o->strtab_alloc * 2) : 1024;
	o->strtab = calloc(o->strtab_alloc, sizeof(StringEntry*));
	if (o->strtab == NULL) return -1;
	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_strtab_alloc; ++i) {
		StringEntry *entry = old_strtab[i];
		while (entry) {
			StringEntry *prev_entry;
			uint len, hash;
			len = strlen(entry->string);
			hash = hash_string(o, entry->string, len);
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
 * The string \p str cannot be larger (in bytes) than the memory page size of
 * the system.
 * \return the unique copy of \p str, or NULL if allocation fails
 */
const char *SGS_symtab_intern_str(SGSSymtab *o, const char *str) {
	uint len;
	int hash;
	StringEntry *entry;
	if (o->strtab_count == (o->strtab_alloc / 2)) extend_strtab(o);
	len = strlen(str);
	hash = hash_string(o, str, len);
	entry = o->strtab[hash];
	for (;;) {
		if (entry == NULL) break; /* missing */
		if (!strcmp(entry->string, str)) return entry->string; /* found */
		entry = entry->prev;
	}
	/*
	 * Register string.
	 */
	entry = SGS_mempool_add(o->mempool, NULL, GET_STRING_ENTRY_SIZE(len));
	if (entry == NULL) return NULL;
	entry->prev = o->strtab[hash];
	o->strtab[hash] = entry;
	entry->symbol_bucket = -1; /* As-yet unused. */
	strcpy(entry->string, str);
	++o->strtab_count;
	return entry->string;
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
