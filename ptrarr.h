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

struct SGSPtrArr {
	uint32_t count;
	uint32_t copy_count;
	void **data;
	uint32_t alloc;
};

void SGS_ptrarr_add(struct SGSPtrArr *list, void *node);
void SGS_ptrarr_clear(struct SGSPtrArr *list);
void SGS_ptrarr_copy(struct SGSPtrArr *dst, const struct SGSPtrArr *src);
