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

#include "scanner.h"
#include "math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SCAN_EOF 0xFF

#define STRBUF_LEN 256

/**
 * Create instance. Requires a File instance.
 *
 * Assigns a modifiable copy of the SGS_Scanner_def_c_handlers array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SGS_Scanner *SGS_create_Scanner(SGS_File *f) {
	if (!f) return NULL;

	SGS_Scanner *o = calloc(1, sizeof(SGS_Scanner));
	o->f = f;
	size_t handlers_size = sizeof(SGS_ScannerCHandler_f) * 256;
	o->c_handlers = SGS_memdup(SGS_Scanner_def_c_handlers, handlers_size);
	o->line_pos = 1; // not increased upon first read
	o->strbuf = calloc(1, STRBUF_LEN);
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Scanner(SGS_Scanner *o) {
	free(o->strbuf);
	free(o->c_handlers);
	free(o);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Scanner *o, uint32_t options, const char *prefix,
	const char *fmt, va_list ap) {
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			o->f->path, o->line_pos, o->char_pos);
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
void SGS_Scanner_warning(SGS_Scanner *o, const char *fmt, ...) {
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
void SGS_Scanner_error(SGS_Scanner *o, const char *fmt, ...) {
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

/*
 * Read identifier string into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always zero-terminated.
 *
 * If \p str_len is not NULL, it will be set to the string length.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
static bool read_symstr(SGS_File *o, void *buf, uint32_t buf_len,
		uint32_t *str_len) {
	SGS_CBuf *cb = &o->cb;
	uint8_t *dst = buf;
	uint32_t i = 0;
	uint32_t max_len = buf_len - 1;
	bool truncate = false;
	for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SGS_CBuf_GETC(cb);
		if (!IS_SYMCHAR(c)) {
			SGS_CBufMode_DECP(&cb->r);
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
 * Checks file status, returning SCAN_EOF if the file has ended,
 * and printing a warning upon error.
 *
 * \return 0 or SCAN_EOF
 */
uint8_t SGS_Scanner_handle_invalid(SGS_Scanner *o, uint8_t c) {
	if (!SGS_File_AFTER_EOF(o->f)) {
		SGS_Scanner_warning(o, "invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = SGS_File_STATUS(o->f);
	switch (status) {
		case SGS_File_END:
			break;
		case SGS_File_ERROR:
			SGS_Scanner_error(o, "file reading failed");
			break;
		default: /* shouldn't happen */
			SGS_Scanner_error(o, "file read status 0x%02hhX", status);
			break;
	}
	return SCAN_EOF;
}

/**
 * Get characters until the next is neither a space nor a tab.
 *
 * \return 0
 */
uint8_t SGS_Scanner_handle_blanks(SGS_Scanner *o, uint8_t c SGS__maybe_unused) {
	o->char_pos += SGS_File_skipspace(o->f);
	return 0;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return 0
 */
uint8_t SGS_Scanner_handle_linebreaks(SGS_Scanner *o, uint8_t c) {
	SGS_CBuf *cb = &o->f->cb;
	do {
		++o->line_pos;
		if (c == '\n') SGS_CBuf_TRYC(cb, '\r');
		c = SGS_CBuf_GETC(cb);
	} while (IS_LNBRK(c));
	SGS_CBufMode_DECP(&cb->r);
	o->c_flags |= SGS_SCAN_C_NEWLINE;
	o->old_char_pos = o->char_pos;
	o->char_pos = 0;
	return 0;
}

/**
 * Get characters until the end of the line is reached.
 *
 * Call for a character to use it as a line comment opener.
 *
 * Does not update character position, since a line change follows.
 *
 * \return 0
 */
uint8_t SGS_Scanner_handle_linecomment(SGS_Scanner *o,
		uint8_t c SGS__maybe_unused) {
	SGS_File_skipline(o->f);
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
 * \return 0
 */
uint8_t SGS_Scanner_handle_blockcomment(SGS_Scanner *o, uint8_t check_c) {
	SGS_CBuf *cb = &o->f->cb;
	int32_t line_pos = o->line_pos;
	int32_t char_pos = o->char_pos;
	for (;;) {
		uint8_t c = SGS_CBuf_GETC(cb);
		++char_pos;
		if (c == '\n') {
			++line_pos;
			SGS_CBuf_TRYC(cb, '\r');
			char_pos = 0;
		} else if (c == '\r') {
			++line_pos;
			char_pos = 0;
		} else if (c == check_c) {
			c = SGS_CBuf_GETC(cb);
			if (c == o->match_c) {
				++char_pos;
				break; /* end of block comment */
			} else {
				SGS_CBufMode_DECP(&cb->r);
			}
		} else if (c <= SGS_File_MARKER && SGS_File_AFTER_EOF(o->f)) {
			c = SGS_Scanner_handle_invalid(o, c);
			if (c == SCAN_EOF) {
				o->c_flags |= SGS_SCAN_C_ERROR;
				--o->char_pos; // print for beginning of comment
				SGS_Scanner_error(o, "unterminated comment");
				++o->char_pos;
				return 0;
			}
		}
	}
	o->line_pos = line_pos;
	o->char_pos = char_pos;
	return 0;
}

/**
 * Upon '/' (slash), check for C-style or C++-style comment opener,
 * handling comment if present, otherwise simply returning '/'.
 */
uint8_t SGS_Scanner_handle_slashcomments(SGS_Scanner *o, uint8_t c) {
	SGS_CBuf *cb = &o->f->cb;
	uint8_t next_c = SGS_CBuf_GETC(cb);
	if (next_c == '/') return SGS_Scanner_handle_linecomment(o, next_c);
	if (next_c == '*') {
		++o->char_pos;
		o->match_c = '/';
		return SGS_Scanner_handle_blockcomment(o, next_c);
	}
	SGS_CBufMode_DECP(&cb->r);
	return c;
}

/**
 * Default array of character handler functions for SGS_Scanner_getc().
 * Each Scanner instance is assigned a copy and may change entries.
 *
 * NULL when the character is simply accepted.
 */
const SGS_ScannerCHandler_f SGS_Scanner_def_c_handlers[256] = {
	/* NUL 0x00 */ SGS_Scanner_handle_invalid,
	/* SOH 0x01 */ SGS_Scanner_handle_invalid,
	/* STX 0x02 */ SGS_Scanner_handle_invalid,
	/* ETX 0x03 */ SGS_Scanner_handle_invalid,
	/* EOT 0x04 */ SGS_Scanner_handle_invalid,
	/* ENQ 0x05 */ SGS_Scanner_handle_invalid,
	/* ACK 0x06 */ SGS_Scanner_handle_invalid,
	/* BEL '\a' */ SGS_Scanner_handle_invalid, // SGS_File_MARKER
	/* BS  '\b' */ SGS_Scanner_handle_invalid,
	/* HT  '\t' */ SGS_Scanner_handle_blanks,
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
	/*     ' '  */ SGS_Scanner_handle_blanks,
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
	/*     0x80 */ SGS_Scanner_handle_invalid,
	/*     0x81 */ SGS_Scanner_handle_invalid,
	/*     0x82 */ SGS_Scanner_handle_invalid,
	/*     0x83 */ SGS_Scanner_handle_invalid,
	/*     0x84 */ SGS_Scanner_handle_invalid,
	/*     0x85 */ SGS_Scanner_handle_invalid,
	/*     0x86 */ SGS_Scanner_handle_invalid,
	/*     0x87 */ SGS_Scanner_handle_invalid,
	/*     0x88 */ SGS_Scanner_handle_invalid,
	/*     0x89 */ SGS_Scanner_handle_invalid,
	/*     0x8A */ SGS_Scanner_handle_invalid,
	/*     0x8B */ SGS_Scanner_handle_invalid,
	/*     0x8C */ SGS_Scanner_handle_invalid,
	/*     0x8D */ SGS_Scanner_handle_invalid,
	/*     0x8E */ SGS_Scanner_handle_invalid,
	/*     0x8F */ SGS_Scanner_handle_invalid,
	/*     0x90 */ SGS_Scanner_handle_invalid,
	/*     0x91 */ SGS_Scanner_handle_invalid,
	/*     0x92 */ SGS_Scanner_handle_invalid,
	/*     0x93 */ SGS_Scanner_handle_invalid,
	/*     0x94 */ SGS_Scanner_handle_invalid,
	/*     0x95 */ SGS_Scanner_handle_invalid,
	/*     0x96 */ SGS_Scanner_handle_invalid,
	/*     0x97 */ SGS_Scanner_handle_invalid,
	/*     0x98 */ SGS_Scanner_handle_invalid,
	/*     0x99 */ SGS_Scanner_handle_invalid,
	/*     0x9A */ SGS_Scanner_handle_invalid,
	/*     0x9B */ SGS_Scanner_handle_invalid,
	/*     0x9C */ SGS_Scanner_handle_invalid,
	/*     0x9D */ SGS_Scanner_handle_invalid,
	/*     0x9E */ SGS_Scanner_handle_invalid,
	/*     0x9F */ SGS_Scanner_handle_invalid,
	/*     0xA0 */ SGS_Scanner_handle_invalid,
	/*     0xA1 */ SGS_Scanner_handle_invalid,
	/*     0xA2 */ SGS_Scanner_handle_invalid,
	/*     0xA3 */ SGS_Scanner_handle_invalid,
	/*     0xA4 */ SGS_Scanner_handle_invalid,
	/*     0xA5 */ SGS_Scanner_handle_invalid,
	/*     0xA6 */ SGS_Scanner_handle_invalid,
	/*     0xA7 */ SGS_Scanner_handle_invalid,
	/*     0xA8 */ SGS_Scanner_handle_invalid,
	/*     0xA9 */ SGS_Scanner_handle_invalid,
	/*     0xAA */ SGS_Scanner_handle_invalid,
	/*     0xAB */ SGS_Scanner_handle_invalid,
	/*     0xAC */ SGS_Scanner_handle_invalid,
	/*     0xAD */ SGS_Scanner_handle_invalid,
	/*     0xAE */ SGS_Scanner_handle_invalid,
	/*     0xAF */ SGS_Scanner_handle_invalid,
	/*     0xB0 */ SGS_Scanner_handle_invalid,
	/*     0xB1 */ SGS_Scanner_handle_invalid,
	/*     0xB2 */ SGS_Scanner_handle_invalid,
	/*     0xB3 */ SGS_Scanner_handle_invalid,
	/*     0xB4 */ SGS_Scanner_handle_invalid,
	/*     0xB5 */ SGS_Scanner_handle_invalid,
	/*     0xB6 */ SGS_Scanner_handle_invalid,
	/*     0xB7 */ SGS_Scanner_handle_invalid,
	/*     0xB8 */ SGS_Scanner_handle_invalid,
	/*     0xB9 */ SGS_Scanner_handle_invalid,
	/*     0xBA */ SGS_Scanner_handle_invalid,
	/*     0xBB */ SGS_Scanner_handle_invalid,
	/*     0xBC */ SGS_Scanner_handle_invalid,
	/*     0xBD */ SGS_Scanner_handle_invalid,
	/*     0xBE */ SGS_Scanner_handle_invalid,
	/*     0xBF */ SGS_Scanner_handle_invalid,
	/*     0xC0 */ SGS_Scanner_handle_invalid,
	/*     0xC1 */ SGS_Scanner_handle_invalid,
	/*     0xC2 */ SGS_Scanner_handle_invalid,
	/*     0xC3 */ SGS_Scanner_handle_invalid,
	/*     0xC4 */ SGS_Scanner_handle_invalid,
	/*     0xC5 */ SGS_Scanner_handle_invalid,
	/*     0xC6 */ SGS_Scanner_handle_invalid,
	/*     0xC7 */ SGS_Scanner_handle_invalid,
	/*     0xC8 */ SGS_Scanner_handle_invalid,
	/*     0xC9 */ SGS_Scanner_handle_invalid,
	/*     0xCA */ SGS_Scanner_handle_invalid,
	/*     0xCB */ SGS_Scanner_handle_invalid,
	/*     0xCC */ SGS_Scanner_handle_invalid,
	/*     0xCD */ SGS_Scanner_handle_invalid,
	/*     0xCE */ SGS_Scanner_handle_invalid,
	/*     0xCF */ SGS_Scanner_handle_invalid,
	/*     0xD0 */ SGS_Scanner_handle_invalid,
	/*     0xD1 */ SGS_Scanner_handle_invalid,
	/*     0xD2 */ SGS_Scanner_handle_invalid,
	/*     0xD3 */ SGS_Scanner_handle_invalid,
	/*     0xD4 */ SGS_Scanner_handle_invalid,
	/*     0xD5 */ SGS_Scanner_handle_invalid,
	/*     0xD6 */ SGS_Scanner_handle_invalid,
	/*     0xD7 */ SGS_Scanner_handle_invalid,
	/*     0xD8 */ SGS_Scanner_handle_invalid,
	/*     0xD9 */ SGS_Scanner_handle_invalid,
	/*     0xDA */ SGS_Scanner_handle_invalid,
	/*     0xDB */ SGS_Scanner_handle_invalid,
	/*     0xDC */ SGS_Scanner_handle_invalid,
	/*     0xDD */ SGS_Scanner_handle_invalid,
	/*     0xDE */ SGS_Scanner_handle_invalid,
	/*     0xDF */ SGS_Scanner_handle_invalid,
	/*     0xE0 */ SGS_Scanner_handle_invalid,
	/*     0xE1 */ SGS_Scanner_handle_invalid,
	/*     0xE2 */ SGS_Scanner_handle_invalid,
	/*     0xE3 */ SGS_Scanner_handle_invalid,
	/*     0xE4 */ SGS_Scanner_handle_invalid,
	/*     0xE5 */ SGS_Scanner_handle_invalid,
	/*     0xE6 */ SGS_Scanner_handle_invalid,
	/*     0xE7 */ SGS_Scanner_handle_invalid,
	/*     0xE8 */ SGS_Scanner_handle_invalid,
	/*     0xE9 */ SGS_Scanner_handle_invalid,
	/*     0xEA */ SGS_Scanner_handle_invalid,
	/*     0xEB */ SGS_Scanner_handle_invalid,
	/*     0xEC */ SGS_Scanner_handle_invalid,
	/*     0xED */ SGS_Scanner_handle_invalid,
	/*     0xEE */ SGS_Scanner_handle_invalid,
	/*     0xEF */ SGS_Scanner_handle_invalid,
	/*     0xF0 */ SGS_Scanner_handle_invalid,
	/*     0xF1 */ SGS_Scanner_handle_invalid,
	/*     0xF2 */ SGS_Scanner_handle_invalid,
	/*     0xF3 */ SGS_Scanner_handle_invalid,
	/*     0xF4 */ SGS_Scanner_handle_invalid,
	/*     0xF5 */ SGS_Scanner_handle_invalid,
	/*     0xF6 */ SGS_Scanner_handle_invalid,
	/*     0xF7 */ SGS_Scanner_handle_invalid,
	/*     0xF8 */ SGS_Scanner_handle_invalid,
	/*     0xF9 */ SGS_Scanner_handle_invalid,
	/*     0xFA */ SGS_Scanner_handle_invalid,
	/*     0xFB */ SGS_Scanner_handle_invalid,
	/*     0xFC */ SGS_Scanner_handle_invalid,
	/*     0xFD */ SGS_Scanner_handle_invalid,
	/*     0xFE */ SGS_Scanner_handle_invalid,
	/*     0xFF */ SGS_Scanner_handle_invalid, /* Scanning end marker */
};

#if 0
static bool scan_number(SGS_Scanner *o, uint32_t *num) {
	SGS_CBuf *cb = &o->f->cb;
	bool error = false;
	uint32_t c = SGS_CBuf_GETC(cb);
	uint32_t i = 0;
	do {
		o->strbuf[i] = c;
		c = SGS_CBuf_GETC(cb);
	} while (IS_DIGIT(c) && ++i < (STRBUF_LEN - 1));
	if (i == (STRBUF_LEN - 1)) {
		o->strbuf[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->strbuf[i] = '\0';
	}
	SGS_CBufMode_DECP(&cb->r);
	return !error;
}

static bool scan_identifier(SGS_Scanner *o) {
	SGS_CBuf *cb = &o->f->cb;
	uint32_t len;
	bool truncated;
	SGS_CBufMode_DECP(&cb->r);
	truncated = !read_symstr(o->f, o->strbuf, STRBUF_LEN, &len);
	if (truncated) {
		SGS_Scanner_error(o,
"identifier length limited to %d characters", (STRBUF_LEN - 1));
	}
	return !truncated;
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
char SGS_Scanner_getc(SGS_Scanner *o) {
	SGS_CBuf *cb = &o->f->cb;
	uint8_t c;
	if ((o->c_flags & SGS_SCAN_C_NEWLINE) != 0) {
		++o->line_pos;
		o->char_pos = 0;
		o->c_flags &= ~SGS_SCAN_C_NEWLINE;
	}
	do {
		++o->char_pos;
		c = SGS_CBuf_GETC(cb);
		SGS_ScannerCHandler_f handler = o->c_handlers[c];
		if (!handler) break;
		c = handler(o, c);
	} while (c == 0);
	if (c == SCAN_EOF) return 0;
	if ((o->c_flags & SGS_SCAN_C_NEWLINE) != 0) {
		/*
		 * Handle greedy scanning past newline characters.
		 * Unget char, set position, and return newline
		 * (also setting preceding char to newline so a
		 * following unget will work).
		 */
		SGS_CBuf_UNGETC(cb);
		--o->line_pos;
		o->char_pos = o->old_char_pos;
		c = SGS_SCAN_EOL;
		cb->w.pos = cb->r.pos - 1;
		SGS_CBufMode_FIXP(&cb->w);
		SGS_CBuf_SETC_NC(cb, c);
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
void SGS_Scanner_ungetc(SGS_Scanner *o) {
	if ((o->s_flags & SGS_SCAN_S_UNGETC) != 0) {
		SGS_Scanner_error(o, "scanner ungetc repeated by parsing code (return without action)");
		return;
	}
	SGS_CBuf *cb = &o->f->cb;
	SGS_CBuf_UNGETC(cb);
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
bool SGS_Scanner_tryc(SGS_Scanner *o, char testc) {
	char c = SGS_Scanner_getc(o);
	if (c != testc) {
		SGS_Scanner_ungetc(o);
		return false;
	}
	return true;
}
