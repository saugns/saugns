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

#define STRTAB_ALLOC_INITIAL 1024

#if SGS_HASHTAB_STATS
static uint32_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StrEntry {
	struct StrEntry *prev;
	void *symbol_data;
	uint32_t len;
	char str[1]; /* sized to actual length */
} StrEntry;

#define GET_STRING_ENTRY_SIZE(str_len) \
	(offsetof(StrEntry, str) + (str_len))

struct SGS_SymTab {
	SGS_MemPool *malc;
	StrEntry **strtab;
	uint32_t strtab_count;
	uint32_t strtab_alloc;
};

/**
 * Create instance.
 * \return instance or NULL on allocation failure
 */
SGS_SymTab *SGS_create_SymTab(void) {
	SGS_SymTab *o = calloc(1, sizeof(SGS_SymTab));
	if (o == NULL) return NULL;
	o->malc = SGS_create_MemPool(0);
	if (o->malc == NULL) {
		free(o);
		return NULL;
	}
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_SymTab(SGS_SymTab *o) {
#if SGS_HASHTAB_STATS
	printf("collision count: %d\n", collision_count);
#endif
	SGS_destroy_MemPool(o->malc);
	free(o->strtab);
}

/*
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

/*
 * Increase the size of the hash table for the string pool.
 * \return the new allocation size, or -1 upon failure
 */
static int32_t extend_strtab(SGS_SymTab *o) {
	StrEntry **old_strtab = o->strtab;
	uint32_t old_strtab_alloc = o->strtab_alloc;
	uint32_t i;
	o->strtab_alloc = (o->strtab_alloc > 0) ?
		(o->strtab_alloc << 1) :
		STRTAB_ALLOC_INITIAL;
	o->strtab = calloc(o->strtab_alloc, sizeof(StrEntry*));
	if (o->strtab == NULL)
		return -1;
	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_strtab_alloc; ++i) {
		StrEntry *entry = old_strtab[i];
		while (entry) {
			StrEntry *prev_entry;
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

/*
 * Get unique entry for string in symbol table, or NULL if missing.
 *
 * Initializes the string table if empty.
 *
 * \return StrEntry* or NULL
 */
static StrEntry *unique_entry(SGS_SymTab *o, const void *str, uint32_t len) {
	uint32_t hash;
	StrEntry *entry;
	if (o->strtab_count == (o->strtab_alloc / 2)) {
		if (extend_strtab(o) < 0) return NULL;
	}
	if (str == NULL || len == 0) return NULL;
	hash = hash_string(o, str, len);
	entry = o->strtab[hash];
	if (!entry) goto ADD_ENTRY; /* missing */
	for (;;) {
		if (entry->len == len &&
			!strcmp(entry->str, str)) return entry; /* found */
		entry = entry->prev;
		if (entry == NULL) break; /* missing */
	}
#if SGS_HASHTAB_STATS
	++collision_count;
#endif
ADD_ENTRY:
	entry = SGS_MemPool_alloc(o->malc, NULL, GET_STRING_ENTRY_SIZE(len + 1));
	if (entry == NULL) return NULL;
	entry->prev = o->strtab[hash];
	o->strtab[hash] = entry;
	entry->symbol_data = NULL;
	entry->len = len;
	memcpy(entry->str, str, len);
	entry->str[len] = '\0';
	++o->strtab_count;
	return entry;
}

/**
 * Place a string in the string pool of the symbol table, unless already
 * present. A copy of \p str unique for the symbol table is pointed to
 * by the return value.
 *
 * \return unique copy of \p str for symtab instance, or NULL on failure
 */
const void *SGS_SymTab_pool_str(SGS_SymTab *o, const void *str, uint32_t len) {
	StrEntry *entry = unique_entry(o, str, len);
	return (entry) ? entry->str : NULL;
}

/**
 * Return value associated with string.
 * \return value or NULL if none
 */
void *SGS_SymTab_get(SGS_SymTab *o, const void *key, uint32_t len) {
	StrEntry *entry;
	entry = unique_entry(o, key, len);
	if (!entry) return NULL;
	return entry->symbol_data;
}

/**
 * Set value associated with string.
 * \return previous value or NULL if none
 */
void *SGS_SymTab_set(SGS_SymTab *o, const void *key, uint32_t len,
		void *value) {
	StrEntry *entry;
	entry = unique_entry(o, key, len);
	if (!entry) return NULL;
	void *old_value = entry->symbol_data;
	entry->symbol_data = value;
	return old_value;
}
