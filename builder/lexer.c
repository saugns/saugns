/* sgensys: Script lexer module.
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
		void *restrict buf, size_t buf_len,
		size_t *restrict str_len) {
	uint8_t *dst = buf;
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
			SGS_FBufMode_DECP(&f->mr);
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
	SGS_File *f;
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
SGS_Lexer *SGS_create_Lexer(const char *restrict fname,
		SGS_SymTab *restrict symtab) {
	SGS_Lexer *o;
	if (symtab == NULL) return NULL;
	uint8_t *strbuf = calloc(1, STRBUF_LEN);
	if (strbuf == NULL) return NULL;

	o = calloc(1, sizeof(SGS_Lexer));
	if (o == NULL) return NULL;
	o->f = SGS_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	o->strbuf = strbuf;
	if (!SGS_File_fopenrb(o->f, fname)) goto ERROR;
	return o;

ERROR:
	SGS_destroy_Lexer(o);
	return NULL;
}

/**
 * Destroy instance, also closing the file for which it was made.
 */
void SGS_destroy_Lexer(SGS_Lexer *restrict o) {
	SGS_destroy_File(o->f);
	if (o->strbuf) free(o->strbuf);
	free(o);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGS_Lexer *restrict o,
		uint32_t options, const char *restrict prefix,
		const char *restrict fmt, va_list ap) {
	SGS_File *f = o->f;
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, o->line_num, o->char_num);
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
void SGS_Lexer_warning(SGS_Lexer *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/*
 * Print warning message indicating that the last character retrieved
 * is invalid.
 */
//static void warning_character(SGS_Lexer *restrict o) {
//	SGS_File *f = o->f;
//	uint8_t b = SGS_File_RETC(f);
//	if (IS_VISIBLE(b)) {
//		SGS_Lexer_warning(o, "invalid character: %c", (char) b);
//	} else {
//		SGS_Lexer_warning(o, "invalid character (value 0x%02hhX)", b);
//	}
//}

/**
 * Print error message including file name and current position.
 */
void SGS_Lexer_error(SGS_Lexer *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}

typedef uint8_t (*HandleValue_f)(SGS_Lexer *o, uint8_t c);

static uint8_t handle_invalid(SGS_Lexer *restrict o,
		uint8_t c SGS__maybe_unused) {
	SGS_File *f = o->f;
	SGS_ScriptToken *t = &o->token;
	t->type = SGS_T_INVALID;
	if (!SGS_File_AFTER_EOF(f)) {
		t->data.b = 0;
#if !SGS_LEXER_QUIET
		SGS_Lexer_warning(o, "invalid character (value 0x%02hhX)", c);
#endif
		return 0;
	}
	uint8_t status = SGS_File_STATUS(f);
	t->data.b = status;
	if ((status & SGS_FILE_ERROR) != 0) {
		SGS_Lexer_error(o, "file reading failed");
	}
	return 0;
}

static uint8_t handle_blanks(SGS_Lexer *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	do {
		++o->char_num;
		c = SGS_File_GETC(f);
	} while (IS_SPACE(c));
	return c;
}

static uint8_t handle_linebreaks(SGS_Lexer *restrict o, uint8_t c) {
	SGS_File *f = o->f;
	++o->line_num;
	if (c == '\n') SGS_File_TRYC(f, '\r');
	while (SGS_File_trynewline(f)) {
		++o->line_num;
	}
	o->char_num = 1;
	return SGS_File_GETC_NC(f);
}

static uint8_t handle_linecomment(SGS_Lexer *restrict o,
		uint8_t c SGS__maybe_unused) {
	SGS_File *f = o->f;
	o->char_num += SGS_File_skipline(f);
	return SGS_File_GETC_NC(f);
}

//static uint8_t handle_block_comment(SGS_Lexer *restrict o, uint8_t c) {
//	SGS_Lexer_warning(o, "cannot yet handle comment marked by '%c'", c);
//	return 0;
//}

static uint8_t handle_special(SGS_Lexer *restrict o, uint8_t c) {
	SGS_ScriptToken *t = &o->token;
	t->type = SGS_T_SPECIAL;
	t->data.c = c;
	return 0;
}

static uint8_t handle_numeric_value(SGS_Lexer *restrict o,
		uint8_t SGS__maybe_unused first_digit) {
	SGS_File *f = o->f;
	SGS_ScriptToken *t = &o->token;
	double num;
	size_t read_len;
	SGS_FBufMode_DECP(&f->mr);
	SGS_File_getd(f, &num, false, &read_len);
	t->type = SGS_T_VAL_REAL;
	// XXX: dummy code
	return 0;
}

static uint8_t handle_identifier(SGS_Lexer *restrict o,
		uint8_t c SGS__maybe_unused) {
	SGS_File *f = o->f;
	SGS_ScriptToken *t = &o->token;
	size_t len;
	SGS_FBufMode_DECP(&f->mr);
	bool truncated = !read_syms(f, o->strbuf, STRBUF_LEN, &len);
	if (truncated) {
		SGS_Lexer_warning(o,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		o->char_num += SGS_File_skips(f, filter_symchar);
	}
	const char *pool_str;
	pool_str = SGS_SymTab_pool_str(o->symtab, o->strbuf, len);
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
bool SGS_Lexer_get(SGS_Lexer *restrict o, SGS_ScriptToken *restrict t) {
	SGS_File *f = o->f;
	uint8_t c;
	++o->char_num;
	c = SGS_File_GETC(f);
	do {
		switch (c) {
		case /* NUL */ 0x00:
		case /* SOH */ 0x01:
		case /* STX */ 0x02:
		case /* ETX */ 0x03:
		case /* EOT */ 0x04:
		case /* ENQ */ 0x05:
		case /* ACK */ 0x06:
		case /* BEL */ '\a': // SGS_FILE_MARKER
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
		default:
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
bool SGS_Lexer_get_special(SGS_Lexer *restrict o,
		SGS_ScriptToken *restrict t) {
	SGS_File *f = o->f;
	uint8_t c;
	do {
		++o->char_num;
		c = SGS_File_GETC(f);
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
