/* sgensys: Text file reader module.
 * Copyright (c) 2014, 2017-2019 Joel K. Pettersson
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
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Increase call position by buffer area length,
 * wrapping it to within the buffer boundary.
 *
 * Default callback for reading and writing modes.
 *
 * \return length of buffer area
 */
size_t SGS_File_wrap(SGS_File *o SGS__maybe_unused, SGS_FBufMode *m) {
	m->call_pos = (m->call_pos + SGS_FBUF_ALEN) & (SGS_FBUF_SIZ - 1);
	return SGS_FBUF_ALEN;
}

/**
 * Reset mode struct instance to default values, including
 * the default callback.
 */
void SGS_FBufMode_reset(SGS_FBufMode *restrict m) {
	m->pos = 0;
	m->call_pos = SGS_FBUF_ALEN;
	m->f = SGS_File_wrap;
}

/**
 * Create instance.
 *
 * Sets the default callback for reading and writing modes.
 */
SGS_File *SGS_create_File(void) {
	SGS_File *o = calloc(1, sizeof(SGS_File));
	if (!o) return NULL;
	SGS_FBufMode_reset(&o->mr);
	o->mw = o->mr;
	return o;
}

/**
 * Destroy instance. Closes file if open.
 */
void SGS_destroy_File(SGS_File *restrict o) {
	if (o->close_f) {
		o->close_f(o);
	}
	free(o);
}

static size_t file_mode_fread(SGS_File *restrict o, SGS_FBufMode *restrict m);

static void file_ref_close(SGS_File *restrict o);

/**
 * Open file for reading.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with an explicit call to SGS_File_close()
 * or SGS_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool SGS_File_fopenrb(SGS_File *restrict o, const char *restrict path) {
	SGS_File_close(o);

	if (!path) return false;
	FILE *f = fopen(path, "rb");
	if (!f) return false;

	o->mr.call_pos = 0;
	o->mr.f = file_mode_fread;
	o->status = SGS_FILE_OK;
	o->end_pos = (size_t) -1;
	o->ref = f;
	o->path = path;
	o->close_f = file_ref_close;
	return true;
}

/**
 * Close File if open. Reset buffer read and write modes, but not
 * buffer contents.
 */
void SGS_File_close(SGS_File *restrict o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SGS_FBufMode_reset(&o->mr);
	SGS_FBufMode_reset(&o->mw);
	o->status = SGS_FILE_OK;
	o->path = NULL;
}

/**
 * Reset File object. Like SGS_File_close(), except it also zeroes the buffer.
 */
void SGS_File_reset(SGS_File *restrict o) {
	SGS_File_close(o);
	memset(o->buf, 0, SGS_FBUF_SIZ);
}

static void add_end_marker(SGS_File *restrict o, SGS_FBufMode *restrict m,
		size_t len) {
	o->end_pos = m->pos + len;
	o->buf[o->end_pos] = o->status;
	++m->call_pos;
}

/*
 * Fill the area of the buffer currently arrived at. This should be
 * called when indicated by SGS_File_NEED_FILL().
 *
 * When EOF or a read error occurs, the file will be closed and
 * the first character after the last one successfully read will
 * be assigned an end marker value. Further calls will reset the
 * reading position and write the end marker again.
 *
 * SGS_File_STATUS() will return the same value as the end marker,
 * which is always <= SGS_FILE_MARKER.
 *
 * \return number of characters successfully read
 */
static size_t file_mode_fread(SGS_File *restrict o, SGS_FBufMode *restrict m) {
	FILE *f = o->ref;
	/*
	 * Set position to the first character of the buffer area.
	 *
	 * Read a buffer area's worth of data from the file, if
	 * open. Upon short read, insert SGS_File_STATUS() value
	 * not counted in return length. Close file upon end or error.
	 */
	m->pos &= (SGS_FBUF_SIZ - 1) & ~(SGS_FBUF_ALEN - 1);
	if (!f) {
		m->call_pos = m->pos;
		add_end_marker(o, m, 0);
		return 0;
	}
	size_t len = fread(&o->buf[m->pos], 1, SGS_FBUF_ALEN, f);
	m->call_pos = (m->pos + len) & (SGS_FBUF_SIZ - 1);
	if (ferror(f)) {
		o->status |= SGS_FILE_ERROR;
	}
	if (feof(f)) {
		o->status |= SGS_FILE_END;
		file_ref_close(o);
	}
	if (len < SGS_FBUF_ALEN) {
		add_end_marker(o, m, len);
	}
	return len;
}

/*
 * Close file without clearing state.
 */
static void file_ref_close(SGS_File *restrict o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}

#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

/**
 * Read characters into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always zero-terminated.
 *
 * If \p str_len is not NULL, it will be set to the string length.
 * If \p c_filter is not NULL, it will be used to filter characters
 * and end the string when 0 is returned.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
bool SGS_File_gets(SGS_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict str_len, SGS_File_CFilter_f c_filter) {
	uint8_t *dst = buf;
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	if (c_filter) for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = c_filter(o, SGS_File_GETC(o));
		if (c == '\0') {
			SGS_FBufMode_DECP(&o->mr);
			break;
		}
		dst[i++] = c;
	} else for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SGS_File_GETC(o);
		if (c <= SGS_FILE_MARKER && SGS_File_AFTER_EOF(o)) {
			SGS_FBufMode_DECP(&o->mr);
			break;
		}
		dst[i++] = c;
	}
	dst[i] = '\0';
	if (str_len) *str_len = i;
	return !truncate;
}

/**
 * Read integer into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SGS_File_geti(SGS_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	uint8_t c;
	int32_t num = 0;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = SGS_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SGS_File_GETC(o);
		++len;
	}
	if (!IS_DIGIT(c)) {
		SGS_File_UNGETN(o, len);
		if (str_len) *str_len = 0;
		return true;
	}
	if (minus) {
		do {
			int32_t new_num = num * 10 - (c - '0');
			if (new_num > num) truncate = true;
			else num = new_num;
			c = SGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MIN;
	} else {
		do {
			int32_t new_num = num * 10 + (c - '0');
			if (new_num < num) truncate = true;
			else num = new_num;
			c = SGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MAX;
	}
	*var = num;
	SGS_FBufMode_DECP(&o->mr);
	--len;
	if (str_len) *str_len = len;
	return !truncate;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits and/or point followed by digits.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SGS_File_getd(SGS_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	uint8_t c;
	long double num = 0.f, pos_mul = 1.f;
	double res;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = SGS_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SGS_File_GETC(o);
		++len;
	}
	if (c != '.') {
		if (!IS_DIGIT(c)) {
			SGS_File_UNGETN(o, len);
			if (str_len) *str_len = 0;
			return true;
		}
		do {
			num = num * 10.f + (c - '0');
			c = SGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (c != '.') goto DONE;
		c = SGS_File_GETC(o);
		++len;
	} else {
		c = SGS_File_GETC(o);
		++len;
		if (!IS_DIGIT(c)) {
			SGS_File_UNGETN(o, len);
			if (str_len) *str_len = 0;
			return true;
		}
	}
	while (IS_DIGIT(c)) {
		pos_mul *= 0.1f;
		num += (c - '0') * pos_mul;
		c = SGS_File_GETC(o);
		++len;
	}

DONE:
	res = (double) num;
	if (res == INFINITY) truncate = true;
	if (minus) res = -res;
	*var = res;
	SGS_FBufMode_DECP(&o->mr);
	--len;
	if (str_len) *str_len = len;
	return !truncate;
}

/**
 * Advance past characters until \p c_filter returns zero.
 *
 * \return number of characters skipped
 */
size_t SGS_File_skips(SGS_File *restrict o, SGS_File_CFilter_f c_filter) {
	size_t i = 0;
	for (;;) {
		uint8_t c = c_filter(o, SGS_File_GETC(o));
		if (c == '\0') break;
		++i;
	}
	SGS_FBufMode_DECP(&o->mr);
	return i;
}

/**
 * Advance past characters until the next is neither a space nor a tab.
 *
 * \return number of characters skipped
 */
size_t SGS_File_skipspace(SGS_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = SGS_File_GETC(o);
		if (!IS_SPACE(c)) break;
		++i;
	}
	SGS_FBufMode_DECP(&o->mr);
	return i;
}

/**
 * Advance past characters until the next marks the end of the line (or file).
 *
 * \return number of characters skipped
 */
size_t SGS_File_skipline(SGS_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = SGS_File_GETC(o);
		if (IS_LNBRK(c) ||
			(c <= SGS_FILE_MARKER && SGS_File_AFTER_EOF(o))) break;
		++i;
	}
	SGS_FBufMode_DECP(&o->mr);
	return i;
}
