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

#include "sgensys.h"
#include "lexer.h"
#include "symtab.h"
#include "math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUF_LEN 4096
#define READ_ERROR '\0'

struct SGSLexer {
	char buf1[BUF_LEN],
	     buf2[BUF_LEN];
	char *buf_start,
	     *buf_read;
	FILE *file;
	const char *filename;
	uint line, line_pos;
	SGSToken token;
	struct SGSSymtab *symtab;
};

SGSLexer *SGS_create_lexer(const char *filename) {
	SGSLexer *o;
	FILE *file = fopen(filename, "r");
	if (file == NULL) return 0;

	o = calloc(1, sizeof(SGSLexer));
	if (o == NULL) return 0;
	o->buf_start = NULL;
	o->buf_read = o->buf_start + BUF_LEN; /* trigger initialization */
	o->file = file;
	o->filename = filename;
	return o;
}

void SGS_destroy_lexer(SGSLexer *o) {
	if (o == NULL) return;
	fclose(o->file);
	free(o);
}

static void print_error(SGSLexer *o, const char *msg) {
	fprintf(stderr, "%s:%d:%d: error: %s\n",
		o->filename, o->line, o->line_pos, msg);
}
static void print_warning(SGSLexer *o, const char *msg) {
	fprintf(stderr, "%s:%d:%d: warning: %s\n",
		o->filename, o->line, o->line_pos, msg);
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

SGSToken *SGS_get_token(SGSLexer *o) {
	SGSToken *t = &o->token;
	for (;;) {
		char c = buf_getc(o);
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

/* EOF */
