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
#include "plist.h"
#include "program.h"
#include "result.h"

/*
 * Interpreter for audio program. Produces results to render.
 */

struct SGS_Interpreter;
typedef struct SGS_Interpreter SGS_Interpreter;

SGS_Interpreter *SGS_create_Interpreter(void);
void SGS_destroy_Interpreter(SGS_Interpreter *o);

SGS_Result *SGS_Interpreter_run(SGS_Interpreter *o, SGS_Program *program);
void SGS_Interpreter_get_results(SGS_Interpreter *o, SGS_PList *dst);
void SGS_Interpreter_clear(SGS_Interpreter *o);
