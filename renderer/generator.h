/* saugns: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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
#include "../program.h"

struct SAU_Generator;
typedef struct SAU_Generator SAU_Generator;

SAU_Generator* SAU_create_Generator(const SAU_Program *restrict prg,
		uint32_t srate) SAU__malloclike;
void SAU_destroy_Generator(SAU_Generator *restrict o);

bool SAU_Generator_run(SAU_Generator *restrict o,
		int16_t *restrict buf, size_t buf_len,
		size_t *restrict out_len);
