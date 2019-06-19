/* saugns: WAV file writer module.
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
#include "common.h"

struct SAU_WAVFile;
typedef struct SAU_WAVFile SAU_WAVFile;

SAU_WAVFile *SAU_create_WAVFile(const char *restrict fpath,
		uint16_t channels, uint32_t srate) SAU__malloclike;
int SAU_close_WAVFile(SAU_WAVFile *restrict o);

bool SAU_WAVFile_write(SAU_WAVFile *restrict o,
		const int16_t *restrict buf, uint32_t samples);
