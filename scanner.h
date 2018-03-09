/* sgensys: Script file scanner module.
 * Copyright (c) 2014, 2017-2018 Joel K. Pettersson
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
#include "stream.h"

/**
 * Characters returned after filtering. Also used for comparisons.
 */
enum {
	SGS_SCAN_NEWLINE = '\n',
};

struct SGS_Scanner {
	SGS_Stream fr;
	int32_t line_num, char_num;
	bool newline;
	int32_t old_char_num;
};
typedef struct SGS_Scanner SGS_Scanner;

SGS_Scanner *SGS_create_Scanner(void);
void SGS_destroy_Scanner(SGS_Scanner *o);

bool SGS_Scanner_open(SGS_Scanner *o, const char *fname);
void SGS_Scanner_close(SGS_Scanner *o);

char SGS_Scanner_getc(SGS_Scanner *o);
bool SGS_Scanner_tryc(SGS_Scanner *o, char testc);

char *SGS_Scanner_get_id(SGS_Scanner *o);

void SGS_Scanner_warning(SGS_Scanner *o, const char *fmt, ...);
void SGS_Scanner_error(SGS_Scanner *o, const char *fmt, ...);
