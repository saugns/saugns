/* ssndgen: Text file reader module.
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
#include <string.h>
#include <stdio.h>

static size_t file_mode_fread(SSG_CBufMode *o); // read callback

static void file_ref_close(SSG_File *o);

/**
 * Initialize instance. Must only be called once before a
 * finalization.
 */
bool SSG_init_File(SSG_File *o) {
	if (!SSG_init_CBuf(&o->cb)) {
		return false;
	}
	o->ref = NULL;
	o->path = NULL;
	o->close_f = NULL;
	return true;
}

/**
 * Finalize instance. Must only be called once after each
 * initialization.
 */
void SSG_fini_File(SSG_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SSG_fini_CBuf(&o->cb);
}

/**
 * Open file for reading.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with an explicit call to SSG_File_close()
 * or SSG_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool SSG_File_fopenrb(SSG_File *o, const char *path) {
	SSG_File_close(o);

	if (!path) return false;
	FILE *f = fopen(path, "rb");
	if (!f) return false;

	o->cb.r.call_pos = 0;
	o->cb.r.f = file_mode_fread;
	o->status = SSG_File_OK;
	o->end_marker = NULL;
	o->ref = f;
	o->path = path;
	o->close_f = file_ref_close;
	return true;
}

/**
 * Close File if open. Reset buffer read and write modes, but not
 * buffer contents.
 */
void SSG_File_close(SSG_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SSG_CBufMode_reset(&o->cb.r);
	SSG_CBufMode_reset(&o->cb.w);
	o->status = SSG_File_OK;
}

/**
 * Reset File object, including the buffer, its contents and
 * read and write modes. If open, will be closed.
 */
void SSG_File_reset(SSG_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SSG_CBuf_reset(&o->cb);
	o->status = SSG_File_OK;
	o->path = NULL;
}

/*
 * Fill the area of the buffer currently arrived at. This should be
 * called when indicated by SSG_File_NEED_FILL().
 *
 * When EOF or a read error occurs, the file will be closed and
 * the first character after the last one successfully read will
 * be assigned an end marker value. Further calls will reset the
 * reading position and write the end marker again.
 *
 * SSG_File_STATUS() will return the same value as the end marker,
 * which is always <= SSG_File_MARKER.
 *
 * \return number of characters successfully read
 */
static size_t file_mode_fread(SSG_CBufMode *o) {
	SSG_File *fo = o->ref;
	FILE *f = fo->ref;
	size_t len = 0;
	/*
	 * Set read pos to the first character of the buffer area.
	 * If it has ended up outside of the buffer (fill after last
	 * character in buffer), bring it back to the first buffer
	 * area.
	 *
	 * Read a buffer area's worth of data from the file, if
	 * open. Upon short read, insert SSG_File_STATUS() value
	 * not counted in return length. Close file upon end or error.
	 */
	o->pos &= (SSG_CBUF_SIZ - 1) & ~(SSG_CBUF_ALEN - 1);
	if (!f) {
		o->call_pos = o->pos;
		goto ADD_MARKER;
	}
	len = fread(&fo->cb.buf[o->pos], 1, SSG_CBUF_ALEN, f);
	o->call_pos = o->pos + len; /* pre-mask pos */
	if (ferror(f)) {
		fo->status |= SSG_File_ERROR;
	}
	if (feof(f)) {
		fo->status |= SSG_File_END;
		file_ref_close(fo);
	}
	if (len < SSG_CBUF_ALEN) {
		goto ADD_MARKER;
	}
	return len;

ADD_MARKER:
	fo->end_marker = &fo->cb.buf[o->pos + len];
	*fo->end_marker = fo->status;
	++o->call_pos;
	return len;
}

/*
 * Close file without clearing state.
 */
void file_ref_close(SSG_File *o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}
