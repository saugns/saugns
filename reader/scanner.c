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

/**
 * Create instance.
 *
 * Assigns a modifiable copy of the SGS_Scanner_def_c_filters array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SGS_Scanner *SGS_create_Scanner(SGS_SymTab *restrict symtab) {
	if (!symtab) return NULL;

	SGS_Scanner *o = calloc(1, sizeof(SGS_Scanner));
	if (!o) return NULL;
	o->f = SGS_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t filters_size = sizeof(SGS_ScanCFilter_f) * SGS_SCAN_CFILTERS;
	o->c_filters = SGS_memdup(SGS_Scanner_def_c_filters, filters_size);
	if (!o->c_filters) goto ERROR;
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
	SGS_destroy_File(o->f);
	free(o->strbuf);
	free(o->c_filters);
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

	o->cf.line_num = 1; // not increased upon first read
	o->cf.char_num = 0;
	return true;
}

/**
 * Close file (if open).
 */
void SGS_Scanner_close(SGS_Scanner *restrict o) {
	SGS_File_close(o->f);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Scanner *restrict o,
		uint32_t options, const char *restrict prefix,
		const char *restrict fmt, va_list ap) {
	SGS_File *f = o->f;
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, o->cf.line_num, o->cf.char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file path and current position.
 */
void SGS_Scanner_warning(SGS_Scanner *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file path and current position.
 *
 * Sets the scanner state error flag.
 */
void SGS_Scanner_error(SGS_Scanner *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	o->s_flags |= SGS_SCAN_S_ERROR;
	va_end(ap);
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
		SGS_Scanner_warning(o, "invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = SGS_File_STATUS(f);
	if ((status & SGS_FILE_ERROR) != 0) {
		SGS_Scanner_error(o, "file reading failed");
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
	o->cf.char_num += SGS_File_skipspace(f);
	return SGS_SCAN_SPACE;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return SGS_SCAN_LNBRK
 */
uint8_t SGS_Scanner_filter_linebreaks(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	++o->cf.line_num;
	if (c == '\n') SGS_File_TRYC(f, '\r');
	while (SGS_File_trynewline(f)) {
		++o->cf.line_num;
	}
	o->cf.char_num = 0;
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
	o->cf.char_num += SGS_File_skipline(f);
	return SGS_SCAN_SPACE;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. SGS_Scanner_filter_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the newline field, even if the comment contains newlines;
 * any newlines within a block comment are ignored (commented out), apart
 * from in line numbering.
 *
 * \return SGS_SCAN_SPACE or SGS_SCAN_EOF (on unterminated comment)
 */
uint8_t SGS_Scanner_filter_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c) {
	SGS_File *f = o->f;
	int32_t line_num = o->cf.line_num;
	int32_t char_num = o->cf.char_num;
	for (;;) {
		uint8_t c = SGS_File_GETC(f);
		++char_num;
		if (c == '\n') {
			++line_num;
			SGS_File_TRYC(f, '\r');
			char_num = 0;
		} else if (c == '\r') {
			++line_num;
			char_num = 0;
		} else if (c == check_c) {
			c = SGS_File_GETC(f);
			if (c == o->cf.match_c) {
				++char_num;
				break; /* end of block comment */
			} else {
				SGS_File_DECP(f);
			}
		} else if (c <= SGS_FILE_MARKER && SGS_File_AFTER_EOF(f)) {
			c = SGS_Scanner_filter_invalid(o, c);
			o->cf.flags |= SGS_SCAN_C_ERROR;
			--o->cf.char_num; // print for beginning of comment
			SGS_Scanner_error(o, "unterminated comment");
			++o->cf.char_num;
			return SGS_SCAN_EOF;
		}
	}
	o->cf.line_num = line_num;
	o->cf.char_num = char_num;
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
		++o->cf.char_num;
		o->cf.match_c = '/';
		return SGS_Scanner_filter_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->cf.char_num;
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
	if (o->cf.char_num == 1) return SGS_Scanner_filter_linecomment(o, c);
	return c;
}

/**
 * Default array of character filter functions for SGS_Scanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const SGS_ScanCFilter_f SGS_Scanner_def_c_filters[SGS_SCAN_CFILTERS] = {
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
 * Perform pending updates before a get call.
 */
static void update_cframe(SGS_Scanner *restrict o) {
	if ((o->cf.flags & SGS_SCAN_C_LNBRK) != 0) {
		++o->cf.line_num;
		o->cf.char_num = 0;
		o->cf.flags &= ~SGS_SCAN_C_LNBRK;
	}
	o->s_flags &= ~SGS_SCAN_S_UNGETC;
}

/*
 * Set previous character to \p c.
 *
 * Used to make getting after an undo use the character.
 */
static void set_prevc(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	size_t r_pos = f->pos;
	SGS_File_DECP(f);
	SGS_File_FIXP(f);
	SGS_File_SETC_NC(f, c);
	f->pos = r_pos;
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
	update_cframe(o);
	for (;;) {
		++o->cf.char_num;
		c = SGS_File_GETC(f);
		SGS_ScanCFilter_f filter = SGS_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SGS_SCAN_SPACE) {
			skipped_space = true;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SGS_SCAN_EOF) return 0;
	set_prevc(o, c);
	if (skipped_space) {
		/*
		 * Unget a character and store skipped space
		 * before returning it.
		 */
		SGS_File_UNGETC(f);
		--o->cf.char_num;
		set_prevc(o, SGS_SCAN_SPACE);
		return SGS_SCAN_SPACE;
	}
	if (c == SGS_SCAN_LNBRK) {
		o->cf.flags |= SGS_SCAN_C_LNBRK;
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
	SGS_ScanCFilter_f filter;
	int32_t old_char_num;
	bool skipped_lnbrk = false;
	update_cframe(o);
	for (;;) {
		++o->cf.char_num;
		c = SGS_File_GETC(f);
		filter = SGS_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SGS_SCAN_SPACE) continue;
		if (c == SGS_SCAN_LNBRK) {
			skipped_lnbrk = true;
			old_char_num = o->cf.char_num;
			++o->cf.line_num;
			o->cf.char_num = 0;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SGS_SCAN_EOF) return 0;
	set_prevc(o, c);
	if (skipped_lnbrk) {
		/*
		 * Unget a character and store skipped linebreak
		 * before returning it.
		 */
		SGS_File_UNGETC(f);
		--o->cf.line_num;
		o->cf.char_num = old_char_num;
		set_prevc(o, SGS_SCAN_LNBRK);
		return SGS_SCAN_LNBRK;
	}
	return c;
}

/**
 * Unget the last character read. This only moves the reading position
 * back one step; any skipped characters (whitespace, etc.) will not be
 * processed again.
 *
 * Useful after getting and examining a character and deciding on a
 * different scanning method, e.g. reading a string.
 *
 * Only meant to be called once in a row; an error is printed without
 * further action if called several times in succession. (Allowing
 * several character ungets would risk parsing errors when moving back
 * past syntactic end markers.)
 */
void SGS_Scanner_ungetc(SGS_Scanner *restrict o) {
	if ((o->s_flags & SGS_SCAN_S_UNGETC) != 0) {
		SGS_Scanner_error(o, "scanner ungetc repeated; return without action");
		return;
	}
	SGS_File *f = o->f;
	SGS_File_UNGETC(f);
	--o->cf.char_num;
	o->s_flags |= SGS_SCAN_S_UNGETC;
}

/**
 * Get next character (filtering whitespace, etc.) if it matches \p testc.
 *
 * Calls SGS_Scanner_ungetc() and returns false if the characters do not
 * match, meaning a new get or try will immediately arrive at the same
 * character. Note that SGS_Scanner_ungetc() cannot be called multiple
 * times in a row, so if false is returned, do not make a direct call to
 * it before further scanning is done.
 *
 * \return true if character matched \p testc
 */
bool SGS_Scanner_tryc(SGS_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SGS_Scanner_getc(o);
	if (c != testc) {
		SGS_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Get identifier string. If a valid symbol string was read,
 * the copy set to \p strp will be the unique copy stored
 * in the symbol table. If no string was read,
 * \p strp will be set to NULL.
 *
 * If not NULL, \p lenp will be set to the length of the string.
 *
 * \return true if string was short enough to be read in full
 */
bool SGS_Scanner_getsymstr(SGS_Scanner *restrict o,
		const void **restrict strp, size_t *restrict lenp) {
	SGS_File *f = o->f;
	size_t len;
	bool truncated;
	update_cframe(o);
	SGS_File_DECP(f);
	truncated = !read_symstr(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		*strp = NULL;
		if (lenp) *lenp = 0;
		return true;
	}
	o->cf.char_num += len - 1;
	if (truncated) {
		SGS_Scanner_warning(o,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		o->cf.char_num += SGS_File_skipstr(f, filter_symchar);
	}
	SGS_SymStr *s = SGS_SymTab_get_symstr(o->symtab, o->strbuf, len);
	if (s == NULL) {
		SGS_Scanner_error(o, "failed to register string '%s'",
				o->strbuf);
	}
	*strp = s->key;
	if (lenp) *lenp = len;
	return !truncated;
}
