/* sgensys: Main functions and project definitions.
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

#define SGS_CLINAME_STR "sgensys"
#define SGS_VERSION_STR "v0.2-dev"

#define SGS_DEFAULT_SRATE 96000

size_t SGS_build(const SGS_PtrList *restrict script_args, bool are_paths,
		SGS_PtrList *restrict prg_objs);

bool SGS_play(const SGS_PtrList *restrict prg_objs, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path);
