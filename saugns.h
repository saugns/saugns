/* saugns: Main functions and project definitions.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
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
#include "script.h"
#include "ptrarr.h"

#define SAU_CLINAME_STR "saugns"
#define SAU_VERSION_STR "v0.3-dev"

#define SAU_DEFAULT_SRATE 96000

/**
 * Command line options flags.
 */
enum {
	SAU_OPT_MODE_FULL     = 1<<0,
	SAU_OPT_SYSAU_ENABLE  = 1<<1,
	SAU_OPT_SYSAU_DISABLE = 1<<2,
	SAU_OPT_AUDIO_MONO    = 1<<3,
	SAU_OPT_AUDIO_STDOUT  = 1<<4,
	SAU_OPT_AUFILE_STDOUT = 1<<5,
	SAU_OPT_MODE_CHECK    = 1<<6,
	SAU_OPT_PRINT_INFO    = 1<<7,
	SAU_OPT_EVAL_STRING   = 1<<8,
	SAU_OPT_PRINT_VERBOSE = 1<<9,
};

#if SAU_ADD_TESTOPT
extern int SAU_testopt; /* defaults to 0, set using debug option "-?" */
#endif

size_t SAU_load(const SAU_PtrArr *restrict script_args, uint32_t options,
		SAU_PtrArr *restrict script_objs);

bool SAU_play(const SAU_PtrArr *restrict script_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path);

void SAU_discard(SAU_PtrArr *restrict script_objs);
