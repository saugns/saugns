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
#include "../common.h"

/*
 * Buffered reading implementation (uses circular buffer).
 *
 * Faster than handling characters one by one using stdio.
 */

#define SGS_READ_LEN      4096
#define SGS_READ_BUFAREAS 2
#define SGS_READ_BUFSIZ   (SGS_READ_LEN * SGS_READ_BUFAREAS)

/*
 * File reading status.
 *
 * Changed when EOF reached or file reading error occurs.
 */
enum {
	SGS_READ_OK = 0,
	SGS_READ_EOF,
	SGS_READ_ERROR
};

typedef struct SGS_File {
	uint8_t buf[SGS_READ_BUFSIZ];
	size_t read_pos;
	size_t fill_pos;
	uint8_t read_status;
	void *ref;
	const char *path;
} SGS_File;

bool SGS_File_openrb(SGS_File *o, const char *path);
void SGS_File_close(SGS_File *o);

/**
 * Flip to using the next buffer area.
 */
#define SGS_File_SWAP_BUFAREA(o) \
	((o)->read_pos = ((o)->read_pos + SGS_READ_LEN) \
	                  & (SGS_READ_BUFSIZ - 1))

/**
 * Position relative to buffer area.
 */
#define SGS_File_BUFAREA_POS(o) \
	((o)->read_pos & (SGS_READ_LEN - 1))

/**
 * True if end of buffer area last filled reached.
 */
#define SGS_File_NEED_FILL(o) \
	((o)->read_pos == (o)->fill_pos)

/**
 * Check if reader needs fill and fill if needed. Done automatically
 * in the SGS_File_GETC(), SGS_File_TESTC(), etc., macros.
 */
#define SGS_File_PREPARE(o) \
	((void)(SGS_File_NEED_FILL(o) && SGS_File_fill(o)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The value will indicate which.
 */
#define SGS_File_STATUS(o) \
	((o)->read_status)

/**
 * Increment position. No checking is done. Can be used
 * after SGS_File_RETC() as an alternative to SGS_File_GETC().
 *
 * \return new reading position
 */
#define SGS_File_INCP(o) (++(o)->read_pos)

/**
 * Decrement position. No checking is done.
 *
 * \return new reading position
 */
#define SGS_File_DECP(o) (--(o)->read_pos)

/**
 * Get next character, advancing reading position after retrieval.
 *
 * If a value of 0 is returned, check SGS_File_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_File_GETC(o) \
	(SGS_File_PREPARE(o), \
	 (o)->buf[(o)->read_pos++])

/**
 * Get next character without checking buffer area boundaries
 * nor handling further filling of the buffer, advancing reading
 * position after retrieval.
 *
 * If a value of 0 is returned, check SGS_File_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_File_GETC_NOCHECK(o) \
	((o)->buf[(o)->read_pos++])

/**
 * Undo the getting of a character. This can safely be done a number of
 * times equal to (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 *
 * Ungetting can be done regardless of SGS_File_STATUS(). The next read
 * (unless using unchecked macro or function) will handle the condition
 * properly.
 */
#define SGS_File_UNGETC(o) \
	SGS_File_UNGETN(o, 1)

/**
 * Get next character, without advancing reading position.
 *
 * If a value of 0 is returned, check SGS_File_STATUS() to see
 * if the file is still open or has been closed.
 *
 * \return next character
 */
#define SGS_File_RETC(o) \
	(SGS_File_PREPARE(o), \
	 (o)->buf[(o)->read_pos])

/**
 * Compare current character to value c, without advancing reading
 * position.
 *
 * \return true if equal
 */
#define SGS_File_TESTC(o, c) \
	(SGS_File_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)))

/**
 * Compare current character to value c, advancing reading position
 * if equal.
 *
 * \return true if equal
 */
#define SGS_File_TRYC(o, c) \
	(SGS_File_PREPARE(o), \
	 ((o)->buf[(o)->read_pos] == (c)) && (++(o)->read_pos, true))

/**
 * Undo the getting of n number of characters. This can safely be done
 * for n <= (SGS_READ_LEN - 1) plus the number of characters gotten
 * within the current buffer area.
 *
 * Ungetting can be done regardless of SGS_File_STATUS(). The next read
 * (unless using unchecked macro or function) will handle the condition
 * properly.
 */
#define SGS_File_UNGETN(o, n) \
	((o)->read_pos = ((o)->read_pos - (n)) & (SGS_READ_BUFSIZ - 1))

size_t SGS_File_fill(SGS_File *o);
