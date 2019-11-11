/* saugns: Audio program interpreter module.
 * Copyright (c) 2013-2014, 2018-2019 Joel K. Pettersson
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
#include "../ptrlist.h"
#include "../program.h"
#include "../result.h"

/*
 * Interpreter for audio program. Produces results to render.
 */

size_t SAU_interpret(const SAU_PtrList *restrict prg_objs,
		SAU_PtrList *restrict res_objs);
