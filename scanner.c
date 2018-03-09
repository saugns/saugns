/* sgensys: Script file scanner module.
 * Copyright (c) 2014, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "scanner.h"
#include "streamf.h"
#include "math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Scanner implementation.
 */

#define SCAN_EOF 0xFF

#define STRING_MAXLEN 255

/**
 * Create instance.
 *
 * \return instance or NULL if allocation fails
 */
SGS_Scanner *SGS_create_Scanner(void) {
	SGS_Scanner *o = calloc(1, sizeof(struct SGS_Scanner));
	SGS_init_Stream(&o->fr);
	return o;
}

/**
 * Destroy instance. Closes file if open.
 */
void SGS_destroy_Scanner(SGS_Scanner *o) {
	SGS_Scanner_close(o);
	SGS_fini_Stream(&o->fr);
	free(o);
}

/**
 * Open file.
 *
 * If a file is already open, it will first be closed.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but the filename remains available for printing until an explicit
 * close; see fread module.
 *
 * \return true if successful
 */
bool SGS_Scanner_open(SGS_Scanner *o, const char *fname) {
	if (o->fr.active != SGS_STREAM_CLOSED) {
		SGS_Scanner_close(o);
	}
	if (!SGS_Stream_fopenrb(&o->fr, fname)) return false;
	o->line_num = 0;
	o->char_num = 0;
	o->newline = false;
	o->old_char_num = 0;
	return true;
}

/**
 * Close the current file.
 *
 * \return true if file was open
 */
void SGS_Scanner_close(SGS_Scanner *o) {
	SGS_Stream_close(&o->fr);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Scanner *o, uint32_t options, const char *prefix,
	const char *fmt, va_list ap) {
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			o->fr.name, o->line_num, o->char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file name and current position.
 */
void SGS_Scanner_warning(SGS_Scanner *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file name and current position.
 */
void SGS_Scanner_error(SGS_Scanner *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_BLANK(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

static bool read_sym(SGS_Stream *fr, char *buf, uint32_t maxlen,
		uint32_t *sym_len) {
	uint32_t i = 0;
	bool error = false;
	for (;;) {
		if (i == maxlen) {
			error = true;
			break;
		}
		uint8_t c = SGS_CBuf_GETC(&fr->buf);
		if (!IS_SYMCHAR(c)) {
			SGS_CBufMode_DECP(&fr->buf.r);
			break;
		}
		buf[i++] = c;
	}
	buf[i] = '\0';
	*sym_len = i;
	return !error;
}

typedef uint8_t (*HandleValue_f)(SGS_Scanner *o, uint8_t c);

static uint8_t handle_invalid(SGS_Scanner *o, uint8_t c) {
	uint8_t status = o->fr.status;
	switch (status) {
		case SGS_STREAM_OK:
			SGS_Scanner_warning(o, "invalid character (value 0x%hhx)",
				c);
			c = 0;
			break;
		case SGS_STREAM_END:
			c = SCAN_EOF;
			break;
		default:
			SGS_Scanner_error(o, "file reading failed (status %hhu)",
				status);
			c = SCAN_EOF;
			break;
	}
	return c;
}

static uint8_t handle_blanks(SGS_Scanner *o, uint8_t c) {
	int32_t i = 0;
	for (;;) {
		c = SGS_CBuf_GETC(&o->fr.buf);
		if (!IS_BLANK(c)) break;
		++i;
	}
	SGS_CBufMode_DECP(&o->fr.buf.r);
	o->char_num += i;
	return 0;
}

static uint8_t handle_linebreaks(SGS_Scanner *o, uint8_t c) {
	do {
		++o->line_num;
		if (c == '\n') SGS_CBuf_TRYC(&o->fr.buf, '\r');
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_LNBRK(c));
	SGS_CBufMode_DECP(&o->fr.buf.r);
	o->newline = true;
	o->old_char_num = o->char_num;
	o->char_num = 0;
	return 0;
}

static uint8_t handle_linecomments(SGS_Scanner *o, uint8_t c) {
	do {
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (!IS_LNBRK(c));
	SGS_CBufMode_DECP(&o->fr.buf.r);
	return 0;
}

static uint8_t handle_ccomment(SGS_Scanner *o, uint8_t c) {
	SGS_Scanner_warning(o, "cannot yet handle C-style comment");
	return 0;
}

static uint8_t handle_slash(SGS_Scanner *o, uint8_t c) {
	uint8_t next_c = SGS_CBuf_GETC(&o->fr.buf);
	if (next_c == '/') return handle_linecomments(o, next_c);
	if (next_c == '*') return handle_ccomment(o, next_c);
	SGS_CBufMode_DECP(&o->fr.buf.r);
	return c;
}

static bool scan_number(SGS_Scanner *o, uint32_t *num) {
	char str[STRING_MAXLEN + 1];
	bool error = false;
	uint32_t c = SGS_CBuf_GETC(&o->fr.buf);
	uint32_t i = 0;
	do {
		str[i] = c;
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_DIGIT(c) && ++i < (STRING_MAXLEN - 1));
	if (i == (STRING_MAXLEN - 1)) {
		str[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		str[i] = '\0';
	}
	SGS_CBufMode_DECP(&o->fr.buf.r);
	return !error;
}

static bool scan_identifier(SGS_Scanner *o, char *str) {
	uint32_t len;
	bool error;
	error = !read_sym(&o->fr, str, STRING_MAXLEN, &len);
	if (error) {
		SGS_Scanner_error(o, "cannot handle identifier longer than %d characters", STRING_MAXLEN);
	}
	if (len == 0) {
	}
	uint8_t c = SGS_CBuf_GETC(&o->fr.buf);
	uint32_t i = 0;
	do {
		str[i] = c;
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_SYMCHAR(c) && ++i < (STRING_MAXLEN - 1));
	if (i == (STRING_MAXLEN - 1)) {
		str[i] = '\0';
		// TODO: warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		str[i] = '\0';
	}
	return !error;
}

/**
 * Get next character (with filtering); remove spaces, tabs,
 * comments, and replace newlines with a single SGS_SCAN_NEWLINE
 * ('\n') character.
 *
 * Upon end of file, 0 will be returned. Non-visible characters in
 * the input are otherwise passed over, printing a warning.
 *
 * \return character or 0 upon end of file
 */
char SGS_Scanner_getc(SGS_Scanner *o) {
	uint8_t c;
	if (o->newline) {
		++o->line_num;
		o->char_num = 0;
		o->newline = false;
	}
	do {
		++o->char_num;
		c = SGS_CBuf_GETC(&o->fr.buf);
		switch (c) {
		case /* NUL */ 0x00: /* check stream status */
		case /* SOH */ 0x01:
		case /* STX */ 0x02:
		case /* ETX */ 0x03:
		case /* EOT */ 0x04:
		case /* ENQ */ 0x05:
		case /* ACK */ 0x06:
		case /* BEL */ '\a':
		case /* BS  */ '\b':
			c = handle_invalid(o, c);
			break;
		case /* HT  */ '\t':
			c = handle_blanks(o, c);
			break;
		case /* LF  */ '\n':
			c = handle_linebreaks(o, c);
			break;
		case /* VT  */ '\v':
		case /* FF  */ '\f':
			c = handle_invalid(o, c);
			break;
		case /* CR  */ '\r':
			c = handle_linebreaks(o, c);
			break;
		case /* SO  */ 0x0E:
		case /* SI  */ 0x0F:
		case /* DLE */ 0x10:
		case /* DC1 */ 0x11:
		case /* DC2 */ 0x12:
		case /* DC3 */ 0x13:
		case /* DC4 */ 0x14:
		case /* NAK */ 0x15:
		case /* SYN */ 0x16:
		case /* ETB */ 0x17:
		case /* CAN */ 0x18:
		case /* EM  */ 0x19:
		case /* SUB */ 0x1A:
		case /* ESC */ 0x1B:
		case /* FS  */ 0x1C:
		case /* GS  */ 0x1D:
		case /* RS  */ 0x1E:
		case /* US  */ 0x1F:
			c = handle_invalid(o, c);
			break;
		case ' ':
			c = handle_blanks(o, c);
			break;
		case '!':
		case '"':
			break;
		case '#':
			c = handle_linecomments(o, c);
			break;
		case '$':
		case '%':
		case '&':
		case '\'':
		case '(':
		case ')':
		case '*':
		case '+':
		case ',':
		case '-':
		case '.':
			break;
		case '/':
			c = handle_slash(o, c);
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			break;
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '@':
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
		case 'G':
		case 'H':
		case 'I':
		case 'J':
		case 'K':
		case 'L':
		case 'M':
		case 'N':
		case 'O':
		case 'P':
		case 'Q':
		case 'R':
		case 'S':
		case 'T':
		case 'U':
		case 'V':
		case 'W':
		case 'X':
		case 'Y':
		case 'Z':
			break;
		case '[':
		case '\\':
		case ']':
		case '^':
		case '_':
		case '`':
			break;
		case 'a':
		case 'b':
		case 'c':
		case 'd':
		case 'e':
		case 'f':
		case 'g':
		case 'h':
		case 'i':
		case 'j':
		case 'k':
		case 'l':
		case 'm':
		case 'n':
		case 'o':
		case 'p':
		case 'q':
		case 'r':
		case 's':
		case 't':
		case 'u':
		case 'v':
		case 'w':
		case 'x':
		case 'y':
		case 'z':
			break;
		case '{':
		case '|':
		case '}':
		case '~':
			break;
		case /* DEL */ 0x7F:
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:
		case 0x8F:
		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
		case 0x98:
		case 0x99:
		case 0x9A:
		case 0x9B:
		case 0x9C:
		case 0x9D:
		case 0x9E:
		case 0x9F:
		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
		case 0xA4:
		case 0xA5:
		case 0xA6:
		case 0xA7:
		case 0xA8:
		case 0xA9:
		case 0xAA:
		case 0xAB:
		case 0xAC:
		case 0xAD:
		case 0xAE:
		case 0xAF:
		case 0xB0:
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:
		case 0xB7:
		case 0xB8:
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:
		case 0xBF:
		case 0xC0:
		case 0xC1:
		case 0xC2:
		case 0xC3:
		case 0xC4:
		case 0xC5:
		case 0xC6:
		case 0xC7:
		case 0xC8:
		case 0xC9:
		case 0xCA:
		case 0xCB:
		case 0xCC:
		case 0xCD:
		case 0xCE:
		case 0xCF:
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xD4:
		case 0xD5:
		case 0xD6:
		case 0xD7:
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:
		case 0xDF:
		case 0xE0:
		case 0xE1:
		case 0xE2:
		case 0xE3:
		case 0xE4:
		case 0xE5:
		case 0xE6:
		case 0xE7:
		case 0xE8:
		case 0xE9:
		case 0xEA:
		case 0xEB:
		case 0xEC:
		case 0xED:
		case 0xEE:
		case 0xEF:
		case 0xF0:
		case 0xF1:
		case 0xF2:
		case 0xF3:
		case 0xF4:
		case 0xF5:
		case 0xF6:
		case 0xF7:
		case 0xF8:
		case 0xF9:
		case 0xFA:
		case 0xFB:
		case 0xFC:
		case 0xFD:
		case 0xFE:
		case 0xFF:
			c = handle_invalid(o, c);
			break;
		}
	} while (c == 0);
	if (c == SCAN_EOF) return 0;
	if (o->newline) {
		/*
		 * Handle greedy scanning past newline characters.
		 * Unget char, set position, and return newline
		 * (also setting preceding char to newline so a
		 * following unget will work).
		 */
		SGS_CBuf_UNGETC(&o->fr.buf);
		--o->line_num;
		o->char_num = o->old_char_num;
		c = SGS_SCAN_NEWLINE;
		o->fr.buf.w.pos = o->fr.buf.r.pos - 1;
		SGS_CBufMode_FIXP(&o->fr.buf.w);
		SGS_CBuf_SETC_NC(&o->fr.buf, c);
	}
	return c;
}

/**
 * Get next character (with filtering) if it matches \p testc.
 *
 * If the characters do not match, the last character read will
 * be ungotten; any skipped characters (whitespace, etc.) will not
 * be processed again.
 *
 * \return true if character matched \p testc
 */
bool SGS_Scanner_tryc(SGS_Scanner *o, char testc) {
	char c = SGS_Scanner_getc(o);
	if (c != testc) {
		SGS_CBuf_UNGETC(&o->fr.buf);
		--o->char_num;
		return false;
	}
	return true;
}
