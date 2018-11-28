/* sgensys: Audio program builder module.
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

#include "sgensys.h"
#include "script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build(const char *restrict script_arg, bool is_path) {
	SGS_Script *sd = SGS_load_Script(script_arg, is_path);
	if (!sd) return NULL;

	SGS_Program *o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
	return o;
}
