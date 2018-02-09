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

#include "file.h"
#include <string.h>
#include <stdio.h>

static void file_ref_close(SGS_File *o);

/**
 * Open file.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with an explicit SGS_File_close()
 * call (so as to be available for printing).
 *
 * \return true on success
 */
bool SGS_File_openrb(SGS_File *o, const char *path) {
	if (!path) return false;
	o->ref = fopen(path, "rb");
	if (!o->ref) return false;
	o->status = SGS_File_OK;
	o->end_marker = NULL;
	o->path = path;
	return true;
}

/**
 * Close file.
 */
void SGS_File_close(SGS_File *o) {
	file_ref_close(o);
	o->path = NULL;
}

/**
 * Fill the area of the buffer currently arrived at. This should be
 * called when indicated by SGS_File_NEED_FILL().
 *
 * When EOF or a read error occurs, the file will be closed and
 * the first character after the last one successfully read will
 * be assigned an end marker value. Further calls will reset the
 * reading position and write the end marker again.
 *
 * SGS_File_STATUS() will return the same value as the end marker,
 * which is always <= SGS_File_MARKER.
 *
 * \return number of characters successfully read
 */
size_t SGS_File_fill(SGS_File *o) {
	FILE *f = o->ref;
	size_t len = 0;
	/*
	 * Set read pos to the first character of the buffer area.
	 * If it has ended up outside of the buffer (fill after last
	 * character in buffer), bring it back to the first buffer
	 * area.
	 *
	 * Read a buffer area's worth of data from the file, if
	 * open. Upon short read, insert SGS_File_STATUS() value
	 * not counted in return length. Close file upon end or error.
	 */
	o->read_pos &= (SGS_FILE_BSIZ - 1) & ~(SGS_FILE_ALEN - 1);
	if (!f) {
		o->fill_pos = o->read_pos;
		goto ADD_MARKER;
	}
	len = fread(&o->buf[o->read_pos], 1, SGS_FILE_ALEN, f);
	o->fill_pos = o->read_pos + len; /* pre-mask pos */
	if (ferror(f)) {
		o->status |= SGS_File_ERROR;
	}
	if (feof(f)) {
		o->status |= SGS_File_END;
		file_ref_close(o);
	}
	if (len < SGS_FILE_ALEN) {
		goto ADD_MARKER;
	}
	return len;

ADD_MARKER:
	o->end_marker = &o->buf[o->read_pos + len];
	*o->end_marker = o->status;
	++o->fill_pos;
	return len;
}

/*
 * Close file without clearing state.
 */
void file_ref_close(SGS_File *o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}
