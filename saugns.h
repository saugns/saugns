/* saugns: Main functions and project definitions.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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
#include "ptrlist.h"

/*
 * Configuration options.
 */

#define SAU_CLINAME_STR "saugns"
#define SAU_VERSION_STR "v0.3-dev"

#define SAU_DEFAULT_SRATE 96000

/* Print symtab test statistics? */
#define SAU_SYMTAB_STATS 0

size_t SAU_build(const SAU_PtrList *restrict script_args, bool are_paths,
		SAU_PtrList *restrict prg_objs, bool print_info);

bool SAU_render(const SAU_PtrList *restrict prg_objs, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path);
