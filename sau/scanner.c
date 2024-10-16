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

#include <sau/scanner.h>
#include <sau/math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STRBUF_LEN 256

#if SAU_SCANNER_STATS
static size_t hits = 0;
static size_t misses = 0;
#endif

/**
 * Create instance. Requires \p symtab to be a valid instance.
 *
 * Assigns a modifiable copy of the sauScanner_def_filters array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
sauScanner *sau_create_Scanner(sauSymtab *restrict symtab) {
	if (!symtab)
		return NULL;
	sauScanner *o = calloc(1, sizeof(sauScanner));
	if (!o)
		return NULL;
	o->f = sau_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t filters_size = sizeof(sauScanFilter_f) * SAU_SCAN_FILTER_COUNT;
	o->filters = malloc(filters_size);
	if (!o->filters) goto ERROR;
	memcpy(o->filters, sauScanner_def_filters, filters_size);
	o->ws_level = SAU_SCAN_WS_ALL;
	o->strbuf = calloc(1, STRBUF_LEN);
	if (!o->strbuf) goto ERROR;
	return o;
ERROR:
	sau_destroy_Scanner(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void sau_destroy_Scanner(sauScanner *restrict o) {
	if (!o)
		return;
#if SAU_SCANNER_STATS
	fprintf(stderr, "hits: %zd\nmisses: %zd\n", hits, misses);
#endif
	sau_destroy_File(o->f);
	free(o->strbuf);
	free(o->filters);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around sauFile functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool sauScanner_open(sauScanner *restrict o,
		const char *restrict script, bool is_path) {
	if (!is_path) {
		sauFile_stropenrb(o->f, "<string>", script);
	} else if (!sauFile_fopenrb(o->f, script)) {
		sau_error(NULL,
"couldn't open script file \"%s\" for reading", script);
		return false;
	}

	o->sf.line_num = 1; // not increased upon first read
	o->sf.char_num = 0;
	return true;
}

/**
 * Close file (if open).
 */
void sauScanner_close(sauScanner *restrict o) {
	sauFile_close(o->f);
}

static uint8_t filter_symchar(sauFile *restrict o sauMaybeUnused,
		uint8_t c) {
	return sau_is_symchar(c) ? c : 0;
}

/*
 * Read identifier string into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be used to set the string length.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
static bool read_symstr(sauFile *restrict f,
		uint8_t *restrict buf, size_t buf_len,
		size_t *restrict lenp) {
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = sauFile_GETC(f);
		if (!sau_is_symchar(c)) {
			sauFile_DECP(f);
			break;
		}
		buf[i++] = c;
	}
	buf[i] = '\0';
	if (lenp) *lenp = i;
	return !truncate;
}

/**
 * Handle invalid character, or the end of the file. Prints
 * an invalid character warning unless the file has ended.
 *
 * Checks file status, returning SAU_SCAN_EOF if the file has ended,
 * and printing a warning upon error.
 *
 * \return 0 or SAU_SCAN_EOF
 */
uint8_t sauScanner_filter_invalid(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	if (!sauFile_AFTER_EOF(f)) {
		sauScanner_warning(o, NULL,
			"invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = sauFile_STATUS(f);
	if (status & SAU_FILE_ERROR) {
		sauScanner_error(o, NULL,
			"file reading failed");
	}
	return SAU_SCAN_EOF;
}

static inline void pos_past_linebreak(sauScanner *restrict o,
		size_t char_num) {
	++o->sf.line_num;
	o->sf.char_num = char_num;
}

/**
 * Return standard space marker (for space or tab).
 *
 * \return SAU_SCAN_SPACE
 */
uint8_t sauScanner_filter_space_keep(sauScanner *restrict o sauMaybeUnused,
		uint8_t c sauMaybeUnused) {
	o->sf.c_flags |= SAU_SCAN_C_SPACE;
	return SAU_SCAN_SPACE;
}

/**
 * Handle linebreak portably (move past CR for LF),
 * and return standard linebreak marker.
 *
 * \return SAU_SCAN_LNBRK
 */
uint8_t sauScanner_filter_linebreak_keep(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	if (c == '\n') sauFile_TRYC(f, '\r');
	o->sf.c_flags |= (SAU_SCAN_C_LNBRK | SAU_SCAN_C_LNBRK_POSUP);
	return SAU_SCAN_LNBRK;
}

/**
 * Skip spaces and/or linebreaks.
 *
 * \return 0
 */
uint8_t sauScanner_filter_ws_none(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	if (c == '\n') {
		sauFile_TRYC(f, '\r');
	} else if (c != '\r') {
		o->sf.char_num += sauFile_skipspace(f);
		return 0;
	}
	o->sf.c_flags |= SAU_SCAN_C_LNBRK;
	o->sf.c_flags &= ~SAU_SCAN_C_LNBRK_POSUP;
	pos_past_linebreak(o, 0);

	size_t space_count;
LNBRK:
	while (sauFile_trynewline(f)) pos_past_linebreak(o, 0);
	space_count = sauFile_skipspace(f);
	if (space_count > 0) {
		o->sf.char_num = space_count;
		goto LNBRK;
	}
	return 0;
}

/**
 * Skip characters until the next character ends the line (or file).
 *
 * Call for a character to use it as a line comment opener.
 *
 * \return 0
 */
uint8_t sauScanner_filter_linecomment(sauScanner *restrict o,
		uint8_t c sauMaybeUnused) {
	sauFile *f = o->f;
	o->sf.char_num += sauFile_skipline(f);
	return 0;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. sauScanner_filter_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the linebreak flag. Linebreaks within a block comment
 * are ignored (commented out), apart from in line numbering.
 * A block comment counts syntactically as a single space (so that
 * it cannot silently be placed between characters in a token),
 * unless all whitespace is filtered out.
 *
 * \return filtered SAU_SCAN_SPACE, or SAU_SCAN_EOF (on unterminated comment)
 */
uint8_t sauScanner_filter_blockcomment(sauScanner *restrict o,
		uint8_t check_c) {
	sauFile *f = o->f;
	int32_t line_num = o->sf.line_num;
	int32_t char_num = o->sf.char_num;
	for (;;) {
		uint8_t c = sauFile_GETC(f);
		++char_num;
		if (c == '\n') {
			++line_num;
			char_num = 0;
			sauFile_TRYC(f, '\r');
		} else if (c == '\r') {
			++line_num;
			char_num = 0;
		} else if (c == check_c) {
			if (sauFile_TRYC(f, o->match_c)) {
				++char_num;
				break; /* end of block comment */
			}
		} else if (c <= SAU_FILE_MARKER && sauFile_AFTER_EOF(f)) {
			c = sauScanner_filter_invalid(o, c);
			o->sf.c_flags |= SAU_SCAN_C_ERROR;
			--o->sf.char_num; // print for beginning of comment
			sauScanner_error(o, NULL, "unterminated comment");
			++o->sf.char_num;
			return SAU_SCAN_EOF;
		}
	}
	o->sf.line_num = line_num;
	o->sf.char_num = char_num;
	return sauScanner_usefilter(o, SAU_SCAN_SPACE, SAU_SCAN_SPACE);
}

/**
 * Use for '/' (slash) to handle C-style and C++-style comments.
 *
 * Checks the next character for C-style or C++-style comment opener,
 * handling comment if present, otherwise simply returning the first
 * character.
 *
 * \return \p c, 0, filtered SAU_SCAN_SPACE, or SAU_SCAN_EOF
 */
uint8_t sauScanner_filter_slashcomments(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	uint8_t next_c = sauFile_GETC(f);
	if (next_c == '*') {
		++o->sf.char_num;
		o->match_c = '/';
		return sauScanner_filter_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->sf.char_num;
		return sauScanner_filter_linecomment(o, next_c);
	}
	sauFile_DECP(f);
	return c;
}

/**
 * If at the beginning of a line, handle line comment.
 * Otherwise, simply return the character.
 *
 * Call for a character to use it as a line comment opener
 * for the first character position only. (For example,
 * git-style comments, or old Fortran comments.)
 *
 * \return \p c or 0
 */
uint8_t sauScanner_filter_char1comments(sauScanner *restrict o, uint8_t c) {
	if (o->sf.char_num == 1)
		return sauScanner_filter_linecomment(o, c);
	return c;
}

/**
 * Default array of character filter functions for sauScanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const sauScanFilter_f sauScanner_def_filters[SAU_SCAN_FILTER_COUNT] = {
	/* NUL 0x00 */ sauScanner_filter_invalid, // also for values above 127
	/* SOH 0x01 */ sauScanner_filter_invalid,
	/* STX 0x02 */ sauScanner_filter_invalid,
	/* ETX 0x03 */ sauScanner_filter_invalid,
	/* EOT 0x04 */ sauScanner_filter_invalid,
	/* ENQ 0x05 */ sauScanner_filter_invalid,
	/* ACK 0x06 */ sauScanner_filter_invalid,
	/* BEL '\a' */ sauScanner_filter_invalid, // SAU_FILE_MARKER
	/* BS  '\b' */ sauScanner_filter_invalid,
	/* HT  '\t' */ sauScanner_filter_space_keep,
	/* LF  '\n' */ sauScanner_filter_linebreak_keep,
	/* VT  '\v' */ sauScanner_filter_invalid,
	/* FF  '\f' */ sauScanner_filter_invalid,
	/* CR  '\r' */ sauScanner_filter_linebreak_keep,
	/* SO  0x0E */ sauScanner_filter_invalid,
	/* SI  0x0F */ sauScanner_filter_invalid,
	/* DLE 0x10 */ sauScanner_filter_invalid,
	/* DC1 0x11 */ sauScanner_filter_invalid,
	/* DC2 0x12 */ sauScanner_filter_invalid,
	/* DC3 0x13 */ sauScanner_filter_invalid,
	/* DC4 0x14 */ sauScanner_filter_invalid,
	/* NAK 0x15 */ sauScanner_filter_invalid,
	/* SYN 0x16 */ sauScanner_filter_invalid,
	/* ETB 0x17 */ sauScanner_filter_invalid,
	/* CAN 0x18 */ sauScanner_filter_invalid,
	/* EM  0x19 */ sauScanner_filter_invalid,
	/* SUB 0x1A */ sauScanner_filter_invalid,
	/* ESC 0x1B */ sauScanner_filter_invalid,
	/* FS  0x1C */ sauScanner_filter_invalid,
	/* GS  0x1D */ sauScanner_filter_invalid,
	/* RS  0x1E */ sauScanner_filter_invalid,
	/* US  0x1F */ sauScanner_filter_invalid,
	/*     ' '  */ sauScanner_filter_space_keep,
	/*     '!'  */ NULL,
	/*     '"'  */ NULL,
	/*     '#'  */ sauScanner_filter_linecomment,
	/*     '$'  */ NULL,
	/*     '%'  */ NULL,
	/*     '&'  */ NULL,
	/*     '\'' */ NULL,
	/*     '('  */ NULL,
	/*     ')'  */ NULL,
	/*     '*'  */ NULL,
	/*     '+'  */ NULL,
	/*     ','  */ NULL,
	/*     '-'  */ NULL,
	/*     '.'  */ NULL,
	/*     '/'  */ sauScanner_filter_slashcomments,
	/* num '0'  */ NULL,
	/* num '1'  */ NULL,
	/* num '2'  */ NULL,
	/* num '3'  */ NULL,
	/* num '4'  */ NULL,
	/* num '5'  */ NULL,
	/* num '6'  */ NULL,
	/* num '7'  */ NULL,
	/* num '8'  */ NULL,
	/* num '9'  */ NULL,
	/*     ':'  */ NULL,
	/*     ';'  */ NULL,
	/*     '<'  */ NULL,
	/*     '='  */ NULL,
	/*     '>'  */ NULL,
	/*     '?'  */ NULL,
	/*     '@'  */ NULL,
	/* AL  'A'  */ NULL,
	/* AL  'B'  */ NULL,
	/* AL  'C'  */ NULL,
	/* AL  'D'  */ NULL,
	/* AL  'E'  */ NULL,
	/* AL  'F'  */ NULL,
	/* AL  'G'  */ NULL,
	/* AL  'H'  */ NULL,
	/* AL  'I'  */ NULL,
	/* AL  'J'  */ NULL,
	/* AL  'K'  */ NULL,
	/* AL  'L'  */ NULL,
	/* AL  'M'  */ NULL,
	/* AL  'N'  */ NULL,
	/* AL  'O'  */ NULL,
	/* AL  'P'  */ NULL,
	/* AL  'Q'  */ NULL,
	/* AL  'R'  */ NULL,
	/* AL  'S'  */ NULL,
	/* AL  'T'  */ NULL,
	/* AL  'U'  */ NULL,
	/* AL  'V'  */ NULL,
	/* AL  'W'  */ NULL,
	/* AL  'X'  */ NULL,
	/* AL  'Y'  */ NULL,
	/* AL  'Z'  */ NULL,
	/*     '['  */ NULL,
	/*     '\\' */ NULL,
	/*     ']'  */ NULL,
	/*     '^'  */ NULL,
	/*     '_'  */ NULL,
	/*     '`'  */ NULL,
	/* al  'a'  */ NULL,
	/* al  'b'  */ NULL,
	/* al  'c'  */ NULL,
	/* al  'd'  */ NULL,
	/* al  'e'  */ NULL,
	/* al  'f'  */ NULL,
	/* al  'g'  */ NULL,
	/* al  'h'  */ NULL,
	/* al  'i'  */ NULL,
	/* al  'j'  */ NULL,
	/* al  'k'  */ NULL,
	/* al  'l'  */ NULL,
	/* al  'm'  */ NULL,
	/* al  'n'  */ NULL,
	/* al  'o'  */ NULL,
	/* al  'p'  */ NULL,
	/* al  'q'  */ NULL,
	/* al  'r'  */ NULL,
	/* al  's'  */ NULL,
	/* al  't'  */ NULL,
	/* al  'u'  */ NULL,
	/* al  'v'  */ NULL,
	/* al  'w'  */ NULL,
	/* al  'x'  */ NULL,
	/* al  'y'  */ NULL,
	/* al  'z'  */ NULL,
	/*     '{'  */ NULL,
	/*     '|'  */ NULL,
	/*     '}'  */ NULL,
	/*     '~'  */ NULL,
	/* DEL 0x7F */ sauScanner_filter_invalid,
};

/**
 * Set filter functions for whitespace characters to a standard set.
 *
 * \return old ws_level value
 */
uint8_t sauScanner_setws_level(sauScanner *restrict o, uint8_t ws_level) {
	uint8_t old_level = o->ws_level;
	switch (ws_level) {
	case SAU_SCAN_WS_ALL: // default level
		o->filters['\t'] = sauScanner_filter_space_keep;
		o->filters['\n'] = sauScanner_filter_linebreak_keep;
		o->filters['\r'] = sauScanner_filter_linebreak_keep;
		o->filters[' ']  = sauScanner_filter_space_keep;
		break;
	case SAU_SCAN_WS_NONE:
		o->filters['\t'] = sauScanner_filter_ws_none;
		o->filters['\n'] = sauScanner_filter_ws_none;
		o->filters['\r'] = sauScanner_filter_ws_none;
		o->filters[' ']  = sauScanner_filter_ws_none;
		break;
	}
	o->ws_level = ws_level;
	return old_level;
}

/*
 * Assign scan frame from undo buffer, moving \a undo_pos by \p offset.
 */
static void change_frame(sauScanner *restrict o, int offset) {
	o->undo_pos = (o->undo_pos + offset) & SAU_SCAN_UNGET_MAX;
	o->sf = o->undo[o->undo_pos];
}

/*
 * Perform pending updates before a get call.
 */
static void pre_get_setup(sauScanner *restrict o) {
	if (o->undo_ungets > 0) {
		--o->undo_ungets;
		/*
		 * Start from frame after the one ungotten to.
		 */
		change_frame(o, +1);
		o->s_flags |= SAU_SCAN_S_REGOT;
		--o->sf.char_num;
	}
}

/*
 * Perform pending updates for a completed one-character get call.
 */
static void prepare_frame(sauScanner *restrict o) {
	if (o->s_flags & SAU_SCAN_S_REGOT) {
		o->s_flags &= ~SAU_SCAN_S_REGOT;
	} else {
		o->undo_pos = (o->undo_pos + 1) & SAU_SCAN_UNGET_MAX;
	}
	o->undo[o->undo_pos] = o->sf;
	if (o->sf.c_flags & SAU_SCAN_C_LNBRK_POSUP) {
		o->sf.c_flags &= ~SAU_SCAN_C_LNBRK_POSUP;
		pos_past_linebreak(o, 0);
	}
	o->sf.c_flags &= ~(SAU_SCAN_C_SPACE | SAU_SCAN_C_LNBRK);
}

/*
 * Set character used after filtering.
 *
 * Sets the file buffer character before the current to \p c,
 * so that a new get after an undo arrives at \p c.
 */
static void set_usedc(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	size_t r_pos = f->pos;
	o->sf.c = c;
	sauFile_DECP(f);
	sauFile_FIXP(f);
	sauFile_SETC_NC(f, c);
	f->pos = r_pos;
}

/*
 * Perform updates after reading a sequence of characters,
 * e.g. a string or number. Prepares a temporary post-get scan frame.
 *
 * TODO: The current approach doeen't allow for unget with position preserved.
 */
static void advance_frame(sauScanner *restrict o,
		size_t strlen, size_t prelen, uint8_t c) {
	if (strlen == 0)
		return;
	uint32_t reget_count = strlen - prelen;
	if (reget_count > o->undo_ungets) reget_count = o->undo_ungets;
	if (reget_count > 0) {
		/*
		 * Advance past ungets prior to frame to restore to.
		 */
		o->undo_ungets -= (reget_count - 1);
	}
	o->sf.char_num += prelen;
	prepare_frame(o);
	o->sf.char_num += strlen - prelen;
	o->sf.c = c;
}

/**
 * Filter character, read more if needed until a character can be returned.
 *
 * \return character, or 0 upon end of file
 */
uint8_t sauScanner_filterc(sauScanner *restrict o, uint8_t c,
		sauScanFilter_f filter_f) {
	sauFile_INCP(o->f);
	pre_get_setup(o);
	for (;;) {
		++o->sf.char_num;
		o->match_c = 0;
		c = filter_f(o, c);
		if (c != 0) {
			if (c == SAU_SCAN_EOF) {
				c = 0;
				goto RETURN;
			}
			set_usedc(o, c);
			break;
		}
		c = sauFile_GETC(o->f);
		filter_f = sauScanner_getfilter(o, c);
		if (!filter_f) {
			++o->sf.char_num;
			o->sf.c = c;
			break;
		}
	}
RETURN:
	prepare_frame(o);
	return c;
}

/**
 * Get current character, without advancing the position.
 * Filter functions will be used with \a match_c set to 0.
 * This function does the work necessary to check what the
 * filtered character will be. Call sauScanner_getc() for
 * the purpose of then moving past the character as such.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character, or 0 upon end of file
 */
uint8_t sauScanner_retc(sauScanner *restrict o) {
	uint8_t c = sauFile_RETC(o->f);
	sauScanFilter_f filter_f = sauScanner_getfilter(o, c);
	if (filter_f) {
		c = sauScanner_filterc(o, c, filter_f);
		sauScanner_ungetc(o);
	}
	return c;
}

/**
 * Get current character, advancing the position afterwards.
 * Filter functions will be used with \a match_c set to 0.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character, or 0 upon end of file
 */
uint8_t sauScanner_getc(sauScanner *restrict o) {
	uint8_t c;
	sauScanFilter_f filter_f;
	pre_get_setup(o);
	for (;;) {
		c = sauFile_GETC(o->f);
		filter_f = sauScanner_getfilter(o, c);
		++o->sf.char_num;
		if (!filter_f) {
			o->sf.c = c;
			break;
		}
		o->match_c = 0;
		c = filter_f(o, c);
		if (c != 0) {
			if (c == SAU_SCAN_EOF) {
				c = 0;
				goto RETURN;
			}
			set_usedc(o, c);
			break;
		}
	}
RETURN:
	prepare_frame(o);
	return c;
}

/**
 * Get character after the current if \p testc was matched first.
 * Advances the position if both characters got, otherwise acts like
 * an unsuccessful sauScanner_tryc().
 *
 * A value of 0 is returned for no character. Otherwise handles 0
 * values like sauScanner_getc().
 *
 * \return character, or 0 upon either mismatch or end of file
 */
uint8_t sauScanner_getc_after(sauScanner *restrict o, uint8_t testc) {
	if (!sauScanner_tryc(o, testc))
		return 0;
	return sauScanner_getc(o);
}

/**
 * Advance the position past the current character if it matches \p testc.
 * Note that characters removed by filters cannot be tested successfully.
 *
 * For filtered characters, does a get followed by sauScanner_ungetc()
 * if the characters do not match; characters skipped or changed by the
 * filtering remain skipped or changed for future gets.
 *
 * \return true if character matched \p testc
 */
bool sauScanner_tryc(sauScanner *restrict o, uint8_t testc) {
	sauFile *f = o->f;
	uint8_t c = sauFile_RETC(f);
	sauScanFilter_f filter_f = sauScanner_getfilter(o, c);
	if (!filter_f) {
		if (c != testc)
			return false;
		pre_get_setup(o);
		++o->sf.char_num;
		sauFile_INCP(f);
		o->sf.c = c;
		prepare_frame(o);
	} else {
		c = sauScanner_filterc(o, c, filter_f);
		if (c != testc) {
			sauScanner_ungetc(o);
			return false;
		}
	}
	return true;
}

/**
 * Unget one character and jump to the previous scan frame.
 * The next get will jump back and begin with the last character got.
 *
 * The scan position is assigned from the undo buffer, with up to
 * SAU_SCAN_UNGET_MAX ungets allowed in a row.
 *
 * Allows revisiting a character using a different scanning method.
 *
 * \return number of unget
 */
uint32_t sauScanner_ungetc(sauScanner *restrict o) {
	if (o->undo_ungets >= SAU_SCAN_UNGET_MAX) {
		sau_error("scanner",
"Unget function called >%d times in a row; return without action",
			SAU_SCAN_UNGET_MAX);
		return o->undo_ungets;
	}
	++o->undo_ungets;
	o->s_flags &= ~SAU_SCAN_S_REGOT;
	change_frame(o, -1);
	sauFile_DECP(o->f);
	char safe_c = o->undo[o->undo_pos].c;
	set_usedc(o, safe_c); /* re-getting past skipped comments now safe */
	return o->undo_ungets;
}

/**
 * Read 32-bit signed integer into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool sauScanner_geti(sauScanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	sauFile *f = o->f;
	size_t read_len;
	pre_get_setup(o);
	o->sf.c = sauFile_RETC(f);
	bool truncated = !sauFile_geti(f, var, allow_sign, &read_len);
	if (read_len == 0) {
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		sauScanner_warning(o, NULL,
			"value truncated, too large for signed 32-bit int");
	}
	advance_frame(o, read_len, 1, sauFile_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * If \p numconst_f is not NULL, then after possibly reading a numerical
 * sign, it will be tried, the normal number reading used as fallback if
 * zero returned.
 *
 * \return true unless number too large and result truncated
 */
bool sauScanner_getd(sauScanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		sauScanNumConst_f numconst_f) {
	sauFile *f = o->f;
	uint8_t c;
	bool sign = false, minus = false;
	size_t read_len;
	pre_get_setup(o);
	o->sf.c = c = sauFile_RETC(f);
	/*
	 * Handle sign here to allow it before named constant too.
	 * Otherwise the same behavior as sauFile_getd().
	 */
	if (allow_sign && (c == '+' || c == '-')) {
		sauFile_INCP(f);
		if (c == '-') minus = true;
		c = sauFile_RETC(f);
		sign = true;
	}
	bool truncated;
	if (numconst_f && (read_len = numconst_f(o, var)) > 0) {
		truncated = false;
	} else {
		truncated = !sauFile_getd(f, var, false, &read_len);
	}
	if (read_len == 0) {
		if (sign)
			sauFile_DECP(f);
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		sauScanner_warning(o, NULL,
			"value truncated, too large for 64-bit float");
	}
	if (sign) ++read_len;
	if (minus) *var = - *var;
	advance_frame(o, read_len, 1, sauFile_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
}

/**
 * Get character if alphabetic and not followed by an identifier character.
 *
 * \return character or 0 if not got
 */
uint8_t sauScanner_get_suffc(sauScanner *restrict o) {
	sauFile *f = o->f;
	uint8_t c = sauFile_RETC(f), next_c;
	sauScanFilter_f filter_f = sauScanner_getfilter(o, c);
	if (!filter_f) {
		if (!SAU_IS_ALPHA(c))
			return 0;
		pre_get_setup(o);
		sauFile_INCP(f);
		++o->sf.char_num;
		o->sf.c = c;
		prepare_frame(o);
	} else {
		c = sauScanner_filterc(o, c, filter_f);
		if (!SAU_IS_ALPHA(c)) goto UNGET_C;
	}
	next_c = sauScanner_retc(o);
	if (sau_is_symchar(next_c)) goto UNGET_C;
	return c;

UNGET_C:
	sauScanner_ungetc(o);
	return 0;
}

/**
 * Get identifier string. If a valid symbol string was read,
 * then \a symstr will be set to the unique item
 * stored in the symbol table, otherwise to NULL.
 *
 * \return true if string was short enough to be read in full
 */
bool sauScanner_get_symstr(sauScanner *restrict o,
		sauSymstr **restrict symstrp) {
	sauFile *f = o->f;
	size_t len;
	bool truncated;
	pre_get_setup(o);
	o->sf.c = sauFile_RETC(f);
	truncated = !read_symstr(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		*symstrp = NULL;
		return true;
	}

	size_t read_len = len;
	if (truncated) {
		sauScanner_warning(o, NULL,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		read_len += sauFile_skipstr(f, filter_symchar);
	}
	advance_frame(o, read_len, 1, sauFile_RETC_NC(f));

	sauSymstr *symstr = sauSymtab_get_symstr(o->symtab, o->strbuf, len);
	if (!symstr) {
		sauScanner_error(o, NULL, "failed to register string '%s'",
				o->strbuf);
	}
	*symstrp = symstr;
	return !truncated;
}

/**
 * Skip whitespace before next character retrieved, as
 * if the filtering uses sauScanner_filter_ws_none().
 *
 * Calling this before another get or return function is
 * an alternative to using the \a SAU_SCAN_WS_NONE level
 * of filtering, and does nothing if that level is used.
 *
 * \return character up next after filtering
 */
uint8_t sauScanner_skipws(sauScanner *restrict o) {
	uint8_t c = sauScanner_retc(o);
	if (c == SAU_SCAN_SPACE || c == SAU_SCAN_LNBRK) {
		c = sauScanner_filterc(o, c, sauScanner_filter_ws_none);
		sauScanner_ungetc(o);
	}
	return c;
}

static sauNoinline void print_stderr(const sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict prefix, const char *restrict fmt,
		va_list ap) {
	sauFile *f = o->f;
	if (!sf) sf = &o->sf;
	if (sf != NULL && !(sf == &o->sf && sauFile_AFTER_EOF(f))) {
		fprintf(stderr, "%s:%d:%d: ",
				f->path, sf->line_num, sf->char_num);
	} else {
		fprintf(stderr, "%s: ", f->path);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning-like message without a "warning" or
 * "error" prefix, including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 */
void sauScanner_notice(const sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, sf, NULL, fmt, ap);
	va_end(ap);
}

/**
 * Print warning message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 */
void sauScanner_warning(const sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	if (o->s_flags & SAU_SCAN_S_QUIET)
		return;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, sf, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 *
 * Sets the scanner state error flag.
 */
void sauScanner_error(sauScanner *restrict o,
		const sauScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	o->s_flags |= SAU_SCAN_S_ERROR;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, sf, "error", fmt, ap);
	va_end(ap);
}

/**
 * Print warning message including file path and position.
 * Uses \p got_at for information from unget buffer at relative position:
 * 0 the current, -1 the previus, +1 the next if ungot, etc.
 */
void sauScanner_warning_at(const sauScanner *restrict o,
		int got_at, const char *restrict fmt, ...) {
	if (o->s_flags & SAU_SCAN_S_QUIET)
		return;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, &o->undo[(o->undo_pos + got_at) & SAU_SCAN_UNGET_MAX],
		"warning", fmt, ap);
	va_end(ap);
}

/**
 * Print warning message including file path and position.
 * Uses \p got_at for information from unget buffer at relative position:
 * 0 the current, -1 the previus, +1 the next if ungot, etc.
 */
void sauScanner_error_at(sauScanner *restrict o,
		int got_at, const char *restrict fmt, ...) {
	o->s_flags |= SAU_SCAN_S_ERROR;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, &o->undo[(o->undo_pos + got_at) & SAU_SCAN_UNGET_MAX],
		"error", fmt, ap);
	va_end(ap);
}
