/* sgensys: Text file reader module.
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

#pragma once
#include "sgensys.h"
#include <string.h>
#include <stdio.h>

/*
 * Buffered reading implementation (uses circular buffer).
 * Meant for scanning, lexing, parsing.
 *
 * Wrapper around stdio which is faster than directly handling
 * characters one by one, and implements some convenient
 * functionality.
 */

#define SGS_READ_LEN      4096
#define SGS_READ_BUFAREAS 2
#define SGS_READ_BUFSIZ   (SGS_READ_LEN * SGS_READ_BUFAREAS)

/*
 * File reading status. Changed when EOF reached or
 * file reading error occurs.
 */
enum {
	SGS_READ_OK = 0,
	SGS_READ_EOF,
	SGS_READ_ERROR
};

struct SGS_FRead {
	char buf[SGS_READ_BUFSIZ];
	size_t read_pos;
	size_t fill_pos;
	uint8_t read_status;
	const char *filename;
	FILE *file;
};

/**
 * Open file.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but filename is only cleared with an explicit SGS_FREAD_CLOSE()
 * call (so as to be available for printing).
 */
#define SGS_FREAD_OPEN(o, fname) ((bool) \
	((o)->filename = (fname)) && \
	((o)->file = fopen((fname), "rb")))

/**
 * Close file.
 */
#define SGS_FREAD_CLOSE(o) do{ \
	(o)->filename = NULL; \
	if ((o)->file != NULL) { \
		fclose((o)->file); \
		(o)->file = NULL; \
	} \
}while(0)

/**
 * Flip to using the next buffer area.
 */
#define SGS_FREAD_SWAP_BUFAREA(o) \
	((o)->read_pos = ((o)->read_pos + SGS_READ_LEN) \
	                  & (SGS_READ_BUFSIZ - 1))

/**
 * Position relative to buffer area.
 */
#define SGS_FREAD_BUFAREA_POS(o) \
	((o)->read_pos & (SGS_READ_LEN - 1))

/**
 * True if end of buffer area last filled reached.
 */
#define SGS_FREAD_NEED_FILL(o) \
	((o)->read_pos == (o)->fill_pos)

/**
 * Check if reader needs fill and fill if needed. Done automatically
 * in the SGS_FREAD_GETC(), SGS_FREAD_TESTC(), etc., macros.
 */
#define SGS_FREAD_PREPARE(o) \
	((void)(SGS_FREAD_NEED_FILL(o) && SGS_fread_fill(o)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The value will indicate which.
 */
#define SGS_FREAD_STATUS(o) \
	((o)->read_status)

/**
 * Increment position. No checking is done. Can be used
 * after SGS_FREAD_RETC() as an alternative to SGS_FREAD_GETC().
 *
 * \return new reading position
 */
#define SGS_FREAD_INCP(o) (++(o)->read_pos)

/**
 * Decrement position. No checking is done.
 *
 * \return new reading position
 */
#define SGS_FREAD_DECP(o) (--(o)->read_pos)

/**
 * Get next character, advancing reading position after retrieval.
 *
 * If a value of 0 is returned, check SGS_FREAD_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_FREAD_GETC(o) \
	(SGS_FREAD_PREPARE(o), \
	 (o)->buf[(o)->read_pos++])

/**
 * Get next character without checking buffer area boundaries
 * nor handling further filling of the buffer, advancing reading
 * position after retrieval.
 *
 * If a value of 0 is returned, check SGS_FREAD_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_FREAD_GETC_NOCHECK(o) \
	((o)->buf[(o)->read_pos++])

/**
 * Undo the getting of a character. This can safely be done a number of
 * times equal to (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 *
 * Ungetting can be done regardless of SGS_FREAD_STATUS(). The next read
 * (unless using unchecked macro or function) will handle the condition
 * properly.
 */
#define SGS_FREAD_UNGETC(o) \
	SGS_FREAD_UNGETN(o, 1)

/**
 * Get next character, without advancing reading position.
 *
 * If a value of 0 is returned, check SGS_FREAD_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_FREAD_RETC(o) \
	(SGS_FREAD_PREPARE(o), \
	 (o)->buf[(o)->read_pos])

/**
 * Compare current character to value c, without advancing reading
 * position.
 *
 * \return true if equal
 */
#define SGS_FREAD_TESTC(o, c) \
	(SGS_FREAD_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)))

/**
 * Compare current character to value c, advancing reading position
 * if equal.
 *
 * \return true if equal
 */
#define SGS_FREAD_TESTCGET(o, c) \
	(SGS_FREAD_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)) && (++(o)->read_pos, true))

bool SGS_fread_getn(struct SGS_FRead *o, char *buf, size_t *n);

/**
 * Undo the getting of n number of characters. This can safely be done
 * for n <= (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 *
 * Ungetting can be done regardless of SGS_FREAD_STATUS(). The next read
 * (unless using unchecked macro or function) will handle the condition
 * properly.
 */
#define SGS_FREAD_UNGETN(o, n) \
	((o)->read_pos = ((o)->read_pos - (n)) & (SGS_READ_BUFSIZ - 1))

size_t SGS_fread_fill(struct SGS_FRead *o);
