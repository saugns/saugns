/* sgensys script lexer module.
 * Copyright (c) 2014 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include "lexer.h"
#include "math.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define BUF_LEN    BUFSIZ
#define BUFC_ERROR '\0'
#define BUFC_EOF   0xFF
#define STRING_MAX_LEN  1024

struct SGSLexer {
	uint8_t buf1[BUF_LEN],
	        buf2[BUF_LEN];
	uint8_t *buf_start,
	        *buf_read,
	        *buf_eof;
	FILE *file;
	const char *filename;
	SGSSymtab *symtab;
	uint32_t line, line_pos;
	SGSToken token;
	uint8_t string[STRING_MAX_LEN];
};

SGSLexer *SGS_create_lexer(const char *filename, SGSSymtab *symtab) {
	SGSLexer *o;
	if (symtab == NULL) return NULL;
	FILE *file = fopen(filename, "rb");
	if (file == NULL) return NULL;

	o = calloc(1, sizeof(SGSLexer));
	if (o == NULL) return NULL;
	o->buf_start = NULL;
	o->buf_read = o->buf_start + BUF_LEN; /* trigger initialization */
	o->file = file;
	o->filename = filename;
	o->symtab = symtab;
	return o;
}

void SGS_destroy_lexer(SGSLexer *o) {
	if (o == NULL) return;
	fclose(o->file);
	free(o);
}

/*
 * Buffered reading implementation.
 */

/**
 * Swap active buffer (buf_start), filling the new one. Check for read
 * errors and EOF.
 *
 * When EOF is reached, or a read error occurs, the character after the
 * last one read (or the first if none) is set to either BUFC_EOF or
 * BUFC_ERROR, respectively. The return value of fread() determines the
 * position of the marker. buf_eof is set to point to the marker, having
 * been NULL previously.
 *
 * \return number of characters read.
 */
static size_t buf_fill(SGSLexer *o) {
	size_t read;
	/* Initialize active buffer to buf1, or swap if set. */
	o->buf_start = (o->buf_start == o->buf1) ? o->buf2 : o->buf1;
	read = fread(o->buf_start, 1, BUF_LEN, o->file);
	o->buf_read = o->buf_start;
	if (read < BUF_LEN) {
		/* Set marker immediately after any characters read.
		 */
		o->buf_eof = &o->buf_start[read];
		*o->buf_eof = (!ferror(o->file) && feof(o->file)) ?
			BUFC_EOF :
			BUFC_ERROR;
	}
	return read;
}

/**
 * True if end of active buffer reached.
 */
#define BUF_EOB(o) (((size_t)((o)->buf_read - (o)->buf_start)) == BUF_LEN)

/**
 * True if EOF reached or a read error has occurred. To find out which is
 * the case, examine the character retrieved at this position, which will
 * be either BUFC_EOF or BUFC_ERROR.
 *
 * A character can be equal to BUFC_EOF or BUFC_ERROR without the
 * condition having occurred, so BUF_EOF() should always be checked before
 * handling the situation.
 */
#define BUF_EOF(o) ((o)->buf_eof == (o)->buf_read)

/**
 * Get next character.
 *
 * In case of read error, BUFC_ERROR will be returned. In case of EOF
 * reached, BUFC_EOF will be returned.
 *
 * \return next character
 */
#define BUF_GETC(o) \
	((void)(BUF_EOB(o) && (buf_fill(o), --(o)->buf_read)), \
	 *++(o)->buf_read)

/**
 * Get next character without checking for nor handling the end of the
 * buffer being reached.
 *
 * In case of read error, BUFC_ERROR will be returned. In case of EOF
 * reached, BUFC_EOF will be returned.
 *
 * \return next character
 */
#define BUF_RAW_GETC(o) \
	(*++(o)->buf_read)

/**
 * Handle the end of the buffer being reached, assuming reads use
 * BUF_GETC_RAW().
 */
#define BUF_HANDLE_EOB(o) \
	((void)(buf_fill(o), --(o)->buf_read))

/*
 * Undo the getting of a character. Can only be used after reading a
 * character, and at most one time until the next read.
 */
#define BUF_UNGETC(o) \
	((void)(--(o)->buf_read))


/*
 * Message printing routines.
 */

enum {
	PRINT_FILE_INFO = 1<<0
};
static void print_stderr(SGSLexer *o, uint options, const char *prefix,
	const char *fmt, va_list ap) {
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			o->filename, o->line, o->line_pos);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}
void SGS_lexer_warning(SGSLexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}
void SGS_lexer_error(SGSLexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}

#define MARK_BLANK(o) do{\
	++(o)->line_pos;\
} while(0)

#define MARK_NEWLINE(o) do{\
	++(o)->line;\
	(o)->line_pos = 1;\
} while(0)

/*
 * The following macros are used to recognize types of characters.
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_BLANK(c) ((c) == ' ' || (c) == '\t')
#define IS_LINEB(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_IDHEAD(c) IS_ALPHA(c)
#define IS_IDTAIL(c) (IS_ALNUM(c) || (c) == '_')

static void handle_unknown_or_end(SGSLexer *o, uint8_t c) {
	SGSToken *t = &o->token;
	if (BUF_EOF(o)) {
		if (c == BUFC_EOF) {
			t->type = SGS_T_EOF;
			return;
		} else {
			t->type = SGS_T_ERROR;
			fprintf(stderr, "%s: error reading file\n",
				o->filename);
			return;
		}
	}
	t->type = SGS_T_UNKNOWN;
//	putchar(c);
}

static void handle_comment(SGSLexer *o, uint8_t c) {
}

static void handle_numeric_value(SGSLexer *o, uint8_t first_digit) {
	SGSToken *t = &o->token;
	uint32_t c = first_digit;
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = BUF_GETC(o);
	} while (IS_DIGIT(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	t->type = SGS_T_INTVALUE; /* XXX: dummy code */
	BUF_UNGETC(o);
}

#define STRALLOC_MAX UINT32_MAX
static void handle_identifier(SGSLexer *o, uint8_t id_head) {
	SGSToken *t = &o->token;
	const uint8_t *reg_str;
	uint8_t c = id_head;
#if 0
	uint8_t *str_start, *str_write = o->string;
	uint32_t i, str_len = 0;
	do {
		++len;
		if (BUF_EOB(o)) {
			uint8_t *str_start = o->buf_read - len;
			memcpy(str_write, str_start, len * sizeof(uint8_t));
			str_write += len;
			BUF_HANDLE_EOB(o);
		}
		c = BUF_RAW_GETC(o);

	} while (IS_IDTAIL(c) && ++i < (STRALLOC_MAX - 1));
	if (i == ((STRALLOC_MAX - 1)) {
		o->string[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
REG_STR:
	reg_str = (const uint8_t*)SGS_symtab_intern_str(o->symtab, (const char*)o->string, i);
	if (reg_str == NULL) {
		SGS_lexer_error(o, "failed to register string '%s'", o->string);
	}
	t->type = SGS_T_IDENTIFIER;
	BUF_UNGETC(o);

#else
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = BUF_GETC(o);
	} while (IS_IDTAIL(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	reg_str = (const uint8_t*)SGS_symtab_intern_str(o->symtab, (const char*)o->string, i);
	if (reg_str == NULL) {
		SGS_lexer_error(o, "failed to register string '%s'", o->string);
	}
	t->type = SGS_T_IDENTIFIER;
	BUF_UNGETC(o);
#endif
}

/**
 * Return the next token from the current file. The token is overwritten on
 * each call, so it must be copied before a new call if it is to be preserved.
 * Memory for the token is handled by the SGSLexer instance.
 *
 * Upon end of file, the SGS_T_EOF token is returned; upon any file-reading
 * error, the SGS_T_ERROR token is returned.
 *
 * See the SGSToken type and the tokens defined in lexer.h for more
 * information.
 * \return the address of the current token
 */
SGSToken *SGS_get_token(SGSLexer *o) {
	SGSToken *t = &o->token;
	uint8_t c;
	c = BUF_GETC(o);
TEST_1STCHAR:
	switch (c) {
	case /* NUL */ 0x00: /* BUFC_ERROR */
	case /* SOH */ 0x01:
	case /* STX */ 0x02:
	case /* ETX */ 0x03:
	case /* EOT */ 0x04:
	case /* ENQ */ 0x05:
	case /* ACK */ 0x06:
	case /* BEL */ '\a':
	case /* BS  */ '\b':
		goto HANDLE_UNKNOWN;
	case /* HT  */ '\t':
		goto HANDLE_BLANK;
	case /* LF  */ '\n':
		goto HANDLE_LINEBREAK;
	case /* VT  */ '\v':
		goto HANDLE_UNKNOWN;
	case /* FF  */ '\f':
		goto HANDLE_UNKNOWN;
	case /* CR  */ '\r':
		goto HANDLE_LINEBREAK;
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
		goto HANDLE_UNKNOWN;
	case ' ':
		goto HANDLE_BLANK;
	case '!':
	case '"':
	case '#':
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
		goto HANDLE_1CT;
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
		goto HANDLE_NUMERICVALUE;
	case ':':
	case ';':
	case '<':
	case '=':
	case '>':
	case '?':
	case '@':
		goto HANDLE_1CT;
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
		goto HANDLE_IDENTIFIER;
	case '[':
	case '\\':
	case ']':
	case '^':
	case '_':
	case '`':
		goto HANDLE_1CT;
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
		goto HANDLE_IDENTIFIER;
	case '{':
	case '|':
	case '}':
	case '~':
		goto HANDLE_1CT;
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
	case 0xFF: /* BUFC_EOF */
		goto HANDLE_UNKNOWN;
	/*
	 * Visited according to the case selection above.
	 */
	HANDLE_UNKNOWN:
		handle_unknown_or_end(o, c);
		break;
	HANDLE_BLANK:
		do {
			MARK_BLANK(o);
			c = BUF_GETC(o);
		} while (IS_BLANK(c));
		goto TEST_1STCHAR;
	HANDLE_LINEBREAK:
		do {
			MARK_NEWLINE(o);
			c = BUF_GETC(o);
		} while (IS_LINEB(c));
		goto TEST_1STCHAR;
	HANDLE_1CT:
		t->type = SGS_T_1CT(c);
		break;
	HANDLE_NUMERICVALUE:
		handle_numeric_value(o, c);
		break;
	HANDLE_IDENTIFIER:
		handle_identifier(o, c);
		break;
	}
	return t;
}

/* EOF */
