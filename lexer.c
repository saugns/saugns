/* sgensys: Script lexer module.
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

#include "lexer.h"
#include "streamf.h"
#include "math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Read helper definitions & functions.
 */

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

/* Visible ASCII character. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

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
			SGS_CBuf_GETC(&fr->buf);
			break;
		}
		buf[i++] = c;
	}
	buf[i] = '\0';
	*sym_len = i;
	return !error;
}

/*
 * Lexer implementation.
 */

#define STRING_MAX_LEN 1024

struct SGS_Lexer {
	SGS_Stream fr;
	SGS_SymTab *symtab;
	uint32_t line_num, char_num;
	SGS_LexerToken token;
	uint8_t string[STRING_MAX_LEN];
};

/**
 * Create instance for the given file and using the given symbol
 * table.
 *
 * \return instance, or NULL on failure.
 */
SGS_Lexer *SGS_create_Lexer(const char *fname, SGS_SymTab *symtab) {
	SGS_Lexer *o;
	if (symtab == NULL) return NULL;

	o = calloc(1, sizeof(struct SGS_Lexer));
	if (o == NULL) return NULL;
	SGS_init_Stream(&o->fr);
	if (!SGS_Stream_fopenrb(&o->fr, fname)) {
		SGS_fini_Stream(&o->fr);
		free(o);
		return NULL;
	}
	o->symtab = symtab;
	return o;
}

/**
 * Destroy instance, also closing the file for which it was made.
 */
void SGS_destroy_Lexer(SGS_Lexer *o) {
	if (o == NULL) return;
	SGS_Stream_close(&o->fr);
	SGS_fini_Stream(&o->fr);
	free(o);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Lexer *o, uint32_t options, const char *prefix,
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
void SGS_Lexer_warning(SGS_Lexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/*
 * Print warning message indicating that the last character retrieved
 * is invalid.
 */
static void warning_character(SGS_Lexer *o) {
	uint8_t b = SGS_CBuf_RETC(&o->fr.buf);
	if (IS_VISIBLE(b)) {
		SGS_Lexer_warning(o, "invalid character: %c", (char) b);
	} else {
		SGS_Lexer_warning(o, "invalid character (value 0x%hhx)", b);
	}
}

/**
 * Print error message including file name and current position.
 */
void SGS_Lexer_error(SGS_Lexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}

typedef uint8_t (*HandleValue_f)(SGS_Lexer *o, uint8_t c);

static uint8_t handle_invalid(SGS_Lexer *o, uint8_t c) {
	SGS_LexerToken *t = &o->token;
	uint8_t status = o->fr.status;
	t->type = SGS_T_INVALID;
	t->data.b = status;
	switch (status) {
		case SGS_STREAM_OK:
#if !SGS_LEXER_QUIET
			warning_character(o);
#endif
			break;
		case SGS_STREAM_END:
			break;
		default:
			SGS_Lexer_error(o, "file reading failed (status %hhu)",
				status);
			break;
	}
	return 0;
}

static uint8_t handle_blanks(SGS_Lexer *o, uint8_t c) {
	do {
		++o->char_num;
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_BLANK(c));
	return c;
}

static uint8_t handle_linebreaks(SGS_Lexer *o, uint8_t c) {
	do {
		++o->line_num;
		if (c == '\n') SGS_CBuf_TRYC(&o->fr.buf, '\r');
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_LNBRK(c));
	o->char_num = 1;
	return c;
}

static uint8_t handle_linecomment(SGS_Lexer *o, uint8_t c) {
	do {
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (!IS_LNBRK(c));
	return c;
}

static uint8_t handle_block_comment(SGS_Lexer *o, uint8_t c) {
	SGS_Lexer_warning(o, "cannot yet handle comment marked by '%c'", c);
	return 0;
}

static uint8_t handle_special(SGS_Lexer *o, uint8_t c) {
	SGS_LexerToken *t = &o->token;
	t->type = SGS_T_SPECIAL;
	t->data.c = c;
	return 0;
}

static uint8_t handle_numeric_value(SGS_Lexer *o, uint8_t first_digit) {
	SGS_LexerToken *t = &o->token;
	uint32_t c = first_digit;
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_DIGIT(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	t->type = SGS_T_VAL_INT; /* XXX: dummy code */
	SGS_CBuf_UNGETC(&o->fr.buf);
	return 0;
}

static uint8_t handle_identifier(SGS_Lexer *o, uint8_t id_head) {
	SGS_LexerToken *t = &o->token;
	uint32_t len;
	bool error;
	error = !read_sym(&o->fr, o->string, STRING_MAX_LEN, &len);
	if (error) {
		SGS_Lexer_error(o, "cannot handle identifier longer than %d characters", STRING_MAX_LEN);
	}
	if (len == 0) {
	}
	const char *pool_str;
	uint8_t c = id_head;
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = SGS_CBuf_GETC(&o->fr.buf);
	} while (IS_SYMCHAR(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// TODO: warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	pool_str = SGS_SymTab_pool_str(o->symtab, (const char*)o->string, i);
	if (pool_str == NULL) {
		SGS_Lexer_error(o, "failed to register string '%s'", o->string);
	}
	t->type = SGS_T_ID_STR;
	t->data.id = pool_str;
	SGS_CBuf_UNGETC(&o->fr.buf);
	return 0;
}

/**
 * Get the next token from the current file.
 *
 * Upon end of file, an SGS_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SGS_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SGS_Lexer_get(SGS_Lexer *o, SGS_LexerToken *t) {
	uint8_t c;
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
			c = handle_special(o, c);
			break;
		case '#':
			c = handle_linecomment(o, c);
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
		case '/':
			c = handle_special(o, c);
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
			c = handle_numeric_value(o, c);
			break;
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '@':
			c = handle_special(o, c);
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
			c = handle_identifier(o, c);
			break;
		case '[':
		case '\\':
		case ']':
		case '^':
		case '_':
		case '`':
			c = handle_special(o, c);
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
			c = handle_identifier(o, c);
			break;
		case '{':
		case '|':
		case '}':
		case '~':
			c = handle_special(o, c);
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
	} while (c != 0);
	if (t != NULL) {
		*t = o->token;
	}
	return (o->token.type == SGS_T_INVALID) ?
		(o->token.data.b == SGS_STREAM_OK) :
		true;
}

/**
 * Get the next token from the current file. Interprets any visible ASCII
 * character as a special token character.
 *
 * Upon end of file, an SGS_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SGS_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SGS_Lexer_get_special(SGS_Lexer *o, SGS_LexerToken *t) {
	uint8_t c;
	do {
		++o->char_num;
		c = SGS_CBuf_GETC(&o->fr.buf);
		if (IS_VISIBLE(c)) c = handle_special(o, c);
		else c = handle_invalid(o, c);
	} while (c != 0);
	if (t != NULL) {
		*t = o->token;
	}
	return (o->token.type == SGS_T_INVALID) ?
		(o->token.data.b == SGS_STREAM_OK) :
		true;
}
