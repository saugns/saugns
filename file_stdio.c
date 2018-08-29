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
#include <stdio.h>

static size_t file_mode_fread(SGS_CBufMode *o); // read callback

static void file_ref_close(SGS_File *o);

/**
 * Open file for reading.
 *
 * The file (but not the stream) will automatically be closed upon EOF
 * or a read error; the name field will remain set to \p fname so as to
 * be available for printing until the stream is closed.
 *
 * The file reading callback returns 0 when the file has been closed.
 * If 0 is read in the buffer, check the status field to determine
 * whether or not the file is still open.
 *
 * \return true if successful
 */
bool SGS_File_openfrb(SGS_File *o, const char *fname) {
	SGS_File_close(o);

	FILE *f = fopen(fname, "rb");
	if (!f) return false;

	o->buf.r.call_pos = 0;
	o->buf.r.callback = file_mode_fread;
	o->ref = f;
	o->ref_closef = file_ref_close;
	o->name = fname;
	o->active = SGS_FILE_OPEN_R;
	o->status = SGS_FILE_OK;
	return true;
}

/*
 * Reading callback. Fills the area of the buffer currently arrived at
 * with contents from the currently opened file.
 *
 * When EOF or a read error occurs, the file will be closed and the
 * status set to either SGS_FILE_END or SGS_FILE_ERROR. If the file
 * ends before the full fill length, or was already closed, the first
 * character after the last one successfully read will be set to 0.
 *
 * When a value of 0 is encountered, or the returned length is less than
 * SGS_CBUF_ALEN, check the stream status to detect the end and closing of
 * the current file.
 *
 * \return number of characters successfully read
 */
static size_t file_mode_fread(SGS_CBufMode *o) {
	SGS_File *fo = (SGS_File*) o->ref;
	FILE *f = fo->ref;
	size_t len;
	/*
	 * Set read pos to the first character of the buffer area.
	 * If it has ended up outside of the buffer (fill after last
	 * character in buffer), bring it back to the first buffer
	 * area.
	 *
	 * Read a buffer area's worth of data from the file, if
	 * open. Upon short read, insert 0 value not counted in return
	 * length. Close file upon end or error.
	 */
	o->pos &= (SGS_CBUF_SIZ - 1) & ~(SGS_CBUF_ALEN - 1);
	if (!f) {
		fo->buf.buf[o->pos] = 0;
		o->call_pos = o->pos + 1;
		return 0;
	}
	len = fread(&fo->buf.buf[o->pos], 1, SGS_CBUF_ALEN, f);
	o->call_pos = o->pos + len; /* pre-mask pos */
	if (len < SGS_CBUF_ALEN) {
		fo->buf.buf[o->pos + len] = 0;
		++o->call_pos;
	}
	if (ferror(f)) {
		fo->status = SGS_FILE_ERROR;
		file_ref_close(fo);
	} else if (feof(f)) {
		fo->status = SGS_FILE_END;
		file_ref_close(fo);
	}
	return len;
}

/*
 * Close file.
 */
void file_ref_close(SGS_File *o) {
	if (o->ref) {
		fclose((FILE*) o->ref);
		o->ref = NULL;
	}
}
