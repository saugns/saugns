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
 * Number of values for which character filters are defined.
 *
 * Values below this are given their own function pointer;
 * SGS_Scanner_get_c_filter() handles mapping of other values.
 */
#define SGS_SCAN_CFILTERS 128

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
 * and are allowed to alter the table.
 */
typedef uint8_t (*SGS_ScanCFilter_f)(SGS_Scanner *o, uint8_t c);

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
	SGS_SCAN_C_LNBRK = 1<<1, // linebreak scanned last get character call
};

extern const SGS_ScanCFilter_f SGS_Scanner_def_c_filters[SGS_SCAN_CFILTERS];

uint8_t SGS_Scanner_filter_invalid(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_space(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_linebreaks(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_linecomment(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c);
uint8_t SGS_Scanner_filter_slashcomments(SGS_Scanner *restrict o, uint8_t c);
uint8_t SGS_Scanner_filter_char1comments(SGS_Scanner *restrict o, uint8_t c);

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
	SGS_ScanCFilter_f *c_filters; // copy of SGS_Scanner_def_c_filters
	SGS_ScanFrame sf;
	uint8_t undo_pos;
	uint8_t unget_num;
	uint8_t s_flags;
	uint8_t match_c; // for use by character filters
	void *data; // for use by user
	SGS_ScanFrame undo[SGS_SCAN_UNGET_MAX + 1];
};

SGS_Scanner *SGS_create_Scanner(void) SGS__malloclike;
void SGS_destroy_Scanner(SGS_Scanner *restrict o);

bool SGS_Scanner_fopenrb(SGS_Scanner *restrict o, const char *restrict path);
void SGS_Scanner_close(SGS_Scanner *restrict o);

/**
 * Get character filter to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SGS_SCAN_CFILTERS are assigned the filter for the raw value;
 * other values are assigned the filter for '\0'.
 *
 * \return SGS_ScanCFilter_f or NULL
 */
static inline SGS_ScanCFilter_f SGS_Scanner_getfilter(SGS_Scanner *restrict o,
		uint8_t c) {
	if (c >= SGS_SCAN_CFILTERS) c = 0;
	return o->c_filters[c];
}

uint8_t SGS_Scanner_getc(SGS_Scanner *restrict o);
uint8_t SGS_Scanner_getc_nospace(SGS_Scanner *restrict o);
bool SGS_Scanner_tryc(SGS_Scanner *restrict o, uint8_t testc);
bool SGS_Scanner_tryc_nospace(SGS_Scanner *restrict o, uint8_t testc);
uint32_t SGS_Scanner_ungetc(SGS_Scanner *restrict o);
bool SGS_Scanner_geti(SGS_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SGS_Scanner_getd(SGS_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SGS_Scanner_getsyms(SGS_Scanner *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict str_len);

/**
 * Advance past space on the same line.
 * Equivalent to calling SGS_Scanner_tryc() for SGS_SCAN_SPACE.
 */
static inline void SGS_Scanner_skipspace(SGS_Scanner *restrict o) {
	SGS_Scanner_tryc(o, SGS_SCAN_SPACE);
}

/**
 * Advance past whitespace, including linebreaks.
 * Equivalent to calling SGS_Scanner_tryc_nospace() for SGS_SCAN_LNBRK.
 */
static inline void SGS_Scanner_skipws(SGS_Scanner *restrict o) {
	SGS_Scanner_tryc_nospace(o, SGS_SCAN_LNBRK);
}

void SGS_Scanner_warning(const SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) SGS__printflike(3, 4);
void SGS_Scanner_error(SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) SGS__printflike(3, 4);
