/* SAU library: Script scanner module.
 * Copyright (c) 2014, 2017-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "file.h"
#include "symtab.h"

struct sauScanner;
typedef struct sauScanner sauScanner;

/**
 * Number of values for which character filters are defined.
 *
 * Values below this are given their own function pointer;
 * sauScanner_get_filter() handles mapping of other values.
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
typedef uint8_t (*sauScanFilter_f)(sauScanner *o, uint8_t c);

/**
 * Special character values.
 */
enum {
	/**
	 * Returned for spaces and tabs after filtering.
	 * Also used for comparison with sauScanner_tryc().
	 */
	SAU_SCAN_SPACE = ' ',
	/**
	 * Returned for linebreaks after filtering.
	 * Also used for comparison with sauScanner_tryc()
	 * and sauScanner_tryc_nospace().
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

extern const sauScanFilter_f sauScanner_def_filters[SAU_SCAN_FILTER_COUNT];

/**
 * Whitespace filtering levels, used with
 * sauScanner_setws_level() to assign standard filter functions.
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

uint8_t sauScanner_filter_invalid(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_space_keep(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_linebreak_keep(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_ws_none(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_linecomment(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_blockcomment(sauScanner *restrict o,
		uint8_t check_c);
uint8_t sauScanner_filter_slashcomments(sauScanner *restrict o, uint8_t c);
uint8_t sauScanner_filter_char1comments(sauScanner *restrict o, uint8_t c);

uint8_t sauScanner_setws_level(sauScanner *restrict o, uint8_t ws_level);

/**
 * Scanner state flags for current file.
 */
enum {
	SAU_SCAN_S_ERROR = 1<<0, // true if at least one error has been printed
	SAU_SCAN_S_REGOT = 1<<1, // freshly regot
	SAU_SCAN_S_QUIET = 1<<2, // suppress warnings (but still print errors)
};

/**
 * Scan frame with character-level information for a get.
 */
typedef struct sauScanFrame {
	int32_t line_num;
	int32_t char_num;
	uint8_t c;
	uint8_t c_flags;
} sauScanFrame;

/**
 * Scanner type.
 */
struct sauScanner {
	sauFile *f;
	sauSymtab *symtab;
	sauScanFilter_f *filters; // copy of sauScanner_def_filters
	sauScanFrame sf;
	uint8_t undo_pos;
	uint8_t undo_ungets;
	uint8_t s_flags;
	uint8_t match_c; // for use by character filters
	uint8_t ws_level; // level of sauScanner_setws_level(), presuming use
	uint8_t *strbuf;
	void *data; // for use by user
	sauScanFrame undo[SAU_SCAN_UNGET_MAX + 1];
};

sauScanner *sau_create_Scanner(sauSymtab *restrict symtab) sauMalloclike;
void sau_destroy_Scanner(sauScanner *restrict o);

bool sauScanner_open(sauScanner *restrict o,
		const char *restrict script, bool is_path);
void sauScanner_close(sauScanner *restrict o);

/**
 * Get character filter to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SAU_SCAN_FILTER_COUNT are assigned the filter for
 * the raw value; other values are assigned the filter for '\0'.
 *
 * \return sauScanFilter_f or NULL
 */
static inline sauScanFilter_f sauScanner_getfilter(sauScanner *restrict o,
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
static inline uint8_t sauScanner_usefilter(sauScanner *restrict o,
		uint8_t c, uint8_t match_c) {
	sauScanFilter_f f = sauScanner_getfilter(o, c);
	if (f != NULL) {
		o->match_c = match_c;
		return f(o, c);
	}
	return c;
}

/**
 * Callback type allowing reading of named constants using sauScanner_getd().
 * Should return non-zero length if number read and \p var set.
 */
typedef size_t (*sauScanNumConst_f)(sauScanner *restrict o,
		double *restrict var);

uint8_t sauScanner_retc(sauScanner *restrict o);
uint8_t sauScanner_getc(sauScanner *restrict o);
uint8_t sauScanner_getc_after(sauScanner *restrict o, uint8_t testc);
uint8_t sauScanner_filterc(sauScanner *restrict o, uint8_t c,
		sauScanFilter_f filter_f);
bool sauScanner_tryc(sauScanner *restrict o, uint8_t testc);
uint32_t sauScanner_ungetc(sauScanner *restrict o);
bool sauScanner_geti(sauScanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool sauScanner_getd(sauScanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		sauScanNumConst_f numconst_f);
uint8_t sauScanner_get_suffc(sauScanner *restrict o);
bool sauScanner_get_symstr(sauScanner *restrict o,
		sauSymstr **restrict symstrp);
uint8_t sauScanner_skipws(sauScanner *restrict o);

void sauScanner_warning(const sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict fmt, ...) sauPrintflike(3, 4);
void sauScanner_error(sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict fmt, ...) sauPrintflike(3, 4);
void sauScanner_warning_at(const sauScanner *restrict o,
		int got_at, const char *restrict fmt, ...) sauPrintflike(3, 4);
void sauScanner_error_at(sauScanner *restrict o,
		int got_at, const char *restrict fmt, ...) sauPrintflike(3, 4);

/** Valid characters in identifiers. */
static inline bool sau_is_symchar(char c) {
	return SAU_IS_ALNUM(c) || (c) == '_';
}
