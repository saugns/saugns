/* sgensys: script lexer module.
 * Copyright (c) 2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
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

#define BUF_LEN 4096
#define STRING_MAX_LEN 128
#define READ_ERROR '\0'

struct SGSLexer {
	char buf1[BUF_LEN],
	     buf2[BUF_LEN];
	char *buf_start,
	     *buf_read;
	FILE *file;
	const char *filename;
	SGSSymtab *symtab;
	uint line, line_pos;
	SGSToken token;
	char string[STRING_MAX_LEN];
};

SGSLexer *SGS_create_lexer(const char *filename, SGSSymtab *symtab) {
	SGSLexer *o;
	if (symtab == NULL) return NULL;
	FILE *file = fopen(filename, "r");
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

/**
 * Swap active buffer (buf_start), filling the new one. Check for
 * read errors and EOF.
 *
 * In case of read error, the first character of the new active buffer is set
 * to READ_ERROR. In case of EOF, the character after the last one read (or the
 * first if none) is set to EOF.
 *
 * \return number of characters read.
 */
static size_t fill_buf(SGSLexer *o) {
	/* Initialize active buffer to buf1, or swap if set. */
	o->buf_start = (o->buf_start == o->buf1) ? o->buf2 : o->buf1;
	size_t read = fread(o->buf_start, 1, BUF_LEN, o->file);
	o->buf_read = o->buf_start;
	if (read < BUF_LEN) {
		if (ferror(o->file)) {
			o->buf_read = READ_ERROR;
		} else if (feof(o->file)) {
			/* Set character after the last read, or the first if
			 * none, to EOF.
			 */
			o->buf_read[read] = EOF;
		}
	}
	return read;
}

/**
 * True if end of active buffer reached.
 */
#define END_OF_BUF(o) (((size_t)((o)->buf_read - (o)->buf_start)) == BUF_LEN)

/**
 * Get next character.
 *
 * In case of read error, READ_ERROR will be returned. In case of EOF reached,
 * EOF will be returned.
 *
 * \return next character
 */
static char buf_getc(SGSLexer *o) {
	if (END_OF_BUF(o)) {
		fill_buf(o);
	}
	return *o->buf_read++;
}

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

#define MARK_END_OF_LINE(o) do{\
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
	for (;;) {
		char c = buf_getc(o);
		/*
		 * Handle keywords and idenitifiers
		 */
		if (IS_IDHEAD(c)) {
			int id;
			const char *reg_str;
			int i = 0;
			do {
				o->string[i] = c;
				c = buf_getc(o);
			} while (IS_IDTAIL(c) && ++i < (STRING_MAX_LEN - 1));
			if (i == (STRING_MAX_LEN - 1)) {
				o->string[i] = '\0';
				// warn and handle too-long-string
			} else { /* string ended gracefully */
				++i;
				o->string[i] = '\0';
			}
			id = SGS_symtab_register_str(o->symtab, o->string);
			if (id < 0) {
				SGS_lexer_error(o, "failed to register string '%s'", o->string);
			}
#if 0
			id = SGS_symtab_register_str(o->symtab, o->string);
			SGS_lexer_warning(o, "'%s' (id=%d)", o->string, id);
			if (id < 0) SGS_lexer_error(o, "string registration failed");
			SGS_lexer_warning(o, SGS_symtab_lookup_str(o->symtab, id));
#endif
		}
		switch (c) {
		case EOF:
			t->type = SGS_T_EOF;
			return t;
		case READ_ERROR:
			t->type = SGS_T_ERROR;
			fprintf(stderr, "%s: error reading file\n",
				o->filename);
			return t;
		default:
			putchar(c);
			break;
		}
	}
	return t;
}
