/* SAU library: Script lexer module.
 * Copyright (c) 2014, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include <sau/lexer.h>
#include <sau/scanner.h>
#include <sau/math.h>
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

/*
 * Lexer implementation.
 */

#define STRBUF_LEN 1024

struct sauLexer {
	sauScanner *sc;
	sauSymtab *symtab;
	sauScriptToken token;
};

/**
 * Create instance for the given symbol table.
 *
 * \return instance, or NULL on failure.
 */
sauLexer *sau_create_Lexer(sauSymtab *restrict symtab) {
	if (!symtab)
		return NULL;
	sauLexer *o = calloc(1, sizeof(sauLexer));
	if (!o)
		return NULL;
	o->sc = sau_create_Scanner(symtab);
	if (!o->sc) goto ERROR;
	o->symtab = symtab;
#if SAU_LEXER_QUIET
	o->sc->s_flags |= SAU_SCAN_S_QUIET;
#endif
	sauScanner_setws_level(o->sc, SAU_SCAN_WS_NONE);
	return o;
ERROR:
	sau_destroy_Lexer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void sau_destroy_Lexer(sauLexer *restrict o) {
	if (!o)
		return;
	sau_destroy_Scanner(o->sc);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around sauScanner functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool sauLexer_open(sauLexer *restrict o,
		const char *restrict script, bool is_path) {
	return sauScanner_open(o->sc, script, is_path);
}

/**
 * Close file (if open).
 */
void sauLexer_close(sauLexer *restrict o) {
	sauScanner_close(o->sc);
}

static void handle_invalid(sauLexer *o, uint8_t c sauMaybeUnused) {
	sauScriptToken *t = &o->token;
	t->type = SAU_T_INVALID;
	t->data.b = 0;
}

static void handle_eof(sauLexer *restrict o,
		uint8_t c sauMaybeUnused) {
	sauScanner *sc = o->sc;
	sauScriptToken *t = &o->token;
	t->type = SAU_T_INVALID;
	t->data.b = sauFile_STATUS(sc->f);
	//fputs("EOF", stderr);
}

static void handle_special(sauLexer *restrict o, uint8_t c) {
	sauScriptToken *t = &o->token;
	t->type = SAU_T_SPECIAL;
	t->data.c = c;
	//putc(c, stderr);
}

static void handle_numeric_value(sauLexer *restrict o,
		uint8_t c sauMaybeUnused) {
	sauScanner *sc = o->sc;
	sauScriptToken *t = &o->token;
	double d;
	sauScanner_ungetc(sc);
	sauScanner_getd(sc, &d, false, NULL, NULL);
	t->type = SAU_T_VAL_REAL;
	t->data.f = d;
	//fprintf(stderr, "num == %f\n", d);
}

static void handle_identifier(sauLexer *restrict o,
		uint8_t c sauMaybeUnused) {
	sauScanner *sc = o->sc;
	sauScriptToken *t = &o->token;
	sauSymstr *symstr;
	sauScanner_ungetc(sc);
	sauScanner_get_symstr(sc, &symstr);
	t->type = SAU_T_ID_STR;
	t->data.id = symstr ? symstr->key : NULL;
	//fprintf(stderr, "str == %s\n", str);
}

/**
 * Get the next token from the current file.
 *
 * Upon end of file, an SAU_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SAU_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool sauLexer_get(sauLexer *restrict o, sauScriptToken *restrict t) {
	sauScanner *sc = o->sc;
	uint8_t c;
REGET:
	c = sauScanner_getc(sc);
	switch (c) {
	case 0x00:
		handle_eof(o, c);
		break;
	case SAU_SCAN_LNBRK:
	case SAU_SCAN_SPACE:
		goto REGET;
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
		handle_special(o, c);
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
		handle_numeric_value(o, c);
		break;
	case ':':
	case ';':
	case '<':
	case '=':
	case '>':
	case '?':
	case '@':
		handle_special(o, c);
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
		handle_identifier(o, c);
		break;
	case '[':
	case '\\':
	case ']':
	case '^':
	case '_':
	case '`':
		handle_special(o, c);
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
		handle_identifier(o, c);
		break;
	case '{':
	case '|':
	case '}':
	case '~':
		handle_special(o, c);
		break;
	default:
		handle_invalid(o, c);
		break;
	}
	if (t != NULL) {
		*t = o->token;
	}
	return (c != 0);
}

/**
 * Get the next token from the current file. Interprets any visible ASCII
 * character as a special token character.
 *
 * Upon end of file, an SAU_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SAU_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool sauLexer_get_special(sauLexer *restrict o,
		sauScriptToken *restrict t) {
	sauScanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		c = sauScanner_getc(sc);
		if (c == 0) {
			handle_eof(o, c);
			break;
		}
		if (SAU_IS_ASCIIVISIBLE(c)) {
			handle_special(o, c);
			break;
		}
	}
	if (t != NULL) {
		*t = o->token;
	}
	return (c != 0);
}
