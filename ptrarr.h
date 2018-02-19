/* sgensys pointer array module.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "sgensys.h"

/*
 * Pointer array type.
 */

typedef const void *SGSPtr_t;

struct SGSPtrArr {
	size_t count;
	size_t copy_count;
	SGSPtr_t *data;
	size_t alloc;
};

bool SGS_ptrarr_add(struct SGSPtrArr *list, SGSPtr_t value);
void SGS_ptrarr_clear(struct SGSPtrArr *list);
void SGS_ptrarr_copy(struct SGSPtrArr *dst, const struct SGSPtrArr *src);
