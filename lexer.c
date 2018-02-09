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
 * <http://www.gnu.org/licenses/>.
 */

#include "lexer.h"
#include "math.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define BUF_LEN (BUFSIZ << 1)

#define STRING_MAX_LEN 1024

struct SGSLexer {
	uint8_t buf[BUF_LEN];
	size_t read_pos;
	size_t fill_pos;
	uint8_t *read_exception;
	FILE *file;
	const char *filename;
	SGSSymtab *symtab;
	uint32_t line_num, char_num;
	SGSToken token;
	uint8_t string[STRING_MAX_LEN];
};

/**
 * Create SGSLexer for the given file and using the given symbol
 * table.
 *
 * \return instance, or NULL on failure.
 */
SGSLexer *SGS_create_lexer(const char *filename, SGSSymtab *symtab) {
	SGSLexer *o;
	if (symtab == NULL) return NULL;
	FILE *file = fopen(filename, "rb");
	if (file == NULL) return NULL;

	o = calloc(1, sizeof(SGSLexer));
	if (o == NULL) return NULL;
	o->file = file;
	o->filename = filename;
	o->symtab = symtab;
	return o;
}

/**
 * Destroy instance, also closing the file for which the SGSLexer
 * was made.
 */
void SGS_destroy_lexer(SGSLexer *o) {
	if (o == NULL) return;
	fclose(o->file);
	free(o);
}


/*
 * Buffered reading implementation (uses circular buffer).
 */

#define READ_LEN   (BUF_LEN >> 1)

/*
 * Markers which may be inserted into the buffer.
 */
enum {
	READ_EOF = 1,
	READ_ERROR
};

/*
 * Flip to using the next buffer area.
 */
#define READ_SWAP_BUFAREA(o) \
	((o)->read_pos = ((o)->read_pos + READ_LEN) & (BUF_LEN - 1))

/*
 * Position relative to buffer area.
 */
#define READ_BUFAREA_POS(o) \
	((o)->read_pos & (READ_LEN - 1))

/*
 * True if end of buffer area last filled reached.
 */
#define READ_NEED_FILL(o) \
	((o)->read_pos == (o)->fill_pos)

/*
 * Fill the area of the buffer currently arrived at. This should be
 * called when indicated by READ_NEED_FILL().
 *
 * Checks for read errors and EOF.
 *
 * When handling EOF or a read error, the buffer will be at most
 * partially filled. (If it is fully filled, it can be used normally,
 * the handling of EOF or error taking place during the next call.)
 * The first unused position in the buffer is then set to a marker
 * (either READ_EOF or READ_ERROR, depending on the condition) and
 * pointed to by buf_marker.
 *
 * \return number of characters read.
 */
static size_t read_fill_bufarea(SGSLexer *o) {
	size_t read;
	/*
	 * Read a buffer area's worth of data from the file.
	 *
	 * Set read_pos to the first character of the buffer area.
	 * If it has ended up outside of the buffer (fill position
	 * after last buffer area), bring it back to the first buffer
	 * area.
	 */
	o->read_pos &= (BUF_LEN - 1) & ~(READ_LEN - 1);
	o->fill_pos = o->read_pos + READ_LEN; /* pre-mask pos */
	read = fread(&o->buf[o->read_pos], 1, READ_LEN, o->file);
	/*
	 * If buffer not full, insert marker immediately after last
	 * character.
	 */
	if (read < READ_LEN) {
		o->read_exception = &o->buf[o->read_pos + read];
		*o->read_exception = (!ferror(o->file) && feof(o->file)) ?
			READ_EOF :
			READ_ERROR;
	}
	return read;
}

/*
 * Get next character without checking buffer area boundaries
 * nor handling further filling of the buffer.
 *
 * \return next character
 */
#define READ_GETC_NOCHECK(o) \
	((o)->buf[(o)->read_pos++])

/*
 * Undo the getting of a character without checking buffer area
 * boundaries.
 *
 * \return new read position
 */
#define READ_UNGETC_NOCHECK(o) \
	(--(o)->read_pos)

/*
 * True if EOF reached or a read error has occurred. To find out which is
 * the case, examine the character retrieved at this position, which will
 * be either READ_EOF or READ_ERROR.
 *
 * A character can be equal to READ_EOF or READ_ERROR without the condition
 * having occurred, so READ_EXCEPTION() should always be checked before
 * handling the situation.
 */
#define READ_EXCEPTION(o) \
	((o)->read_exception)

/*
 * Get next character.
 *
 * In case of EOF or read error, READ_EOF or READ_ERROR,
 * respectively, will be returned. Use READ_EXCEPTION() to
 * distinguish between such status indicators and normal data.
 *
 * \return next character
 */
#define READ_GETC(o) \
	((void)(READ_NEED_FILL(o) && read_fill_bufarea(o)), \
	 (o)->buf[(o)->read_pos++])

/*
 * Undo the getting of a character. This can safely be done a number of
 * times equal to (READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 */
#define READ_UNGETC(o) \
	((o)->read_pos = ((o)->read_pos - 1) & (BUF_LEN - 1))


/*
 * Message printing routines.
 */

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SGSLexer *o, uint32_t options, const char *prefix,
	const char *fmt, va_list ap) {
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			o->filename, o->line_num, o->char_num);
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
void SGS_lexer_warning(SGSLexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file name and current position.
 */
void SGS_lexer_error(SGSLexer *o, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}


/*
 * Lexer implementation.
 */

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
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_IDHEAD(c) IS_ALPHA(c)
#define IS_IDTAIL(c) (IS_ALNUM(c) || (c) == '_')

static void handle_unknown_or_end(SGSLexer *o, uint8_t c) {
	SGSToken *t = &o->token;
	if (READ_EXCEPTION(o)) {
		if (c == READ_EOF) {
			t->type = SGS_T_EOF;
			return;
		} else {
			t->type = SGS_T_ERROR;
			SGS_lexer_error(o, "file reading failed");
			return;
		}
	}
	t->type = SGS_T_INVALID;
#if !SGS_LEXER_QUIET
	SGS_lexer_warning(o, "invalid character (value 0x%hhx)", c);
#endif
}

static uint8_t handle_blanks(SGSLexer *o, uint8_t c) {
	do {
		++o->char_num;
		c = READ_GETC(o);
	} while (IS_BLANK(c));
	return c;
}

static uint8_t handle_linebreaks(SGSLexer *o, uint8_t c) {
	do {
		uint8_t nl, nc;

		nl = (c == '\n');
		++o->line_num;
		o->char_num = 1;
	NEXTC:
		nc = READ_GETC(o);
		if (nl && (nc == '\r')) {
			nl = 0;
			goto NEXTC; /* finish DOS-format newline */
		}
		c = nc;
	} while (IS_LNBRK(c));
	return c;
}

static uint8_t handle_line_comment(SGSLexer *o) {
	uint8_t c;
	do {
		c = READ_GETC(o);
	} while (!IS_LNBRK(c));
	return handle_linebreaks(o, c);
}

static void handle_block_comment(SGSLexer *o, uint8_t c) {
	SGS_lexer_warning(o, "cannot yet handle comment marked by '%c'", c);
}

static void handle_numeric_value(SGSLexer *o, uint8_t first_digit) {
	SGSToken *t = &o->token;
	uint32_t c = first_digit;
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = READ_GETC(o);
	} while (IS_DIGIT(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// warn and handle too-long-string
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	t->type = SGS_T_INT_NUM; /* XXX: dummy code */
	READ_UNGETC(o);
}

static void handle_identifier(SGSLexer *o, uint8_t id_head) {
	SGSToken *t = &o->token;
	const char *pool_str;
	uint8_t c = id_head;
	uint32_t i = 0;
	do {
		o->string[i] = c;
		c = READ_GETC(o);
	} while (IS_IDTAIL(c) && ++i < (STRING_MAX_LEN - 1));
	if (i == (STRING_MAX_LEN - 1)) {
		o->string[i] = '\0';
		// TODO: warn and handle too-long-string
		SGS_lexer_error(o, "cannot handle string longer than %d characters", STRING_MAX_LEN);
	} else { /* string ended gracefully */
		++i;
		o->string[i] = '\0';
	}
	pool_str = SGS_symtab_pool_str(o->symtab, (const char*)o->string, i);
	if (pool_str == NULL) {
		SGS_lexer_error(o, "failed to register string '%s'", o->string);
	}
	t->type = SGS_T_ID_STR;
	t->data.id = pool_str;
	READ_UNGETC(o);
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
	c = READ_GETC(o);
TEST_1STCHAR:
	switch (c) {
	case /* NUL */ 0x00: /* READ_ERROR */
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
		goto HANDLE_1CT;
	case '#':
		goto HANDLE_LINE_COMMENT;
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
	case 0xFF: /* READ_EOF */
		goto HANDLE_UNKNOWN;
	/*
	 * Visited according to the case selection above.
	 */
	HANDLE_UNKNOWN:
		handle_unknown_or_end(o, c);
		break;
	HANDLE_BLANK:
		c = handle_blanks(o, c);
		goto TEST_1STCHAR;
	HANDLE_LINEBREAK:
		c = handle_linebreaks(o, c);
		goto TEST_1STCHAR;
	HANDLE_LINE_COMMENT:
		c = handle_line_comment(o);
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
