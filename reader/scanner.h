/* saugns: Script scanner module.
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

struct SAU_Scanner;
typedef struct SAU_Scanner SAU_Scanner;

/**
 * Number of values for which character filters are defined.
 *
 * Values below this are given their own function pointer;
 * SAU_Scanner_get_filter() handles mapping of other values.
 */
#define SAU_SCAN_FILTER_COUNT 128

/**
 * Number of old scan positions which can be returned to.
 */
#define SAU_SCAN_UNGET_MAX 63

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
typedef uint8_t (*SAU_ScanFilter_f)(SAU_Scanner *o, uint8_t c);

/**
 * Special character values.
 */
enum {
	/**
	 * Returned for spaces and tabs after filtering.
	 * Also used for comparison with SAU_Scanner_tryc().
	 */
	SAU_SCAN_SPACE = ' ',
	/**
	 * Returned for linebreaks after filtering.
	 * Also used for comparison with SAU_Scanner_tryc()
	 * and SAU_Scanner_tryc_nospace().
	 */
	SAU_SCAN_LNBRK = '\n',
	/**
	 * Used internally. Returned by character filter
	 * to indicate that EOF is reached, error-checking done,
	 * and scanning complete for the file.
	 */
	SAU_SCAN_EOF = 0xFF,
};

/**
 * Flags set by character filters.
 */
enum {
	SAU_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SAU_SCAN_C_SPACE = 1<<1, // space scanned during the get
	SAU_SCAN_C_LNBRK = 1<<2, // linebreak scanned during the get
	SAU_SCAN_C_LNBRK_POSUP = 1<<3, // pending position update for linebreak
};

extern const SAU_ScanFilter_f SAU_Scanner_def_filters[SAU_SCAN_FILTER_COUNT];

/**
 * Whitespace filtering levels, used with
 * SAU_Scanner_setws_level() to assign standard filter functions.
 *
 * Default is SAU_SCAN_WS_ALL. Note that other filter functions,
 * e.g. comment filters, must filter the whitespace markers they
 * return using whichever filter functions are set, in order to
 * avoid excess marker characters in the output.
 */
enum {
	SAU_SCAN_WS_ALL = 0, // include all ws characters mapped to SAU_SCAN_*
	SAU_SCAN_WS_NONE,    // remove all ws characters
};

uint8_t SAU_Scanner_filter_invalid(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_space_keep(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_linebreak_keep(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_ws_none(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_linecomment(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_blockcomment(SAU_Scanner *restrict o,
		uint8_t check_c);
uint8_t SAU_Scanner_filter_slashcomments(SAU_Scanner *restrict o, uint8_t c);
uint8_t SAU_Scanner_filter_char1comments(SAU_Scanner *restrict o, uint8_t c);

uint8_t SAU_Scanner_setws_level(SAU_Scanner *restrict o, uint8_t ws_level);

/**
 * Scanner state flags for current file.
 */
enum {
	SAU_SCAN_S_ERROR = 1<<0, // true if at least one error has been printed
	SAU_SCAN_S_DISCARD = 1<<1, // don't save scan frame next get
	SAU_SCAN_S_QUIET = 1<<2, // suppress warnings (but still print errors)
};

/**
 * Scan frame with character-level information for a get.
 */
typedef struct SAU_ScanFrame {
	int32_t line_num;
	int32_t char_num;
	uint8_t c;
	uint8_t c_flags;
} SAU_ScanFrame;

/**
 * Scanner type.
 */
struct SAU_Scanner {
	SAU_File *f;
	SAU_SymTab *symtab;
	SAU_ScanFilter_f *filters; // copy of SAU_Scanner_def_filters
	SAU_ScanFrame sf;
	uint8_t undo_pos;
	uint8_t unget_num;
	uint8_t s_flags;
	uint8_t match_c; // for use by character filters
	uint8_t ws_level; // level of SAU_Scanner_setws_level(), presuming use
	uint8_t *strbuf;
	void *data; // for use by user
	SAU_ScanFrame undo[SAU_SCAN_UNGET_MAX + 1];
};

SAU_Scanner *SAU_create_Scanner(SAU_SymTab *restrict symtab) sauMalloclike;
void SAU_destroy_Scanner(SAU_Scanner *restrict o);

bool SAU_Scanner_open(SAU_Scanner *restrict o,
		const char *restrict script, bool is_path);
void SAU_Scanner_close(SAU_Scanner *restrict o);

/**
 * Get character filter to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SAU_SCAN_FILTER_COUNT are assigned the filter for
 * the raw value; other values are assigned the filter for '\0'.
 *
 * \return SAU_ScanFilter_f or NULL
 */
static inline SAU_ScanFilter_f SAU_Scanner_getfilter(SAU_Scanner *restrict o,
		uint8_t c) {
	if (c >= SAU_SCAN_FILTER_COUNT) c = 0;
	return o->filters[c];
}

/**
 * Call character filter for character \p c, unless a blank entry.
 * If calling, will set \a match_c for use by the filter function.
 *
 * \return result
 */
static inline uint8_t SAU_Scanner_usefilter(SAU_Scanner *restrict o,
		uint8_t c, uint8_t match_c) {
	SAU_ScanFilter_f f = SAU_Scanner_getfilter(o, c);
	if (f != NULL) {
		o->match_c = match_c;
		return f(o, c);
	}
	return c;
}

/**
 * Callback type allowing reading of named constants using SAU_Scanner_getd().
 * Should return non-zero length if number read and \p var set.
 */
typedef size_t (*SAU_ScanNumConst_f)(SAU_Scanner *restrict o,
		double *restrict var);

uint8_t SAU_Scanner_getc(SAU_Scanner *restrict o);
bool SAU_Scanner_tryc(SAU_Scanner *restrict o, uint8_t testc);
uint32_t SAU_Scanner_ungetc(SAU_Scanner *restrict o);
bool SAU_Scanner_geti(SAU_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SAU_Scanner_getd(SAU_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		SAU_ScanNumConst_f numconst_f);
bool SAU_Scanner_get_symstr(SAU_Scanner *restrict o,
		SAU_SymStr **restrict symstrp);

void SAU_Scanner_warning(const SAU_Scanner *restrict o,
		const SAU_ScanFrame *restrict sf,
		const char *restrict fmt, ...) sauPrintflike(3, 4);
void SAU_Scanner_error(SAU_Scanner *restrict o,
		const SAU_ScanFrame *restrict sf,
		const char *restrict fmt, ...) sauPrintflike(3, 4);
