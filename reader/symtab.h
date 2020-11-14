/* ssndgen: Symbol table module.
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

#pragma once
#include "../common.h"

struct SSG_SymTab;
typedef struct SSG_SymTab SSG_SymTab;

SSG_SymTab *SSG_create_SymTab(void) SSG__malloclike;
void SSG_destroy_SymTab(SSG_SymTab *restrict o);

const void *SSG_SymTab_pool_str(SSG_SymTab *restrict o,
		const void *restrict str, size_t len);
const char **SSG_SymTab_pool_stra(SSG_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);

void *SSG_SymTab_get(SSG_SymTab *restrict o,
		const void *restrict key, size_t len);
void *SSG_SymTab_set(SSG_SymTab *restrict o,
		const void *restrict key, size_t len, void *restrict value);
