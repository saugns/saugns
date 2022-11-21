/* mgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#include <string.h>
#include <stdlib.h>

#define STRTAB_ALLOC_INITIAL 1024

#ifndef MGS_SYMTAB_STATS
/*
 * Print symbol table statistics for testing?
 */
# define MGS_SYMTAB_STATS 0
#endif
#if MGS_SYMTAB_STATS
static size_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StrTab {
	mgsSymStr **sstra;
	size_t count;
	size_t alloc;
} StrTab;

static inline void fini_StrTab(StrTab *restrict o) {
	free(o->sstra);
}

/*
 * Return the hash of the given string \p key of lenght \p len.
 *
 * \return hash
 */
static size_t StrTab_hash_key(StrTab *restrict o,
		const char *restrict key, size_t len) {
	size_t i;
	size_t hash;
	/*
	 * Calculate DJB2 hash,
	 * varied by adding len.
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
static bool StrTab_upsize(StrTab *restrict o) {
	mgsSymStr **sstra, **old_sstra = o->sstra;
	size_t alloc, old_alloc = o->alloc;
	size_t i;
	alloc = (old_alloc > 0) ?
		(old_alloc << 1) :
		STRTAB_ALLOC_INITIAL;
	sstra = calloc(alloc, sizeof(mgsSymStr*));
	if (!sstra)
		return false;
	o->alloc = alloc;
	o->sstra = sstra;

	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_alloc; ++i) {
		mgsSymStr *node = old_sstra[i];
		while (node != NULL) {
			mgsSymStr *prev_node;
			size_t hash;
			hash = StrTab_hash_key(o, node->key, node->key_len);
			/*
			 * Before adding the entry to the new table, set
			 * node->prev to the previous (if any) node with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_node = node->prev;
			node->prev = o->sstra[hash];
			o->sstra[hash] = node;
			node = prev_node;
		}
	}
	free(old_sstra);
	return true;
}

/*
 * Get unique node for key in hash table, adding it if missing.
 * If allocated, \p extra is added to the size of the node; use
 * 1 to add a NULL-byte for a string key.
 *
 * Initializes the hash table if empty.
 *
 * \return mgsSymStr, or NULL on allocation failure
 */
static mgsSymStr *StrTab_unique_node(StrTab *restrict o,
		mgsMemPool *restrict memp,
		const void *restrict key, size_t len, size_t extra) {
	if (!key || len == 0)
		return NULL;
	if (o->count == (o->alloc / 2)) {
		if (!StrTab_upsize(o))
			return NULL;
	}

	size_t hash = StrTab_hash_key(o, key, len);
	mgsSymStr *sstr = o->sstra[hash];
	while (sstr != NULL) {
		if (sstr->key_len == len &&
			!memcmp(sstr->key, key, len)) return sstr;
		sstr = sstr->prev;
#if MGS_SYMTAB_STATS
		++collision_count;
#endif
	}
	sstr = mgs_mpalloc(memp, sizeof(mgsSymStr) + (len + extra));
	if (!sstr)
		return NULL;
	sstr->prev = o->sstra[hash];
	o->sstra[hash] = sstr;
	sstr->key_len = len;
	memcpy(sstr->key, key, len);
	++o->count;
	return sstr;
}

struct mgsSymTab {
	mgsMemPool *memp;
	StrTab strt;
};

static void fini_SymTab(mgsSymTab *restrict o) {
#if MGS_SYMTAB_STATS
	fprintf(stderr, "collision count: %zd\n", collision_count);
#endif
	fini_StrTab(&o->strt);
}

/**
 * Create instance. Requires \p mempool to be a valid instance.
 *
 * \return instance, or NULL on allocation failure
 */
mgsSymTab *mgs_create_SymTab(mgsMemPool *restrict mempool) {
	if (!mempool)
		return NULL;
	mgsSymTab *o = mgs_mpalloc(mempool, sizeof(mgsSymTab));
	if (!mgs_mpregdtor(mempool, (mgsDtor_f) fini_SymTab, o))
		return NULL;
	o->memp = mempool;
	return o;
}

/**
 * Get the unique node held for \p str in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique node for \p str, or NULL on allocation failure
 */
mgsSymStr *mgsSymTab_get_symstr(mgsSymTab *restrict o,
		const void *restrict str, size_t len) {
	return StrTab_unique_node(&o->strt, o->memp, str, len, 1);
}

/**
 * Add an item for the string \p symstr.
 *
 * \return item, or NULL if none
 */
mgsSymItem *mgsSymTab_add_item(mgsSymTab *restrict o,
		mgsSymStr *restrict symstr, uint32_t sym_type) {
	mgsSymItem *item = mgs_mpalloc(o->memp, sizeof(mgsSymItem));
	if (!item)
		return NULL;
	item->sym_type = sym_type;
	item->prev = symstr->item;
	item->sstr = symstr;
	symstr->item = item;
	return item;
}

/**
 * Look for an item for the string \p symstr matching \p sym_type.
 *
 * \return item, or NULL if none
 */
mgsSymItem *mgsSymTab_find_item(mgsSymTab *restrict o mgsMaybeUnused,
		mgsSymStr *restrict symstr, uint32_t sym_type) {
	mgsSymItem *item = symstr->item;
	while (item) {
		if (item->sym_type == sym_type)
			return item;
		item = item->prev;
	}
	return NULL;
}

/**
 * Add the first \p n strings from \p stra to the string pool of the
 * symbol table. For each, an item will be prepared according to the
 * \p sym_type (with the type used assumed to store ID data) and the
 * current string index from 0 to n will be set for MGS_SYM_DATA_ID.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return true, or false on allocation failure
 */
bool mgsSymTab_add_stra(mgsSymTab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t sym_type) {
	for (size_t i = 0; i < n; ++i) {
		mgsSymItem *item;
		mgsSymStr *s = mgsSymTab_get_symstr(o,
				stra[i], strlen(stra[i]));
		if (!s || !(item = mgsSymTab_add_item(o, s, sym_type)))
			return false;
		item->data_use = MGS_SYM_DATA_ID;
		item->data.id = i;
	}
	return true;
}
