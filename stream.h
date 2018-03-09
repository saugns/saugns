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

#pragma once
#include "cbuf.h"

/*
 * Stream I/O type using SGS_CBuf.
 */

/**
 * Stream active flags. Changed when something is opened or closed
 * for the object.
 */
enum {
	SGS_STREAM_CLOSED = 0,
	SGS_STREAM_OPEN_R = 1<<0,
	SGS_STREAM_OPEN_W = 1<<1,
	SGS_STREAM_OPEN_RW = SGS_STREAM_OPEN_R | SGS_STREAM_OPEN_W,
};

/*
 * Stream status. Whether open or closed, zero indicates everything is
 * fine. Non-zero indicates that an opened stream no longer works and
 * should be closed.
 */
enum {
	SGS_STREAM_OK = 0,
	SGS_STREAM_END,
	SGS_STREAM_ERROR,
};

struct SGS_Stream;
typedef struct SGS_Stream SGS_Stream;

/**
 * Close I/O ref callback type. A function which does whatever is
 * necessary before resetting or finalizing the stream object.
 */
typedef void (*SGS_StreamCloseRef_f)(SGS_Stream *o);

struct SGS_Stream {
	SGS_CBuf buf;
	void *io_ref;
	SGS_StreamCloseRef_f close_ref;
	const char *name;
	uint8_t active;
	uint8_t status;
};

void SGS_init_Stream(SGS_Stream *o);
void SGS_fini_Stream(SGS_Stream *o);

void SGS_Stream_close(SGS_Stream *o);
void SGS_Stream_reset(SGS_Stream *o);

size_t SGS_Stream_getstrn(SGS_Stream *o, char *buf, size_t n);
