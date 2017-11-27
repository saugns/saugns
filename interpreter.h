/* sgensys: Audio program interpreter module.
 * Copyright (c) 2013-2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"
#include "result.h"

/*
 * Interpreter for audio program. Produces results to render.
 */

struct SGS_Interpreter;
typedef struct SGS_Interpreter SGS_Interpreter;

SGS_Interpreter *SGS_create_interpreter(void);
void SGS_destroy_interpreter(SGS_Interpreter *o);

SGS_Result *SGS_interpreter_run(SGS_Interpreter *o, SGS_Program *program);
void SGS_interpreter_get_results(SGS_Interpreter *o, SGS_Result ***results,
		size_t *count);

void SGS_interpreter_clear(SGS_Interpreter *o);
