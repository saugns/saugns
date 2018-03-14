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
 * Assigns a modifiable copy of the SGS_Scanner_def_c_handlers array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SGS_Scanner *SGS_create_Scanner(SGS_SymTab *restrict symtab) {
	if (!symtab) return NULL;

	SGS_Scanner *o = calloc(1, sizeof(SGS_Scanner));
	o->f = SGS_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t handlers_size = sizeof(SGS_ScannerCHandler_f) * SGS_SCAN_CHVALS;
	o->c_handlers = SGS_memdup(SGS_Scanner_def_c_handlers, handlers_size);
	if (!o->c_handlers) goto ERROR;
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
	if (o->strbuf) free(o->strbuf);
	if (o->c_handlers) free(o->c_handlers);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around SGS_File_fopenrb() which also resets file-specific
 * scanner state.
 *
 * \return true on success
 */
bool SGS_Scanner_fopenrb(SGS_Scanner *restrict o, const char *restrict path) {
	SGS_File *f = o->f;
	if (!SGS_File_fopenrb(f, path)) return false;

	o->line_pos = 1; // not increased upon first read
	o->char_pos = 0;
	return true;
}

/**
 * Close file (if open).
 */
void SGS_Scanner_close(SGS_Scanner *restrict o) {
	SGS_File *f = o->f;
	SGS_File_close(f);
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
			f->path, o->line_pos, o->char_pos);
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

static uint8_t filter_symchar(SGS_File *restrict o SGS__maybe_unused,
		uint8_t c) {
	return IS_SYMCHAR(c) ? c : 0;
}

/*
 * Read identifier string into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always zero-terminated.
 *
 * If \p str_len is not NULL, it will be set to the string length.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
static bool read_syms(SGS_File *restrict f,
		void *restrict buf, uint32_t buf_len,
		uint32_t *restrict str_len) {
	uint8_t *dst = buf;
	uint32_t i = 0;
	uint32_t max_len = buf_len - 1;
	bool truncate = false;
	for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SGS_File_GETC(f);
		if (!IS_SYMCHAR(c)) {
			SGS_FBufMode_DECP(&f->mr);
			break;
		}
		dst[i++] = c;
	}
	dst[i] = '\0';
	if (str_len) *str_len = i;
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
uint8_t SGS_Scanner_handle_invalid(SGS_Scanner *restrict o, uint8_t c) {
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
 * \return 0
 */
uint8_t SGS_Scanner_handle_space(SGS_Scanner *restrict o,
		uint8_t c SGS__maybe_unused) {
	SGS_File *f = o->f;
	o->char_pos += SGS_File_skipspace(f);
	return 0;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return 0
 */
uint8_t SGS_Scanner_handle_linebreaks(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	++o->line_pos;
	if (c == '\n') SGS_File_TRYC(f, '\r');
	while (SGS_File_trynewline(f)) {
		++o->line_pos;
	}
	o->c_flags |= SGS_SCAN_C_NEWLINE;
	o->old_char_pos = o->char_pos;
	o->char_pos = 0;
	return 0;
}

/**
 * Get characters until the next character ends the line (or file).
 *
 * Call for a character to use it as a line comment opener.
 *
 * \return 0
 */
uint8_t SGS_Scanner_handle_linecomment(SGS_Scanner *restrict o,
		uint8_t c SGS__maybe_unused) {
	SGS_File *f = o->f;
	o->char_pos += SGS_File_skipline(f);
	return 0;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. SGS_Scanner_handle_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the newline field, even if the comment contains newlines;
 * any newlines within a block comment are ignored (commented out), apart
 * from in line numbering.
 *
 * \return 0 or SGS_SCAN_EOF (on unterminated comment)
 */
uint8_t SGS_Scanner_handle_blockcomment(SGS_Scanner *restrict o,
		uint8_t check_c) {
	SGS_File *f = o->f;
	int32_t line_pos = o->line_pos;
	int32_t char_pos = o->char_pos;
	for (;;) {
		uint8_t c = SGS_File_GETC(f);
		++char_pos;
		if (c == '\n') {
			++line_pos;
			SGS_File_TRYC(f, '\r');
			char_pos = 0;
		} else if (c == '\r') {
			++line_pos;
			char_pos = 0;
		} else if (c == check_c) {
			c = SGS_File_GETC(f);
			if (c == o->match_c) {
				++char_pos;
				break; /* end of block comment */
			} else {
				SGS_FBufMode_DECP(&f->mr);
			}
		} else if (c <= SGS_FILE_MARKER && SGS_File_AFTER_EOF(f)) {
			c = SGS_Scanner_handle_invalid(o, c);
			o->c_flags |= SGS_SCAN_C_ERROR;
			--o->char_pos; // print for beginning of comment
			SGS_Scanner_error(o, "unterminated comment");
			++o->char_pos;
			return SGS_SCAN_EOF;
		}
	}
	o->line_pos = line_pos;
	o->char_pos = char_pos;
	return 0;
}

/**
 * Use for '/' (slash) to handle C-style and C++-style comments.
 *
 * Checks the next character for C-style or C++-style comment opener,
 * handling comment if present, otherwise simply returning the first
 * character.
 *
 * \return \p c, 0, or SGS_SCAN_EOF (on unterminated block comment)
 */
uint8_t SGS_Scanner_handle_slashcomments(SGS_Scanner *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	uint8_t next_c = SGS_File_GETC(f);
	if (next_c == '*') {
		++o->char_pos;
		o->match_c = '/';
		return SGS_Scanner_handle_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->char_pos;
		return SGS_Scanner_handle_linecomment(o, next_c);
	}
	SGS_FBufMode_DECP(&f->mr);
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
uint8_t SGS_Scanner_handle_char1comments(SGS_Scanner *restrict o, uint8_t c) {
	if (o->char_pos == 1) return SGS_Scanner_handle_linecomment(o, c);
	return c;
}

/**
 * Default array of character handler functions for SGS_Scanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const SGS_ScannerCHandler_f SGS_Scanner_def_c_handlers[SGS_SCAN_CHVALS] = {
	/* NUL 0x00 */ SGS_Scanner_handle_invalid, // also for values above 127
	/* SOH 0x01 */ SGS_Scanner_handle_invalid,
	/* STX 0x02 */ SGS_Scanner_handle_invalid,
	/* ETX 0x03 */ SGS_Scanner_handle_invalid,
	/* EOT 0x04 */ SGS_Scanner_handle_invalid,
	/* ENQ 0x05 */ SGS_Scanner_handle_invalid,
	/* ACK 0x06 */ SGS_Scanner_handle_invalid,
	/* BEL '\a' */ SGS_Scanner_handle_invalid, // SGS_FILE_MARKER
	/* BS  '\b' */ SGS_Scanner_handle_invalid,
	/* HT  '\t' */ SGS_Scanner_handle_space,
	/* LF  '\n' */ SGS_Scanner_handle_linebreaks,
	/* VT  '\v' */ SGS_Scanner_handle_invalid,
	/* FF  '\f' */ SGS_Scanner_handle_invalid,
	/* CR  '\r' */ SGS_Scanner_handle_linebreaks,
	/* SO  0x0E */ SGS_Scanner_handle_invalid,
	/* SI  0x0F */ SGS_Scanner_handle_invalid,
	/* DLE 0x10 */ SGS_Scanner_handle_invalid,
	/* DC1 0x11 */ SGS_Scanner_handle_invalid,
	/* DC2 0x12 */ SGS_Scanner_handle_invalid,
	/* DC3 0x13 */ SGS_Scanner_handle_invalid,
	/* DC4 0x14 */ SGS_Scanner_handle_invalid,
	/* NAK 0x15 */ SGS_Scanner_handle_invalid,
	/* SYN 0x16 */ SGS_Scanner_handle_invalid,
	/* ETB 0x17 */ SGS_Scanner_handle_invalid,
	/* CAN 0x18 */ SGS_Scanner_handle_invalid,
	/* EM  0x19 */ SGS_Scanner_handle_invalid,
	/* SUB 0x1A */ SGS_Scanner_handle_invalid,
	/* ESC 0x1B */ SGS_Scanner_handle_invalid,
	/* FS  0x1C */ SGS_Scanner_handle_invalid,
	/* GS  0x1D */ SGS_Scanner_handle_invalid,
	/* RS  0x1E */ SGS_Scanner_handle_invalid,
	/* US  0x1F */ SGS_Scanner_handle_invalid,
	/*     ' '  */ SGS_Scanner_handle_space,
	/*     '!'  */ NULL,
	/*     '"'  */ NULL,
	/*     '#'  */ SGS_Scanner_handle_linecomment,
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
	/*     '/'  */ SGS_Scanner_handle_slashcomments,
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
	/* DEL 0x7F */ SGS_Scanner_handle_invalid,
};

#if 0
static bool scan_number(SGS_Scanner *restrict o, uint32_t *restrict num) {
	SGS_File *f = o->f;
	bool error = false;
	uint32_t c = SGS_File_GETC(f);
	uint32_t i = 0;
	do {
		o->strbuf[i] = c;
		c = SGS_File_GETC(f);
	} while (IS_DIGIT(c) && ++i < (STRBUF_LEN - 1));
	if (i == (STRBUF_LEN - 1)) {
		o->strbuf[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->strbuf[i] = '\0';
	}
	SGS_FBufMode_DECP(&f->mr);
	return !error;
}
#endif

/**
 * Get next character (with filtering); remove spaces, tabs,
 * comments, and replace newlines with a single SGS_SCAN_EOL
 * ('\n') character.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character or 0 upon end of file
 */
uint8_t SGS_Scanner_getc(SGS_Scanner *restrict o) {
	SGS_File *f = o->f;
	uint8_t c;
	SGS_ScannerCHandler_f handler;
	if ((o->c_flags & SGS_SCAN_C_NEWLINE) != 0) {
		++o->line_pos;
		o->char_pos = 0;
		o->c_flags &= ~SGS_SCAN_C_NEWLINE;
	}
	do {
		++o->char_pos;
		c = SGS_File_GETC(f);
		handler = SGS_Scanner_get_c_handler(o, c);
		if (!handler) break;
		c = handler(o, c);
	} while (c == 0);
	if (c == SGS_SCAN_EOF) return 0;
	if ((o->c_flags & SGS_SCAN_C_NEWLINE) != 0) {
		/*
		 * Handle greedy scanning past newline characters.
		 * Unget char, set position, and return newline
		 * (also setting preceding char to newline so a
		 * following unget will work).
		 */
		SGS_File_UNGETC(f);
		--o->line_pos;
		o->char_pos = o->old_char_pos;
		c = SGS_SCAN_EOL;
		f->mw.pos = f->mr.pos - 1;
		SGS_FBufMode_FIXP(&f->mw);
		SGS_File_SETC_NC(f, c);
	}
	o->s_flags &= ~SGS_SCAN_S_UNGETC;
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
		SGS_Scanner_error(o, "scanner ungetc repeated by parsing code (return without action)");
		return;
	}
	SGS_File *f = o->f;
	SGS_File_UNGETC(f);
	--o->char_pos;
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
bool SGS_Scanner_getsyms(SGS_Scanner *restrict o,
		const void **restrict strp, uint32_t *restrict lenp) {
	SGS_File *f = o->f;
	uint32_t len;
	SGS_FBufMode_DECP(&f->mr);
	bool truncated = !read_syms(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		*strp = NULL;
		if (lenp) *lenp = 0;
		return true;
	}
	if (truncated) {
		SGS_Scanner_warning(o,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		o->char_pos += SGS_File_skips(f, filter_symchar);
	}
	const char *pool_str;
	pool_str = SGS_SymTab_pool_str(o->symtab, o->strbuf, len);
	if (pool_str == NULL) {
		SGS_Scanner_error(o, "failed to register string '%s'",
				o->strbuf);
	}
	o->char_pos += len - 1;
	*strp = pool_str;
	if (lenp) *lenp = len;
	return !truncated;
}
