/* saugns: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2019 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include "../mempool.h"
#include <string.h>
#include <stdlib.h>

#define HASHTAB_ALLOC_INITIAL 1024

#if SAU_HASHTAB_STATS
static size_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct TabItem {
	struct TabItem *prev;
	void *data;
	size_t key_len;
	char key[1];
} TabItem;

typedef struct HashTab {
	TabItem **items;
	size_t count;
	size_t alloc;
} HashTab;

#define GET_TABITEM_SIZE(key_len) \
	(offsetof(TabItem, key) + (key_len))

static inline void fini_HashTab(HashTab *restrict o) {
	free(o->items);
}

/*
 * Return the hash of the given string \p key of lenght \p len.
 *
 * \return hash
 */
static size_t HashTab_hash_key(HashTab *restrict o,
		const char *restrict key, size_t len) {
	size_t i;
	size_t hash;
	/*
	 * Calculate DJB2 hash,
	 * varied by adding "len".
	 */
	hash = 5381 + (len * 33);
	for (i = 0; i < len; ++i) {
		size_t c = key[i];
		hash = ((hash << 5) + hash) ^ c;
	}
	hash &= (o->alloc - 1);
	return hash;
}

/*
 * Increase the size of the hash table.
 *
 * \return true, or false on allocation failure
 */
static bool HashTab_extend(HashTab *restrict o) {
	TabItem **items, **old_items = o->items;
	size_t alloc, old_alloc = o->alloc;
	size_t i;
	alloc = (old_alloc > 0) ?
		(old_alloc << 1) :
		HASHTAB_ALLOC_INITIAL;
	items = calloc(alloc, sizeof(TabItem*));
	if (!items)
		return false;
	o->alloc = alloc;
	o->items = items;

	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_alloc; ++i) {
		TabItem *item = old_items[i];
		while (item != NULL) {
			TabItem *prev_item;
			size_t hash;
			hash = HashTab_hash_key(o, item->key, item->key_len);
			/*
			 * Before adding the entry to the new table, set
			 * item->prev to the previous (if any) item with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_item = item->prev;
			item->prev = o->items[hash];
			o->items[hash] = item;
			item = prev_item;
		}
	}
	free(old_items);
	return true;
}

/*
 * Get unique item for key in hash table, adding it if missing.
 * If allocated, \p extra is added to the size of the node; use
 * 1 to add a NULL-byte for a string key.
 *
 * Initializes the hash table if empty.
 *
 * \return TabItem, or NULL on allocation failure
 */
static TabItem *HashTab_unique_item(HashTab *restrict o,
		SAU_MemPool *restrict memp,
		const void *restrict key, size_t len, size_t extra) {
	if (!key || len == 0)
		return NULL;
	if (o->count == (o->alloc / 2)) {
		if (!HashTab_extend(o))
			return NULL;
	}

	size_t hash = HashTab_hash_key(o, key, len);
	TabItem *item = o->items[hash];
	while (item != NULL) {
		if (item->key_len == len &&
			!memcmp(item->key, key, len)) return item;
		item = item->prev;
#if SAU_HASHTAB_STATS
		++collision_count;
#endif
	}
	item = SAU_MemPool_alloc(memp, GET_TABITEM_SIZE(len + extra));
	if (!item)
		return NULL;
	item->prev = o->items[hash];
	o->items[hash] = item;
	item->key_len = len;
	memcpy(item->key, key, len);
	++o->count;
	return item;
}

struct SAU_SymTab {
	SAU_MemPool *memp;
	HashTab strtab;
};

/**
 * Create instance.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_SymTab *SAU_create_SymTab(void) {
	SAU_SymTab *o = calloc(1, sizeof(SAU_SymTab));
	if (!o)
		return NULL;
	o->memp = SAU_create_MemPool(0);
	if (!o->memp) {
		free(o);
		return NULL;
	}
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_SymTab(SAU_SymTab *restrict o) {
	if (!o)
		return;
#if SAU_HASHTAB_STATS
	printf("collision count: %zd\n", collision_count);
#endif
	SAU_destroy_MemPool(o->memp);
	fini_HashTab(&o->strtab);
}

/**
 * Add \p str to the string pool of the symbol table, unless already
 * present. Return the copy of \p str unique to the symbol table.
 *
 * \return unique copy of \p str for instance, or NULL on allocation failure
 */
const void *SAU_SymTab_pool_str(SAU_SymTab *restrict o,
		const void *restrict str, size_t len) {
	TabItem *item = HashTab_unique_item(&o->strtab, o->memp, str, len, 1);
	return (item != NULL) ? item->key : NULL;
}

/**
 * Add the first \p n strings from \p stra to the string pool of the
 * symbol table, except any already present. An array of pointers to
 * the unique string pool copies of all \p stra strings is allocated
 * and returned; it will be freed when the symbol table is destroyed.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return array of pointers to unique strings, or NULL on allocation failure
 */
const char **SAU_SymTab_pool_stra(SAU_SymTab *restrict o,
		const char *const*restrict stra,
		size_t n) {
	const char **res_stra;
	res_stra = SAU_MemPool_alloc(o->memp, sizeof(const char*) * n);
	if (!res_stra)
		return NULL;
	for (size_t i = 0; i < n; ++i) {
		const char *str = SAU_SymTab_pool_str(o,
				stra[i], strlen(stra[i]));
		if (!str)
			return NULL;
		res_stra[i] = str;
	}
	return res_stra;
}

/**
 * Return value associated with string.
 *
 * \return value, or NULL if none
 */
void *SAU_SymTab_get(SAU_SymTab *restrict o,
		const void *restrict key, size_t len) {
	TabItem *item = HashTab_unique_item(&o->strtab, o->memp, key, len, 1);
	if (!item)
		return NULL;
	return item->data;
}

/**
 * Set value associated with string.
 *
 * \return previous value, or NULL if none
 */
void *SAU_SymTab_set(SAU_SymTab *restrict o,
		const void *restrict key, size_t len, void *restrict value) {
	TabItem *item = HashTab_unique_item(&o->strtab, o->memp, key, len, 1);
	if (!item)
		return NULL;
	void *old_value = item->data;
	item->data = value;
	return old_value;
}
