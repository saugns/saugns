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

#include "scanner.h"
#include "../math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STRBUF_LEN 256

#if SGS_SCANNER_STATS
static size_t hits = 0;
static size_t misses = 0;
#endif

/**
 * Create instance.
 *
 * Assigns a modifiable copy of the SGS_Scanner_def_filters array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SGS_Scanner *SGS_create_Scanner(SGS_SymTab *restrict symtab) {
	if (!symtab)
		return NULL;
	SGS_Scanner *o = calloc(1, sizeof(SGS_Scanner));
	if (!o)
		return NULL;
	o->f = SGS_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t filters_size = sizeof(SGS_ScanFilter_f) * SGS_SCAN_FILTER_COUNT;
	o->filters = SGS_memdup(SGS_Scanner_def_filters, filters_size);
	if (!o->filters) goto ERROR;
	o->strbuf = calloc(1, STRBUF_LEN);
	if (!o->strbuf) goto ERROR;
	return o;
ERROR:
	SGS_destroy_Scanner(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Scanner(SGS_Scanner *restrict o) {
	if (!o)
		return;
#if SGS_SCANNER_STATS
	printf("hits: %zd\nmisses: %zd\n", hits, misses);
#endif
	SGS_destroy_File(o->f);
	free(o->strbuf);
	free(o->filters);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around SGS_File functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool SGS_Scanner_open(SGS_Scanner *restrict o,
		const char *restrict script, bool is_path) {
	if (!is_path) {
		SGS_File_stropenrb(o->f, "<string>", script);
	} else if (!SGS_File_fopenrb(o->f, script)) {
		SGS_error(NULL,
"couldn't open script file \"%s\" for reading", script);
		return false;
	}

	o->sf.line_num = 1; // not increased upon first read
	o->sf.char_num = 0;
	o->s_flags |= SGS_SCAN_S_DISCARD;
	return true;
}

/**
 * Close file (if open).
 */
void SGS_Scanner_close(SGS_Scanner *restrict o) {
	SGS_File_close(o->f);
}

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

static uint8_t filter_symchar(SGS_File *restrict o sgsMaybeUnused,
		uint8_t c) {
	return IS_SYMCHAR(c) ? c : 0;
}

/*
 * Read identifier string into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be used to set the string length.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
static bool read_symstr(SGS_File *restrict f,
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
		uint8_t c = SGS_File_GETC(f);
		if (!IS_SYMCHAR(c)) {
			SGS_File_DECP(f);
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
 * Checks file status, returning SGS_SCAN_EOF if the file has ended,
 * and printing a warning upon error.
 *
 * \return 0 or SGS_SCAN_EOF
 */
uint8_t SGS_Scanner_filter_invalid(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	if (!SGS_File_AFTER_EOF(f)) {
		SGS_Scanner_warning(o, NULL,
			"invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = SGS_File_STATUS(f);
	if ((status & SGS_FILE_ERROR) != 0) {
		SGS_Scanner_error(o, NULL,
			"file reading failed");
	}
	return SGS_SCAN_EOF;
}

/**
 * Get characters until the next is neither a space nor a tab.
 *
 * \return SGS_SCAN_SPACE
 */
uint8_t SGS_Scanner_filter_space(SGS_Scanner *restrict o,
		uint8_t c sgsMaybeUnused) {
	SGS_File *f = o->f;
	o->sf.char_num += SGS_File_skipspace(f);
	return SGS_SCAN_SPACE;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return SGS_SCAN_LNBRK
 */
uint8_t SGS_Scanner_filter_linebreaks(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	if (c == '\n') SGS_File_TRYC(f, '\r');
	while (SGS_File_trynewline(f)) {
		++o->sf.line_num;
		o->sf.char_num = 0;
	}
	return SGS_SCAN_LNBRK;
}

/**
 * Get characters until the next character ends the line (or file).
 *
 * Call for a character to use it as a line comment opener.
 *
 * \return SGS_SCAN_SPACE
 */
uint8_t SGS_Scanner_filter_linecomment(SGS_Scanner *restrict o,
		uint8_t c sgsMaybeUnused) {
	SGS_File *f = o->f;
	o->sf.char_num += SGS_File_skipline(f);
	return SGS_SCAN_SPACE;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. SGS_Scanner_filter_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the linebreak flag. Linebreaks within a block comment
 * are ignored (commented out), apart from in line numbering.
 *
 * \return SGS_SCAN_SPACE or SGS_SCAN_EOF (on unterminated comment)
 */
uint8_t SGS_Scanner_filter_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c) {
	SGS_File *f = o->f;
	int32_t line_num = o->sf.line_num;
	int32_t char_num = o->sf.char_num;
	for (;;) {
		uint8_t c = SGS_File_GETC(f);
		++char_num;
		if (c == '\n') {
			++line_num;
			char_num = 0;
			SGS_File_TRYC(f, '\r');
		} else if (c == '\r') {
			++line_num;
			char_num = 0;
		} else if (c == check_c) {
			if (SGS_File_TRYC(f, o->match_c)) {
				++char_num;
				break; /* end of block comment */
			}
		} else if (c <= SGS_FILE_MARKER && SGS_File_AFTER_EOF(f)) {
			c = SGS_Scanner_filter_invalid(o, c);
			o->sf.c_flags |= SGS_SCAN_C_ERROR;
			--o->sf.char_num; // print for beginning of comment
			SGS_Scanner_error(o, NULL, "unterminated comment");
			++o->sf.char_num;
			return SGS_SCAN_EOF;
		}
	}
	o->sf.line_num = line_num;
	o->sf.char_num = char_num;
	return SGS_SCAN_SPACE;
}

/**
 * Use for '/' (slash) to handle C-style and C++-style comments.
 *
 * Checks the next character for C-style or C++-style comment opener,
 * handling comment if present, otherwise simply returning the first
 * character.
 *
 * \return \p c, SGS_SCAN_SPACE, or SGS_SCAN_EOF (on unterminated comment)
 */
uint8_t SGS_Scanner_filter_slashcomments(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	uint8_t next_c = SGS_File_GETC(f);
	if (next_c == '*') {
		++o->sf.char_num;
		o->match_c = '/';
		return SGS_Scanner_filter_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->sf.char_num;
		return SGS_Scanner_filter_linecomment(o, next_c);
	}
	SGS_File_DECP(f);
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
 * \return \p c or SGS_SCAN_SPACE
 */
uint8_t SGS_Scanner_filter_char1comments(SGS_Scanner *restrict o, uint8_t c) {
	if (o->sf.char_num == 1)
		return SGS_Scanner_filter_linecomment(o, c);
	return c;
}

/**
 * Default array of character filter functions for SGS_Scanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const SGS_ScanFilter_f SGS_Scanner_def_filters[SGS_SCAN_FILTER_COUNT] = {
	/* NUL 0x00 */ SGS_Scanner_filter_invalid, // also for values above 127
	/* SOH 0x01 */ SGS_Scanner_filter_invalid,
	/* STX 0x02 */ SGS_Scanner_filter_invalid,
	/* ETX 0x03 */ SGS_Scanner_filter_invalid,
	/* EOT 0x04 */ SGS_Scanner_filter_invalid,
	/* ENQ 0x05 */ SGS_Scanner_filter_invalid,
	/* ACK 0x06 */ SGS_Scanner_filter_invalid,
	/* BEL '\a' */ SGS_Scanner_filter_invalid, // SGS_FILE_MARKER
	/* BS  '\b' */ SGS_Scanner_filter_invalid,
	/* HT  '\t' */ SGS_Scanner_filter_space,
	/* LF  '\n' */ SGS_Scanner_filter_linebreaks,
	/* VT  '\v' */ SGS_Scanner_filter_invalid,
	/* FF  '\f' */ SGS_Scanner_filter_invalid,
	/* CR  '\r' */ SGS_Scanner_filter_linebreaks,
	/* SO  0x0E */ SGS_Scanner_filter_invalid,
	/* SI  0x0F */ SGS_Scanner_filter_invalid,
	/* DLE 0x10 */ SGS_Scanner_filter_invalid,
	/* DC1 0x11 */ SGS_Scanner_filter_invalid,
	/* DC2 0x12 */ SGS_Scanner_filter_invalid,
	/* DC3 0x13 */ SGS_Scanner_filter_invalid,
	/* DC4 0x14 */ SGS_Scanner_filter_invalid,
	/* NAK 0x15 */ SGS_Scanner_filter_invalid,
	/* SYN 0x16 */ SGS_Scanner_filter_invalid,
	/* ETB 0x17 */ SGS_Scanner_filter_invalid,
	/* CAN 0x18 */ SGS_Scanner_filter_invalid,
	/* EM  0x19 */ SGS_Scanner_filter_invalid,
	/* SUB 0x1A */ SGS_Scanner_filter_invalid,
	/* ESC 0x1B */ SGS_Scanner_filter_invalid,
	/* FS  0x1C */ SGS_Scanner_filter_invalid,
	/* GS  0x1D */ SGS_Scanner_filter_invalid,
	/* RS  0x1E */ SGS_Scanner_filter_invalid,
	/* US  0x1F */ SGS_Scanner_filter_invalid,
	/*     ' '  */ SGS_Scanner_filter_space,
	/*     '!'  */ NULL,
	/*     '"'  */ NULL,
	/*     '#'  */ SGS_Scanner_filter_linecomment,
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
	/*     '/'  */ SGS_Scanner_filter_slashcomments,
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
	/* DEL 0x7F */ SGS_Scanner_filter_invalid,
};

/*
 * Assign scan frame from undo buffer.
 */
static void restore_frame(SGS_Scanner *restrict o, uint32_t offset) {
	uint32_t i = (o->undo_pos - offset) & SGS_SCAN_UNGET_MAX;
	o->sf = o->undo[i];
}

/*
 * Perform pending updates before a get call.
 */
static void prepare_frame(SGS_Scanner *restrict o) {
	if (o->unget_num > 0) {
		/*
		 * Start from frame after the one ungotten to.
		 */
		restore_frame(o, --o->unget_num);
		return;
	}
	if ((o->s_flags & SGS_SCAN_S_DISCARD) != 0) {
		o->s_flags &= ~SGS_SCAN_S_DISCARD;
	} else {
		o->undo_pos = (o->undo_pos + 1) & SGS_SCAN_UNGET_MAX;
	}
	o->undo[o->undo_pos] = o->sf;
	if ((o->sf.c_flags & SGS_SCAN_C_LNBRK) != 0) {
		o->sf.c_flags &= ~SGS_SCAN_C_LNBRK;
		++o->sf.line_num;
		o->sf.char_num = 0;
	}
}

/*
 * Set character used after filtering.
 *
 * Sets the file buffer character before the current to \p c,
 * so that a new get after an undo arrives at \p c.
 */
static void set_usedc(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	size_t r_pos = f->pos;
	o->sf.c = c;
	SGS_File_DECP(f);
	SGS_File_FIXP(f);
	SGS_File_SETC_NC(f, c);
	f->pos = r_pos;
}

/*
 * Perform updates after reading a sequence of characters,
 * e.g. a string or number. Prepares a temporary post-get scan frame.
 */
static void advance_frame(SGS_Scanner *o, size_t strlen, uint8_t c) {
	if (strlen == 0)
		return;
	uint32_t reget_count = (strlen > o->unget_num) ?
		o->unget_num :
		strlen;
	uint32_t char_inc = strlen;
	if (reget_count > 0) {
		/*
		 * Advance past ungets prior to frame to restore to.
		 */
		o->unget_num -= (reget_count - 1);
	}
	prepare_frame(o);
	o->sf.char_num += char_inc;
	o->sf.c = c;
	o->s_flags |= SGS_SCAN_S_DISCARD;
}

/**
 * Get next character. Reduces whitespace, returning one space marker
 * for spaces, tabs, and/or comments, and one linebreak marker
 * for linebreaks.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character or 0 upon end of file
 */
uint8_t SGS_Scanner_getc(SGS_Scanner *restrict o) {
	SGS_File *f = o->f;
	uint8_t c;
	bool skipped_space = false;
	prepare_frame(o);
	for (;;) {
		++o->sf.char_num;
		c = SGS_File_GETC(f);
		SGS_ScanFilter_f filter = SGS_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SGS_SCAN_SPACE) {
			skipped_space = true;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SGS_SCAN_EOF)
		return 0;
	set_usedc(o, c);
	if (skipped_space) {
		/*
		 * Unget a character and store skipped space
		 * before returning it.
		 */
		SGS_File_UNGETC(f);
		--o->sf.char_num;
		set_usedc(o, SGS_SCAN_SPACE);
		return SGS_SCAN_SPACE;
	}
	if (c == SGS_SCAN_LNBRK) {
		o->sf.c_flags |= SGS_SCAN_C_LNBRK;
	}
	return c;
}

/**
 * Get next character. Removes whitespace, except for a single linebreak
 * marker if linebreaks were filtered.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character or 0 upon end of file
 */
uint8_t SGS_Scanner_getc_nospace(SGS_Scanner *restrict o) {
	SGS_File *f = o->f;
	uint8_t c;
	SGS_ScanFilter_f filter;
	bool skipped_lnbrk = false;
	int32_t old_char_num;
	prepare_frame(o);
	for (;;) {
		++o->sf.char_num;
		c = SGS_File_GETC(f);
		filter = SGS_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SGS_SCAN_SPACE) continue;
		if (c == SGS_SCAN_LNBRK) {
			skipped_lnbrk = true;
			old_char_num = o->sf.char_num;
			++o->sf.line_num;
			o->sf.char_num = 0;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SGS_SCAN_EOF)
		return 0;
	set_usedc(o, c);
	if (skipped_lnbrk) {
		/*
		 * Unget a character and store skipped linebreak
		 * before returning it.
		 */
		SGS_File_UNGETC(f);
		--o->sf.line_num;
		o->sf.char_num = old_char_num;
		o->sf.c_flags |= SGS_SCAN_C_LNBRK;
		set_usedc(o, SGS_SCAN_LNBRK);
		return SGS_SCAN_LNBRK;
	}
	return c;
}

/**
 * Get next character if it matches \p testc,
 * filtering whitespace like SGS_Scanner_getc().
 *
 * For filtered characters, does a get followed by SGS_Scanner_ungetc()
 * if the characters do not match; characters skipped or changed by the
 * filtering remain skipped or changed for future gets.
 *
 * \return true if character matched \p testc
 */
bool SGS_Scanner_tryc(SGS_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SGS_File_RETC(o->f);
	/*
	 * Use quick handling for unfiltered characters.
	 */
	if (!SGS_Scanner_getfilter(o, c)) {
		if (c != testc)
			return false;
		prepare_frame(o);
		++o->sf.char_num;
		SGS_File_INCP(o->f);
		o->sf.c = c;
		return true;
	}
	c = SGS_Scanner_getc(o);
	if (c != testc) {
		o->s_flags |= SGS_SCAN_S_DISCARD;
		SGS_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Get next character if it matches \p testc,
 * filtering whitespace like SGS_Scanner_getc_nospace().
 *
 * For filtered characters, does a get followed by SGS_Scanner_ungetc()
 * if the characters do not match; characters skipped or changed by the
 * filtering remain skipped or changed for future gets.
 *
 * \return true if character matched \p testc
 */
bool SGS_Scanner_tryc_nospace(SGS_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SGS_File_RETC(o->f);
	/*
	 * Use quick handling for unfiltered characters.
	 */
	if (!SGS_Scanner_getfilter(o, c)) {
		if (c != testc)
			return false;
		prepare_frame(o);
		++o->sf.char_num;
		SGS_File_INCP(o->f);
		o->sf.c = c;
		return true;
	}
	c = SGS_Scanner_getc_nospace(o);
	if (c != testc) {
		o->s_flags |= SGS_SCAN_S_DISCARD;
		SGS_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Unget one character and jump to the previous scan frame.
 * The next get will jump back and begin with the last character got.
 *
 * The scan position is assigned from the undo buffer, with up to
 * SGS_SCAN_UNGET_MAX ungets allowed in a row.
 *
 * Allows revisiting a character using a different scanning method.
 *
 * \return number of unget
 */
uint32_t SGS_Scanner_ungetc(SGS_Scanner *restrict o) {
	if (o->unget_num >= SGS_SCAN_UNGET_MAX) {
		SGS_error("scanner",
"Unget function called >%d times in a row; return without action",
			SGS_SCAN_UNGET_MAX);
		return o->unget_num;
	}
	restore_frame(o, ++o->unget_num);
	SGS_File *f = o->f;
	SGS_File_UNGETC(f);
	set_usedc(o, o->sf.c);
	return o->unget_num;
}

/**
 * Read 32-bit signed integer into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SGS_Scanner_geti(SGS_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	SGS_File *f = o->f;
	size_t read_len;
	prepare_frame(o);
	o->sf.c = SGS_File_RETC(f);
	++o->sf.char_num;
	bool truncated = !SGS_File_geti(f, var, allow_sign, &read_len);
	if (read_len == 0) {
		o->s_flags |= SGS_SCAN_S_DISCARD;
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		SGS_Scanner_warning(o, NULL,
			"value truncated, too large for signed 32-bit int");
	}
	advance_frame(o, read_len - 1, SGS_File_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SGS_Scanner_getd(SGS_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	SGS_File *f = o->f;
	size_t read_len;
	prepare_frame(o);
	o->sf.c = SGS_File_RETC(f);
	++o->sf.char_num;
	bool truncated = !SGS_File_getd(f, var, allow_sign, &read_len);
	if (read_len == 0) {
		o->s_flags |= SGS_SCAN_S_DISCARD;
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		SGS_Scanner_warning(o, NULL,
			"value truncated, too large for 64-bit float");
	}
	advance_frame(o, read_len - 1, SGS_File_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
}

/**
 * Get identifier string. If a valid symbol string was read,
 * the copy set to \p strp will be the unique copy stored
 * in the symbol table. If no string was read,
 * \p strp will be set to NULL.
 *
 * If \p lenp is not NULL, it will be used to set the string length.
 *
 * \return true if string was short enough to be read in full
 */
bool SGS_Scanner_getsymstr(SGS_Scanner *restrict o,
		const void **restrict strp, size_t *restrict lenp) {
	SGS_File *f = o->f;
	size_t len;
	bool truncated;
	prepare_frame(o);
	o->sf.c = SGS_File_RETC(f);
	++o->sf.char_num;
	truncated = !read_symstr(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		o->s_flags |= SGS_SCAN_S_DISCARD;
		if (lenp) *lenp = 0;
		return true;
	}

	size_t read_len = len;
	if (truncated) {
		SGS_Scanner_warning(o, NULL,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		read_len += SGS_File_skipstr(f, filter_symchar);
	}
	advance_frame(o, read_len - 1, SGS_File_RETC_NC(f));

	const char *pool_str;
	pool_str = SGS_SymTab_pool_str(o->symtab, o->strbuf, len);
	if (!pool_str) {
		SGS_Scanner_error(o, NULL, "failed to register string '%s'",
				o->strbuf);
	}
	*strp = pool_str;
	if (lenp) *lenp = len;
	return !truncated;
}

static void print_stderr(const SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict prefix, const char *restrict fmt,
		va_list ap) {
	SGS_File *f = o->f;
	if (sf != NULL) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, sf->line_num, sf->char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 */
void SGS_Scanner_warning(const SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	if ((o->s_flags & SGS_SCAN_S_QUIET) != 0)
		return;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, (sf != NULL ? sf : &o->sf),
		"warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 *
 * Sets the scanner state error flag.
 */
void SGS_Scanner_error(SGS_Scanner *restrict o,
		const SGS_ScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, (sf != NULL ? sf : &o->sf),
		"error", fmt, ap);
	o->s_flags |= SGS_SCAN_S_ERROR;
	va_end(ap);
}
