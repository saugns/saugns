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
#include "file.h"
#include "../math.h"
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
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

/* Visible ASCII character. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

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

/*
 * Lexer implementation.
 */

#define STRBUF_LEN 1024

struct SGS_Lexer {
	SGS_File f;
	SGS_SymTab *symtab;
	uint32_t line_num, char_num;
	SGS_ScriptToken token;
	uint8_t *strbuf;
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
	uint8_t *strbuf = calloc(1, STRBUF_LEN);
	if (strbuf == NULL) return NULL;

	o = calloc(1, sizeof(SGS_Lexer));
	if (o == NULL) return NULL;
	SGS_init_File(&o->f);
	if (!SGS_File_fopenrb(&o->f, fname)) {
		SGS_fini_File(&o->f);
		free(o);
		return NULL;
	}
	o->symtab = symtab;
	o->strbuf = strbuf;
	return o;
}

/**
 * Destroy instance, also closing the file for which it was made.
 */
void SGS_destroy_Lexer(SGS_Lexer *o) {
	if (o == NULL) return;
	SGS_File_close(&o->f);
	SGS_fini_File(&o->f);
	free(o->strbuf);
	free(o);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Lexer *o, uint32_t options, const char *prefix,
	const char *fmt, va_list ap) {
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			o->f.path, o->line_num, o->char_num);
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
//static void warning_character(SGS_Lexer *o) {
//	uint8_t b = SGS_CBuf_RETC(&o->f.cb);
//	if (IS_VISIBLE(b)) {
//		SGS_Lexer_warning(o, "invalid character: %c", (char) b);
//	} else {
//		SGS_Lexer_warning(o, "invalid character (value 0x%02hhX)", b);
//	}
//}

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

static uint8_t handle_invalid(SGS_Lexer *o, uint8_t c SGS__maybe_unused) {
	SGS_ScriptToken *t = &o->token;
	t->type = SGS_T_INVALID;
	if (!SGS_File_AFTER_EOF(&o->f)) {
		t->data.b = 0;
#if !SGS_LEXER_QUIET
		SGS_Lexer_warning(o, "invalid character (value 0x%02hhX)", c);
#endif
		return 0;
	}
	uint8_t status = SGS_File_STATUS(&o->f);
	t->data.b = status;
	switch (status) {
		case SGS_File_END:
			break;
		case SGS_File_ERROR:
			SGS_Lexer_error(o, "file reading failed");
			break;
		default: /* shouldn't happen */
			SGS_Lexer_error(o, "file read status 0x%02hhX", status);
			break;
	}
	return 0;
}

static uint8_t handle_blanks(SGS_Lexer *o, uint8_t c) {
	SGS_CBuf *cb = &o->f.cb;
	do {
		++o->char_num;
		c = SGS_CBuf_GETC(cb);
	} while (IS_SPACE(c));
	return c;
}

static uint8_t handle_linebreaks(SGS_Lexer *o, uint8_t c) {
	SGS_CBuf *cb = &o->f.cb;
	do {
		++o->line_num;
		if (c == '\n') SGS_CBuf_TRYC(cb, '\r');
		c = SGS_CBuf_GETC(cb);
	} while (IS_LNBRK(c));
	o->char_num = 1;
	return c;
}

static uint8_t handle_linecomment(SGS_Lexer *o,
		uint8_t c SGS__maybe_unused) {
	o->char_num += SGS_File_skipline(&o->f);
	return SGS_CBuf_GETC_NC(&o->f.cb);
}

//static uint8_t handle_block_comment(SGS_Lexer *o, uint8_t c) {
//	SGS_Lexer_warning(o, "cannot yet handle comment marked by '%c'", c);
//	return 0;
//}

static uint8_t handle_special(SGS_Lexer *o, uint8_t c) {
	SGS_ScriptToken *t = &o->token;
	t->type = SGS_T_SPECIAL;
	t->data.c = c;
	return 0;
}

static uint8_t handle_numeric_value(SGS_Lexer *o, uint8_t first_digit) {
	SGS_CBuf *cb = &o->f.cb;
	SGS_ScriptToken *t = &o->token;
	uint32_t c = first_digit;
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
	t->type = SGS_T_VAL_INT; /* XXX: dummy code */
	SGS_CBufMode_DECP(&cb->r);
	return 0;
}

static uint8_t handle_identifier(SGS_Lexer *o, uint8_t c SGS__maybe_unused) {
	SGS_CBuf *cb = &o->f.cb;
	SGS_ScriptToken *t = &o->token;
	uint32_t len;
	SGS_CBufMode_DECP(&cb->r);
	if (!read_symstr(&o->f, o->strbuf, STRBUF_LEN, &len)) {
		SGS_Lexer_error(o,
"identifier length limited to %d characters", (STRBUF_LEN - 1));
	}
	const char *pool_str;
	pool_str = SGS_SymTab_pool_str(o->symtab, (const char*)o->strbuf, len);
	if (pool_str == NULL) {
		SGS_Lexer_error(o, "failed to register string '%s'", o->strbuf);
	}
	o->char_num += len - 1;
	t->type = SGS_T_ID_STR;
	t->data.id = pool_str;
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
bool SGS_Lexer_get(SGS_Lexer *o, SGS_ScriptToken *t) {
	uint8_t c;
	++o->char_num;
	c = SGS_CBuf_GETC(&o->f.cb);
	do {
		switch (c) {
		case /* NUL */ 0x00:
		case /* SOH */ 0x01:
		case /* STX */ 0x02:
		case /* ETX */ 0x03:
		case /* EOT */ 0x04:
		case /* ENQ */ 0x05:
		case /* ACK */ 0x06:
		case /* BEL */ '\a': // SGS_File_MARKER
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
		(o->token.data.b == 0) :
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
bool SGS_Lexer_get_special(SGS_Lexer *o, SGS_ScriptToken *t) {
	uint8_t c;
	do {
		++o->char_num;
		c = SGS_CBuf_GETC(&o->f.cb);
		if (IS_VISIBLE(c)) c = handle_special(o, c);
		else c = handle_invalid(o, c);
	} while (c != 0);
	if (t != NULL) {
		*t = o->token;
	}
	return (o->token.type == SGS_T_INVALID) ?
		(o->token.data.b == 0) :
		true;
}
