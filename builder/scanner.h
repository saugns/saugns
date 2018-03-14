/* sgensys: Script scanner module.
 * Copyright (c) 2014, 2017-2018 Joel K. Pettersson
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
 * Number of values for which character handlers are defined.
 *
 * Values below this are given their own function pointer;
 * SGS_Scanner_get_c_handler() handles mapping of other values.
 */
#define SGS_SCAN_CHVALS 128

/**
 * Function type used for SGS_Scanner_getc() character handlers.
 * Each Scanner instance uses a table of these.
 *
 * The function takes the unsigned character value, processes it and
 * handles any further reading. Must return value to be used, or 0
 * if another read and corresponding handler call should be done.
 *
 * Handler functions may call other handler functions, and are allowed
 * to alter the table.
 *
 * NULL can be used as a function value, meaning that the character
 * read is to be accepted and used as-is.
 */
typedef uint8_t (*SGS_ScannerCHandler_f)(SGS_Scanner *o, uint8_t c);

/**
 * Special character values.
 */
enum {
	/**
	 * Returned for newlines after filtering.
	 * Also used for comparison with SGS_Scanner_tryc().
	 */
	SGS_SCAN_EOL = '\n',
	/**
	 * Used internally, never returned after filtering.
	 * Returned by character handler to indicate that
	 * EOF is reached and error-checking has completed,
	 * and that scanning is done.
	 */
	SGS_SCAN_EOF = 0xFF,
};

/**
 * Flags set by character handlers.
 */
enum {
	SGS_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SGS_SCAN_C_NEWLINE = 1<<1, // newline scanned; must return SGS_SCAN_EOL
};

extern const SGS_ScannerCHandler_f SGS_Scanner_def_c_handlers[SGS_SCAN_CHVALS];

uint8_t SGS_Scanner_handle_invalid(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_handle_space(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_handle_linebreaks(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_handle_linecomment(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_handle_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c);
uint8_t SGS_Scanner_handle_slashcomments(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_handle_char1comments(SGS_Scanner *restrict o, uint8_t c);

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
	uint8_t *strbuf;
};

SGS_Scanner *SGS_create_Scanner(SGS_SymTab *restrict symtab) SGS__malloclike;
void SGS_destroy_Scanner(SGS_Scanner *restrict o);

bool SGS_Scanner_fopenrb(SGS_Scanner *restrict o, const char *restrict path);
void SGS_Scanner_close(SGS_Scanner *restrict o);

/**
 * Get character handler to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SGS_SCAN_CHVALS are assigned the handler for the raw value;
 * other values are assigned the handler for '\0'.
 *
 * \return SGS_ScannerCHandler_f or NULL
 */
static inline SGS_ScannerCHandler_f SGS_Scanner_get_c_handler(SGS_Scanner *restrict o,
		uint8_t c) {
	if (c >= SGS_SCAN_CHVALS) c = 0;
	return o->c_handlers[c];
}

uint8_t SGS_Scanner_getc(SGS_Scanner *restrict o);
void SGS_Scanner_ungetc(SGS_Scanner *restrict o);
bool SGS_Scanner_tryc(SGS_Scanner *restrict o, uint8_t testc);
bool SGS_Scanner_getsyms(SGS_Scanner *restrict o,
		const void **restrict strp, uint32_t *restrict lenp);

void SGS_Scanner_warning(SGS_Scanner *restrict o,
		const char *restrict fmt, ...) SGS__printflike(2, 3);
void SGS_Scanner_error(SGS_Scanner *restrict o,
		const char *restrict fmt, ...) SGS__printflike(2, 3);
