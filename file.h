/* sgensys: File module.
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
#include "cbuf.h"

/*
 * File I/O type using SGS_CBuf.
 */

/**
 * File active flags. Changed when something is opened or closed
 * for the object.
 */
enum {
	SGS_FILE_CLOSED = 0,
	SGS_FILE_OPEN_R = 1<<0,
	SGS_FILE_OPEN_W = 1<<1,
	SGS_FILE_OPEN_RW = SGS_FILE_OPEN_R | SGS_FILE_OPEN_W,
};

/*
 * File status. Whether open or closed, zero indicates everything is
 * fine. Non-zero indicates that an opened file no longer works and
 * should be closed.
 */
enum {
	SGS_FILE_OK = 0,
	SGS_FILE_END,
	SGS_FILE_ERROR,
};

struct SGS_File;
typedef struct SGS_File SGS_File;

/**
 * Close I/O ref callback type. A function which does whatever is
 * necessary before resetting or finalizing the file object.
 */
typedef void (*SGS_FileCloseRef_f)(SGS_File *o);

struct SGS_File {
	SGS_CBuf buf;
	void *ref;
	SGS_FileCloseRef_f ref_closef;
	const char *name;
	uint8_t active;
	uint8_t status;
};

void SGS_init_File(SGS_File *o);
void SGS_fini_File(SGS_File *o);

void SGS_File_close(SGS_File *o);
void SGS_File_reset(SGS_File *o);

size_t SGS_File_getstrn(SGS_File *o, void *dst, size_t n);

/*
 * Wrapper around stdio for the File type.
 *
 * Makes for faster handling of characters one-by-one than directly
 * getting/ungetting characters, and CBuf implements some functionality
 * convenient for scanning.
 *
 * When reading, the value 0 is used to mark the end of an opened file,
 * but may also be read for other reasons; the status field will make the
 * case clear.
 *
 * Currently only supports reading.
 */

bool SGS_File_openfrb(SGS_File *o, const char *fname);
