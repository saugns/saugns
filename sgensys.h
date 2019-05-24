/* sgensys: Main functions and project definitions.
 * Copyright (c) 2011-2013, 2017-2021 Joel K. Pettersson
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
#include "ptrarr.h"

#define SGS_CLINAME_STR "sgensys"
#define SGS_VERSION_STR "v0.2-dev"

#define SGS_DEFAULT_SRATE 96000

/**
 * Command line options flags.
 */
enum {
	SGS_OPT_MODE_FULL     = 1<<0,
	SGS_OPT_AUDIO_ENABLE  = 1<<1,
	SGS_OPT_AUDIO_DISABLE = 1<<2,
	SGS_OPT_MODE_CHECK    = 1<<3,
	SGS_OPT_PRINT_INFO    = 1<<4,
	SGS_OPT_EVAL_STRING   = 1<<5,
};

size_t SGS_load(const SGS_PtrArr *restrict script_args, uint32_t options,
		SGS_PtrArr *restrict prg_objs);

bool SGS_play(const SGS_PtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path);

void SGS_discard(SGS_PtrArr *restrict prg_objs);
