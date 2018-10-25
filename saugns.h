/* saugns: Main functions and project definitions.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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
#include "program.h"

#define SAU_VERSION_STR "saugns v0.6.0"

SAU_Program* SAU_build(const char *restrict fname);

bool SAU_render(SAU_Program *restrict prg, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path);
