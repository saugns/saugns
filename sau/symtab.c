/* SAU library: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2025 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "symtab.h"
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
static size_t hash_key(const uint8_t *restrict key, size_t len, size_t size) {
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
	return hash & (size - 1);
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
	alloc = old_alloc > 0 ? (old_alloc << 1) : STRTAB_ALLOC_INITIAL;
	if (!(sstra = calloc(alloc, sizeof(sauSymstr*))))
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
			hash = hash_key(node->key, node->key_len, alloc);
			/*
			 * Before adding the entry to the new table, set
			 * node->prev to the previous (if any) node with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_node = node->prev;
			node->prev = sstra[hash];
			sstra[hash] = node;
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
	if (o->count == (o->alloc / 2) && !StrTab_upsize(o))
		return NULL;
	size_t hash = hash_key(key, len, o->alloc);
	sauSymstr *sstr = o->sstra[hash];
	while (sstr != NULL) {
		if (sstr->key_len == len && !memcmp(sstr->key, key, len))
			return sstr;
		sstr = sstr->prev;
#if SAU_SYMTAB_STATS
		++collision_count;
#endif
	}
	if (!(sstr = sau_mpalloc(memp, sizeof(sauSymstr) + len + extra)))
		return NULL;
	sstr->prev = o->sstra[hash];
	o->sstra[hash] = sstr;
	sstr->key_len = len;
	memcpy(sstr->key, key, len);
	++o->count;
	return sstr;
}

typedef struct ScopeReg {
	sauSymstr **syms;
	size_t count;
	size_t alloc;
} ScopeReg;

static inline void fini_ScopeReg(ScopeReg *restrict o) {
	free(o->syms);
}

/*
 * Add symbol to list of lexically scoped ones;
 * it is subject to removal when its scope is left.
 *
 * \return true, or false on allocation failure
 */
static bool ScopeReg_add_item(ScopeReg *restrict o,
		sauSymstr *restrict symstr, uint16_t block_i) {
	if (o->count == o->alloc) {
		size_t alloc = o->alloc > 0 ? (o->alloc << 1) : 1;
		sauSymstr **syms = realloc(o->syms, sizeof(*syms) * alloc);
		if (!syms)
			return false;
		o->syms = syms;
		o->alloc = alloc;
	}
	o->syms[o->count++] = symstr;
	symstr->item->block_i = block_i;
	return true;
}

struct sauSymtab {
	sauMempool *memp;
	StrTab strt;
	ScopeReg sreg;
};

static void fini_Symtab(sauSymtab *restrict o) {
#if SAU_SYMTAB_STATS
	fprintf(stderr, "collision count: %zd\n", collision_count);
#endif
	fini_StrTab(&o->strt);
	fini_ScopeReg(&o->sreg);
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
 * Add an item for the string \p symstr. It will be added as a global symbol.
 *
 * \return item, or NULL if none
 */
sauSymitem *sauSymtab_add_item(sauSymtab *restrict o,
		sauSymstr *restrict symstr, uint8_t sym_type) {
	return sauSymtab_add_item_at(o, symstr, sym_type, 0);
}

/**
 * Add an item for the string \p symstr. It will be located at the
 * \p block_i lexical scope.
 *
 * \return item, or NULL if none
 */
sauSymitem *sauSymtab_add_item_at(sauSymtab *restrict o,
		sauSymstr *restrict symstr, uint8_t sym_type,
		uint16_t block_i) {
	sauSymitem *item = sau_mpalloc(o->memp, sizeof(sauSymitem));
	if (!item)
		return NULL;
	// The linked list per string entry must be kept sorted by \p block_i
	sauSymitem *prev_item = symstr->item, *reorder_item = NULL;
	while (prev_item && block_i < prev_item->block_i) {
		reorder_item = prev_item;
		prev_item = prev_item->prev;
	}
	if (reorder_item) {
		item->prev = reorder_item->prev;
		reorder_item->prev = item;
	} else {
		item->prev = symstr->item;
		symstr->item = item;
	}
	item->sym_type = sym_type;
	if (block_i > 0 && !ScopeReg_add_item(&o->sreg, symstr, block_i))
		return NULL;
	return item;
}

/**
 * Look for an item for the string \p symstr matching \p sym_type.
 *
 * \return item, or NULL if none
 */
sauSymitem *sauSymtab_find_item(sauSymtab *restrict o sauMaybeUnused,
		sauSymstr *restrict symstr, uint8_t sym_type) {
	sauSymitem *item = symstr->item;
	while (item) {
		if (item->sym_type == sym_type)
			return item;
		item = item->prev;
	}
	return NULL;
}

/**
 * Look for an item for the string \p symstr, matching \p sym_type
 * and located at lexical scope \p block_i or shallower levels.
 * For example, looking at level 0 means finding only globals.
 *
 * \return item, or NULL if none
 */
sauSymitem *sauSymtab_find_item_at(sauSymtab *restrict o sauMaybeUnused,
		sauSymstr *restrict symstr, uint8_t sym_type,
		uint16_t block_i) {
	sauSymitem *item = symstr->item;
	while (item) {
		if (item->sym_type == sym_type && item->block_i <= block_i)
			return item;
		item = item->prev;
	}
	return NULL;
}

/**
 * Drop all symbols defined below the \p block_i scope.
 */
void sauSymtab_drop_to(sauSymtab *restrict o, uint16_t block_i) {
	size_t i;
	for (i = o->sreg.count; i > 0; --i) {
		sauSymstr *symstr = o->sreg.syms[i-1];
		sauSymitem *item = symstr->item;
		if (item->block_i <= block_i) break; // target reached
		while (item->block_i > block_i)
			if (!(item = item->prev)) break;
		symstr->item = item; // shadowing symbols now dropped
	}
	o->sreg.count = i;
}

/**
 * Add the first \p n strings from \p stra to the string pool of the
 * symbol table. For each, an item will be prepared according to the
 * \p sym_type (with the type used assumed to store ID data) and the
 * current string index from 0 to n will be set for SAU_SYM_DATA_ID.
 *
 * The string index can be increased by passing non-zero \p id_from.
 * Each per-string item made will always be made for a global scope.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return true, or false on allocation failure
 */
bool sauSymtab_add_stra(sauSymtab *restrict o,
		const char *const*restrict stra, size_t n,
		uint8_t sym_type, uint32_t id_from) {
	for (size_t i = 0; i < n; ++i) {
		sauSymitem *item;
		sauSymstr *s = sauSymtab_get_symstr(o,
				stra[i], strlen(stra[i]));
		if (!s || !(item = sauSymtab_add_item(o, s, sym_type)))
			return false;
		item->data_use = SAU_SYM_DATA_ID;
		item->data_id = id_from + i;
	}
	return true;
}
