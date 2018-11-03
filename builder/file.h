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
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "../cbuf.h"

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most SGS_File_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	SGS_File_OK = 0,
	SGS_File_END = 1<<0,
	SGS_File_ERROR = 1<<1,
	SGS_File_MARKER = 0x07,
};

struct SGS_File;

/**
 * Callback type for closing internal reference.
 * Should close file and reset \a ref, otherwise
 * leaving state unchanged.
 */
typedef void (*SGS_FileCloseRef_f)(struct SGS_File *o);

/**
 * File type using SGS_CBuf for buffered reading.
 *
 * Faster than handling characters one by one using stdio.
 * Meant for scanning and parsing.
 */
typedef struct SGS_File {
	SGS_CBuf cb;
	uint8_t status;
	uint8_t *end_marker;
	void *ref;
	const char *path;
	SGS_FileCloseRef_f close_f;
} SGS_File;

bool SGS_init_File(SGS_File *o);
void SGS_fini_File(SGS_File *o);

bool SGS_File_fopenrb(SGS_File *o, const char *path);

void SGS_File_close(SGS_File *o);
void SGS_File_reset(SGS_File *o);

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * SGS_File_AT_EOF()/SGS_File_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define SGS_File_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SGS_File_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define SGS_File_AT_EOF(o) \
	((o)->end_marker == ((o)->cb.buf + (o)->cb.r.pos))

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SGS_File_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define SGS_File_AFTER_EOF(o) \
	((o)->end_marker == ((o)->cb.buf + (o)->cb.r.pos - 1))
