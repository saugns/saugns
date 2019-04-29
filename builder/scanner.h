/* sgensys: Script scanner module.
 * Copyright (c) 2014, 2017-2019 Joel K. Pettersson
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
#include "file.h"
#include "symtab.h"

struct SGS_Scanner;
typedef struct SGS_Scanner SGS_Scanner;

/**
 * Function type used for SGS_Scanner_getc() character handlers.
 * Each Scanner instance uses a pointer to a 256-value array of these.
 *
 * The function takes the unsigned character value, processes it and
 * handles any further reading. Must return value to be used, or 0
 * if another read and corresponding handler call should be done.
 *
 * Handler functions may call other handler functions, and may alter
 * the table or replace it.
 *
 * NULL can be used as a function value, meaning that the character
 * read is to be accepted and used as-is.
 */
typedef uint8_t (*SGS_ScannerCHandler_f)(SGS_Scanner *o, uint8_t c);

/**
 * Characters returned after filtering. (Also used for comparison
 * with SGS_Scanner_tryc().)
 */
enum {
	SGS_SCAN_EOL = '\n',
};

/**
 * Flags set by character handlers.
 */
enum {
	SGS_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SGS_SCAN_C_NEWLINE = 1<<1, // newline scanned; must return SGS_SCAN_EOL
};

extern const SGS_ScannerCHandler_f SGS_Scanner_def_c_handlers[256];

uint8_t SGS_Scanner_handle_invalid(SGS_Scanner *o, uint8_t c);
uint8_t SGS_Scanner_handle_space(SGS_Scanner *o, uint8_t c);
uint8_t SGS_Scanner_handle_linebreaks(SGS_Scanner *o, uint8_t c);
uint8_t SGS_Scanner_handle_linecomment(SGS_Scanner *o, uint8_t c);
uint8_t SGS_Scanner_handle_blockcomment(SGS_Scanner *o, uint8_t check_c);
uint8_t SGS_Scanner_handle_slashcomments(SGS_Scanner *o, uint8_t c);
uint8_t SGS_Scanner_handle_hash_comments(SGS_Scanner *o, uint8_t c);

/**
 * Scanner state flags for current file.
 */
enum {
	SGS_SCAN_S_ERROR = 1<<0, // true if at least one error has been printed
	SGS_SCAN_S_UNGETC = 1<<1, // ungetc done after last getc
};

/**
 * Scanner type.
 */
struct SGS_Scanner {
	SGS_File *f;
	SGS_SymTab *symtab;
	SGS_ScannerCHandler_f *c_handlers; // copy of SGS_Scanner_def_c_handlers
	int32_t line_pos;
	int32_t char_pos;
	int32_t old_char_pos;
	uint8_t match_c; // for use by character handlers
	uint8_t c_flags;
	uint8_t s_flags;
	char *strbuf;
};

SGS_Scanner *SGS_create_Scanner(SGS_File *f, SGS_SymTab *symtab)
	SGS__malloclike;
void SGS_destroy_Scanner(SGS_Scanner *o);

char SGS_Scanner_getc(SGS_Scanner *o);
void SGS_Scanner_ungetc(SGS_Scanner *o);
bool SGS_Scanner_tryc(SGS_Scanner *o, char testc);

bool SGS_Scanner_getsymstr(SGS_Scanner *o,
		const void **strp, uint32_t *lenp);

void SGS_Scanner_warning(SGS_Scanner *o, const char *fmt, ...);
void SGS_Scanner_error(SGS_Scanner *o, const char *fmt, ...);
