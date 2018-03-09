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
 *
 * Faster than handling characters one by one using stdio.
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

struct SGSReader {
	uint8_t buf[SGS_READ_BUFSIZ];
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
 * but filename is only cleared with an explicit SGS_READER_CLOSE()
 * call (so as to be available for printing).
 */
#define SGS_READER_OPEN(o, fname) ((bool) \
	((o)->filename = (fname)) && \
	((o)->file = fopen((fname), "rb")))

/**
 * Close file.
 */
#define SGS_READER_CLOSE(o) do{ \
	(o)->filename = NULL; \
	if ((o)->file != NULL) { \
		fclose((o)->file); \
		(o)->file = NULL; \
	} \
}while(0)

/**
 * Flip to using the next buffer area.
 */
#define SGS_READER_SWAP_BUFAREA(o) \
	((o)->read_pos = ((o)->read_pos + SGS_READ_LEN) \
	                  & (SGS_READ_BUFSIZ - 1))

/**
 * Position relative to buffer area.
 */
#define SGS_READER_BUFAREA_POS(o) \
	((o)->read_pos & (SGS_READ_LEN - 1))

/**
 * True if end of buffer area last filled reached.
 */
#define SGS_READER_NEED_FILL(o) \
	((o)->read_pos == (o)->fill_pos)

/**
 * Check if reader needs fill and fill if needed. Done automatically
 * in the SGS_READER_GETC(), SGS_READER_TESTC(), etc., macros.
 */
#define SGS_READER_PREPARE(o) \
	((void)(SGS_READER_NEED_FILL(o) && SGS_reader_fill(o)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The value will indicate which.
 */
#define SGS_READER_STATUS(o) \
	((o)->read_status)

/**
 * Manually increment reading position. No checking is done.
 *
 * \return new reading position
 */
#define SGS_READER_INCP(o) (++(o)->read_pos)

/**
 * Manually decrement reading position. No checking is done.
 *
 * \return new reading position
 */
#define SGS_READER_DECP(o) (--(o)->read_pos)

/**
 * Get next character.
 *
 * If a value of 0 is returned, check SGS_READER_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_READER_GETC(o) \
	(SGS_READER_PREPARE(o), \
	 (o)->buf[(o)->read_pos++])

/**
 * Get next character without checking buffer area boundaries
 * nor handling further filling of the buffer.
 *
 * If a value of 0 is returned, check SGS_READER_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_READER_GETC_NOCHECK(o) \
	((o)->buf[(o)->read_pos++])

/**
 * Compare current character to value c without advancing further.
 *
 * \return true if equal
 */
#define SGS_READER_TESTC(o, c) \
	(SGS_READER_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)))

/**
 * Compare current character to value c, advancing if equal.
 *
 * \return true if equal
 */
#define SGS_READER_TESTCGET(o, c) \
	(SGS_READER_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)) && (++(o)->read_pos, true))

/**
 * Undo the getting of n number of characters. This can safely be done
 * for n <= (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 */
#define SGS_READER_UNGETN(o, n) \
	((o)->read_pos = ((o)->read_pos - (n)) & (SGS_READ_BUFSIZ - 1))

/**
 * Undo the getting of a character. This can safely be done a number of
 * times equal to (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 */
#define SGS_READER_UNGETC(o) \
	SGS_READER_UNGETN(o, 1)

size_t SGS_reader_fill(struct SGSReader *o);
