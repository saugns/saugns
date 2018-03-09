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

#include "reader.h"

/**
 * Fill the area of the buffer currently arrived at. This should be
 * called when indicated by SGS_READER_NEED_FILL().
 *
 * When EOF or a read error occurs, the file will be closed and
 * SGS_READER_STATUS() will return either SGS_READ_EOF or SGS_READ_ERROR.
 * The character after the last one successfully read will be set to 0.
 *
 * If the full length was filled, then the contents can be used normally;
 * otherwise, the first unused value will be set to 0; if the file was
 * closed, then the first value will be set to 0 the next call.
 *
 * When a value of 0 is encountered, or the returned length is less than
 * SGS_READ_LEN, check whether the file is NULL or if SGS_READER_STATUS()
 * is non-zero to detect the end and closing of the current file.
 *
 * \return number of characters read.
 */
size_t SGS_reader_fill(struct SGSReader *o) {
	size_t len;
	/*
	 * Read a buffer area's worth of data from the file.
	 *
	 * Set read_pos to the first character of the buffer area.
	 * If it has ended up outside of the buffer (fill position
	 * after last buffer area), bring it back to the first buffer
	 * area.
	 */
	o->read_pos &= (SGS_READ_BUFSIZ - 1) & ~(SGS_READ_LEN - 1);
	o->fill_pos = o->read_pos + SGS_READ_LEN; /* pre-mask pos */
	if (!o->file) {
		o->buf[o->read_pos] = 0;
		return 0;
	}
	len = fread(&o->buf[o->read_pos], 1, SGS_READ_LEN, o->file);
	if (ferror(o->file)) {
		o->read_status = SGS_READ_ERROR;
		o->buf[o->read_pos + len] = 0;
		fclose(o->file);
		o->file = NULL;
		return len;
	}
	if (feof(o->file)) {
		o->read_status = SGS_READ_EOF;
		o->buf[o->read_pos + len] = 0;
		fclose(o->file);
		o->file = NULL;
		return len;
	}
	return len;
}
