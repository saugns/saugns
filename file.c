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

#include "file.h"
#include <stdlib.h>

/**
 * Initialize instance. Must only be called once before a
 * finalization.
 */
void SGS_init_File(SGS_File *o) {
	SGS_init_CBuf(&o->buf);
	o->ref = NULL;
	o->ref_closef = NULL;
	o->name = NULL;
	o->active = SGS_FILE_CLOSED;
	o->status = SGS_FILE_OK;
}

/**
 * Finalize instance. Must only be called once after each
 * initialization.
 */
void SGS_fini_File(SGS_File *o) {
	if (o->ref_closef) {
		o->ref_closef(o);
		o->ref_closef = NULL;
	}
	SGS_fini_CBuf(&o->buf);
}

/**
 * Close stream if open. Reset buffer read and write modes, but not
 * buffer contents.
 */
void SGS_File_close(SGS_File *o) {
	if (o->ref_closef) {
		o->ref_closef(o);
		o->ref_closef = NULL;
	}
	SGS_CBufMode_reset(&o->buf.r);
	SGS_CBufMode_reset(&o->buf.w);
	o->active = SGS_FILE_CLOSED;
	o->status = SGS_FILE_OK;
}

/**
 * Reset stream object, including the buffer, its contents and
 * read and write modes. If open, will be closed.
 */
void SGS_File_reset(SGS_File *o) {
	if (o->ref_closef) {
		o->ref_closef(o);
		o->ref_closef = NULL;
	}
	SGS_CBuf_reset(&o->buf);
	o->active = SGS_FILE_CLOSED;
	o->status = SGS_FILE_OK;
}

/**
 * Read \p n characters into \p buf, or stop upon encountering a 0 byte.
 * Null-terminates \p buf, which must be at least \p n + 1 bytes long.
 * Returns the number of characters read (not including a terminating 0).
 *
 * If the returned length is less than \p n, a 0 byte was enountered
 * before n characters were read; check the stream status to see if the
 * file is still open or has been closed. (Regardless of the status, it
 * is still safe to unget the characters read.)
 *
 * \return number of characters read, not including a terminating 0
 */
size_t SGS_File_getstrn(SGS_File *o, void *dst, size_t n) {
	uint8_t *buf = dst;
	if (buf == NULL) return 0;
	size_t i = 0;
	for (;;) {
		if (i == n) break;
		uint8_t c = SGS_CBuf_GETC(&o->buf);
		if (c == 0) break;
		buf[i++] = c;
	}
	buf[i] = 0;
	return i;
}
