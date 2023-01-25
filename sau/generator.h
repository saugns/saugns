/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#include "program.h"

struct sauGenerator;
typedef struct sauGenerator sauGenerator;

sauGenerator* sau_create_Generator(const sauProgram *restrict prg,
		uint32_t srate) sauMalloclike;
void sau_destroy_Generator(sauGenerator *restrict o);

bool sauGenerator_run(sauGenerator *restrict o,
		int16_t *restrict buf, size_t buf_len, bool stereo,
		size_t *restrict out_len);
