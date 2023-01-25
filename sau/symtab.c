/* SAU library: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2022 Joel K. Pettersson
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

#include <sau/symtab.h>
#include <string.h>
#include <stdlib.h>

#define STRTAB_ALLOC_INITIAL 1024

#ifndef SAU_SYMTAB_STATS
/*
 * Print symbol table statistics for testing?
 */
# define SAU_SYMTAB_STATS 0
#endif
#if SAU_SYMTAB_STATS
static size_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StrTab {
	sauSymstr **sstra;
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
	sauSymstr **sstra, **old_sstra = o->sstra;
	size_t alloc, old_alloc = o->alloc;
	size_t i;
	alloc = (old_alloc > 0) ?
		(old_alloc << 1) :
		STRTAB_ALLOC_INITIAL;
	sstra = calloc(alloc, sizeof(sauSymstr*));
	if (!sstra)
		return false;
	o->alloc = alloc;
	o->sstra = sstra;

	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_alloc; ++i) {
		sauSymstr *node = old_sstra[i];
		while (node != NULL) {
			sauSymstr *prev_node;
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
 * \return sauSymstr, or NULL on allocation failure
 */
static sauSymstr *StrTab_unique_node(StrTab *restrict o,
		sauMempool *restrict memp,
		const void *restrict key, size_t len, size_t extra) {
	if (!key || len == 0)
		return NULL;
	if (o->count == (o->alloc / 2)) {
		if (!StrTab_upsize(o))
			return NULL;
	}

	size_t hash = StrTab_hash_key(o, key, len);
	sauSymstr *sstr = o->sstra[hash];
	while (sstr != NULL) {
		if (sstr->key_len == len &&
			!memcmp(sstr->key, key, len)) return sstr;
		sstr = sstr->prev;
#if SAU_SYMTAB_STATS
		++collision_count;
#endif
	}
	sstr = sau_mpalloc(memp, sizeof(sauSymstr) + (len + extra));
	if (!sstr)
		return NULL;
	sstr->prev = o->sstra[hash];
	o->sstra[hash] = sstr;
	sstr->key_len = len;
	memcpy(sstr->key, key, len);
	++o->count;
	return sstr;
}

struct sauSymtab {
	sauMempool *memp;
	StrTab strt;
};

static void fini_Symtab(sauSymtab *restrict o) {
#if SAU_SYMTAB_STATS
	fprintf(stderr, "collision count: %zd\n", collision_count);
#endif
	fini_StrTab(&o->strt);
}

/**
 * Create instance. Requires \p mempool to be a valid instance.
 *
 * \return instance, or NULL on allocation failure
 */
sauSymtab *sau_create_Symtab(sauMempool *restrict mempool) {
	if (!mempool)
		return NULL;
	sauSymtab *o = sau_mpalloc(mempool, sizeof(sauSymtab));
	if (!sau_mpregdtor(mempool, (sauDtor_f) fini_Symtab, o))
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
sauSymstr *sauSymtab_get_symstr(sauSymtab *restrict o,
		const void *restrict str, size_t len) {
	return StrTab_unique_node(&o->strt, o->memp, str, len, 1);
}

/**
 * Add an item for the string \p symstr.
 *
 * \return item, or NULL if none
 */
sauSymitem *sauSymtab_add_item(sauSymtab *restrict o,
		sauSymstr *restrict symstr, uint32_t sym_type) {
	sauSymitem *item = sau_mpalloc(o->memp, sizeof(sauSymitem));
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
sauSymitem *sauSymtab_find_item(sauSymtab *restrict o sauMaybeUnused,
		sauSymstr *restrict symstr, uint32_t sym_type) {
	sauSymitem *item = symstr->item;
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
 * current string index from 0 to n will be set for SAU_SYM_DATA_ID.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return true, or false on allocation failure
 */
bool sauSymtab_add_stra(sauSymtab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t sym_type) {
	for (size_t i = 0; i < n; ++i) {
		sauSymitem *item;
		sauSymstr *s = sauSymtab_get_symstr(o,
				stra[i], strlen(stra[i]));
		if (!s || !(item = sauSymtab_add_item(o, s, sym_type)))
			return false;
		item->data_use = SAU_SYM_DATA_ID;
		item->data.id = i;
	}
	return true;
}
