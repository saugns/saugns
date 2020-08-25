/* sgensys: Script scanner module.
 * Copyright (c) 2014, 2017-2020 Joel K. Pettersson
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
 * Number of values for which character filters are defined.
 *
 * Values below this are given their own function pointer;
 * SGS_Scanner_get_filter() handles mapping of other values.
 */
#define SGS_SCAN_FILTER_COUNT 128

/**
 * Number of old scan positions which can be returned to.
 */
#define SGS_SCAN_UNGET_MAX 63

/**
 * Function type used for filtered character getting.
 * Each Scanner instance uses a table of these.
 *
 * The function takes the raw character value, processes it and
 * may read further (updating the current scan frame) before
 * returning the character to use. May instead return 0 to
 * skip the character and prompt another read (and possibly a
 * corresponding filter call).
 *
 * NULL can be used as a function value, meaning that the character
 * should be used without filtering.
 *
 * Filter functions may call other filter functions,
 * and are allowed (not done by default functions) to alter the table.
 */
typedef uint8_t (*SGS_ScanFilter_f)(SGS_Scanner *o, uint8_t c);

/**
 * Special character values.
 */
enum {
	/**
	 * Returned for spaces and tabs after filtering.
	 * Also used for comparison with SGS_Scanner_tryc().
	 */
	SGS_SCAN_SPACE = ' ',
	/**
	 * Returned for linebreaks after filtering.
	 * Also used for comparison with SGS_Scanner_tryc()
	 * and SGS_Scanner_tryc_nospace().
	 */
	SGS_SCAN_LNBRK = '\n',
	/**
	 * Used internally. Returned by character filter
	 * to indicate that EOF is reached, error-checking done,
	 * and scanning complete for the file.
	 */
	SGS_SCAN_EOF = 0xFF,
};

/**
 * Flags set by character filters.
 */
enum {
	SGS_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SGS_SCAN_C_SPACE = 1<<1, // space scanned during the get
	SGS_SCAN_C_LNBRK = 1<<2, // linebreak scanned during the get
	SGS_SCAN_C_LNBRK_POSUP = 1<<3, // pending position update for linebreak
};

extern const SGS_ScanFilter_f SGS_Scanner_def_filters[SGS_SCAN_FILTER_COUNT];

/**
 * Whitespace filtering levels, used with
 * SGS_Scanner_setws_level() to assign standard filter functions.
 *
 * Default is SGS_SCAN_WS_ALL. Note that other filter functions,
 * e.g. comment filters, must filter the whitespace markers they
 * return using whichever filter functions are set, in order to
 * avoid excess marker characters in the output.
 */
enum {
	SGS_SCAN_WS_ALL = 0, // include all ws characters mapped to SGS_SCAN_*
	SGS_SCAN_WS_NONE,    // remove all ws characters
};

uint8_t SGS_Scanner_filter_invalid(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_space_keep(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_linebreak_keep(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_ws_none(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_linecomment(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c);
uint8_t SGS_Scanner_filter_slashcomments(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_char1comments(SGS_Scanner *restrict o, uint8_t c);

uint8_t SGS_Scanner_setws_level(SGS_Scanner *restrict o, uint8_t ws_level);

/**
 * Scanner state flags for current file.
 */
enum {
	SGS_SCAN_S_ERROR = 1<<0, // true if at least one error has been printed
	SGS_SCAN_S_DISCARD = 1<<1, // don't save scan frame next get
	SGS_SCAN_S_QUIET = 1<<2, // suppress warnings (but still print errors)
};

/**
 * Scan frame with character-level information for a get.
 */
typedef struct SGS_ScanFrame {
	int32_t line_num;
	int32_t char_num;
	uint8_t c;
	uint8_t c_flags;
} SGS_ScanFrame;

/**
 * Scanner type.
 */
struct SGS_Scanner {
	SGS_File *f;
	SGS_SymTab *symtab;
	SGS_ScanFilter_f *filters; // copy of SGS_Scanner_def_filters
	SGS_ScanFrame sf;
	uint8_t undo_pos;
	uint8_t unget_num;
	uint8_t s_flags;
	uint8_t match_c; // for use by character filters
	uint8_t ws_level; // level of SGS_Scanner_setws_level(), presuming use
	uint8_t *strbuf;
	void *data; // for use by user
	SGS_ScanFrame undo[SGS_SCAN_UNGET_MAX + 1];
};

SGS_Scanner *SGS_create_Scanner(SGS_SymTab *restrict symtab) sgsMalloclike;
void SGS_destroy_Scanner(SGS_Scanner *restrict o);

bool SGS_Scanner_open(SGS_Scanner *restrict o,
		const char *restrict script, bool is_path);
void SGS_Scanner_close(SGS_Scanner *restrict o);

/**
 * Get character filter to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SGS_SCAN_FILTER_COUNT are assigned the filter for
 * the raw value; other values are assigned the filter for '\0'.
 *
 * \return SGS_ScanFilter_f or NULL
 */
static inline SGS_ScanFilter_f SGS_Scanner_getfilter(SGS_Scanner *restrict o,
		uint8_t c) {
	if (c >= SGS_SCAN_FILTER_COUNT) c = 0;
	return o->filters[c];
}

/**
 * Call character filter for character \p c, unless a blank entry.
 * If calling, will set \a match_c for use by the filter function.
 *
 * \return result
 */
static inline uint8_t SGS_Scanner_usefilter(SGS_Scanner *restrict o,
		uint8_t c, uint8_t match_c) {
	SGS_ScanFilter_f f = SGS_Scanner_getfilter(o, c);
	if (f != NULL) {
		o->match_c = match_c;
		return f(o, c);
	}
	return c;
}

/**
 * Callback type allowing reading of named constants using SGS_Scanner_getd().
 * Should return non-zero length if number read and \p var set.
 */
typedef size_t (*SGS_ScanNumConst_f)(SGS_Scanner *restrict o,
		double *restrict var);

uint8_t SGS_Scanner_getc(SGS_Scanner *restrict o);
bool SGS_Scanner_tryc(SGS_Scanner *restrict o, uint8_t testc);
uint32_t SGS_Scanner_ungetc(SGS_Scanner *restrict o);
bool SGS_Scanner_geti(SGS_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SGS_Scanner_getd(SGS_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		SGS_ScanNumConst_f numconst_f);
bool SGS_Scanner_get_symstr(SGS_Scanner *restrict o,
		SGS_SymStr **restrict symstrp);

void SGS_Scanner_warning(const SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) sgsPrintflike(3, 4);
void SGS_Scanner_error(SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) sgsPrintflike(3, 4);
