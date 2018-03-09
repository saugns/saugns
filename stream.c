/* sgensys: Stream module.
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

#include "stream.h"
#include <stdlib.h>

/**
 * Initialize instance. Must only be called once before a
 * finalization.
 */
void SGS_init_Stream(SGS_Stream *o) {
	SGS_init_CBuf(&o->buf);
	o->io_ref = NULL;
	o->close_ref = NULL;
	o->name = NULL;
	o->active = SGS_STREAM_CLOSED;
	o->status = SGS_STREAM_OK;
}

/**
 * Finalize instance. Must only be called once after each
 * initialization.
 */
void SGS_fini_Stream(SGS_Stream *o) {
	if (o->close_ref) {
		o->close_ref(o);
		o->close_ref = NULL;
	}
	SGS_fini_CBuf(&o->buf);
}

/**
 * Close stream if open. Reset buffer read and write modes, but not
 * buffer contents.
 */
void SGS_Stream_close(SGS_Stream *o) {
	if (o->close_ref) {
		o->close_ref(o);
		o->close_ref = NULL;
	}
	SGS_CBufMode_reset(&o->buf.r);
	SGS_CBufMode_reset(&o->buf.w);
	o->active = SGS_STREAM_CLOSED;
	o->status = SGS_STREAM_OK;
}

/**
 * Reset stream object, including the buffer, its contents and
 * read and write modes. If open, will be closed.
 */
void SGS_Stream_reset(SGS_Stream *o) {
	if (o->close_ref) {
		o->close_ref(o);
		o->close_ref = NULL;
	}
	SGS_CBuf_reset(&o->buf);
	o->active = SGS_STREAM_CLOSED;
	o->status = SGS_STREAM_OK;
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
size_t SGS_Stream_getstrn(SGS_Stream *o, char *buf, size_t n) {
	if (buf == NULL) return 0;
	size_t i = 0;
	for (;;) {
		if (i == n) break;
		char c = SGS_CBuf_GETC(&o->buf);
		if (c == 0) break;
		buf[i++] = c;
	}
	buf[i] = 0;
	return i;
}
