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

#include "file.h"
#include "../math.h"
#include <string.h>
#include <stdio.h>

static size_t file_mode_fread(SGS_CBufMode *o); // read callback

static void file_ref_close(SGS_File *o);

/**
 * Initialize instance. Must only be called once before a
 * finalization.
 */
bool SGS_init_File(SGS_File *o) {
	if (!SGS_init_CBuf(&o->cb)) {
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
void SGS_fini_File(SGS_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SGS_fini_CBuf(&o->cb);
}

/**
 * Open file for reading.
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with an explicit call to SGS_File_close()
 * or SGS_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool SGS_File_fopenrb(SGS_File *o, const char *path) {
	SGS_File_close(o);

	if (!path) return false;
	FILE *f = fopen(path, "rb");
	if (!f) return false;

	o->cb.r.call_pos = 0;
	o->cb.r.f = file_mode_fread;
	o->status = SGS_File_OK;
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
void SGS_File_close(SGS_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SGS_CBufMode_reset(&o->cb.r);
	SGS_CBufMode_reset(&o->cb.w);
	o->status = SGS_File_OK;
}

/**
 * Reset File object, including the buffer, its contents and
 * read and write modes. If open, will be closed.
 */
void SGS_File_reset(SGS_File *o) {
	if (o->close_f) {
		o->close_f(o);
		o->close_f = NULL;
	}
	SGS_CBuf_reset(&o->cb);
	o->status = SGS_File_OK;
	o->path = NULL;
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
 * which is always <= SGS_File_MARKER.
 *
 * \return number of characters successfully read
 */
static size_t file_mode_fread(SGS_CBufMode *o) {
	SGS_File *fo = o->ref;
	FILE *f = fo->ref;
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
	o->pos &= (SGS_CBUF_SIZ - 1) & ~(SGS_CBUF_ALEN - 1);
	if (!f) {
		o->call_pos = o->pos;
		goto ADD_MARKER;
	}
	len = fread(&fo->cb.buf[o->pos], 1, SGS_CBUF_ALEN, f);
	o->call_pos = o->pos + len; /* pre-mask pos */
	if (ferror(f)) {
		fo->status |= SGS_File_ERROR;
	}
	if (feof(f)) {
		fo->status |= SGS_File_END;
		file_ref_close(fo);
	}
	if (len < SGS_CBUF_ALEN) {
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
void file_ref_close(SGS_File *o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}

#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

/**
 * Advance past characters until the next is neither a space nor a tab.
 *
 * \return number of characters skipped
 */
uint32_t SGS_File_skipspace(SGS_File *o) {
	SGS_CBuf *cb = &o->cb;
	uint32_t i = 0;
	for (;;) {
		uint8_t c = SGS_CBuf_GETC(cb);
		if (!IS_SPACE(c)) break;
		++i;
	}
	SGS_CBufMode_DECP(&cb->r);
	return i;
}

/**
 * Advance past characters until the next marks the end of the line (or file).
 *
 * \return number of characters skipped
 */
uint32_t SGS_File_skipline(SGS_File *o) {
	SGS_CBuf *cb = &o->cb;
	uint32_t i = 0;
	for (;;) {
		uint8_t c = SGS_CBuf_GETC(cb);
		if (IS_LNBRK(c) ||
			(c <= SGS_File_MARKER && SGS_File_AFTER_EOF(o))) break;
		++i;
	}
	SGS_CBufMode_DECP(&cb->r);
	return i;
}

/**
 * Read characters into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always zero-terminated.
 *
 * If \p str_len is not NULL, it will be set to the string length.
 * If \p cfilter is not NULL, it will be used to filter characters
 * and end the string when 0 is returned.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
bool SGS_File_gets(SGS_File *o, void *buf, uint32_t buf_len,
		uint32_t *str_len, SGS_File_CFilter_f cfilter) {
	SGS_CBuf *cb = &o->cb;
	uint8_t *dst = buf;
	uint32_t i = 0;
	uint32_t max_len = buf_len - 1;
	bool truncate = false;
	if (cfilter) for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = cfilter(o, SGS_CBuf_GETC(cb));
		if (c == '\0') {
			SGS_CBufMode_DECP(&cb->r);
			break;
		}
		dst[i++] = c;
	} else for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SGS_CBuf_GETC(cb);
		if (c <= SGS_File_MARKER && SGS_File_AFTER_EOF(o)) {
			SGS_CBufMode_DECP(&cb->r);
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
bool SGS_File_geti(SGS_File *o, int32_t *var, bool allow_sign,
		uint32_t *str_len) {
	SGS_CBuf *cb = &o->cb;
	uint8_t c;
	int32_t num = 0;
	bool minus = false;
	bool truncate = false;
	uint32_t len = 0;
	c = SGS_CBuf_GETC(cb);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SGS_CBuf_GETC(cb);
		++len;
	}
	if (!IS_DIGIT(c)) {
		SGS_CBuf_UNGETN(cb, len);
		if (str_len) *str_len = 0;
		return true;
	}
	if (minus) {
		do {
			int32_t new_num = num * 10 - (c - '0');
			if (new_num > num) truncate = true;
			else num = new_num;
			c = SGS_CBuf_GETC(cb);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MIN;
	} else {
		do {
			int32_t new_num = num * 10 + (c - '0');
			if (new_num < num) truncate = true;
			else num = new_num;
			c = SGS_CBuf_GETC(cb);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MAX;
	}
	*var = num;
	SGS_CBufMode_DECP(&cb->r);
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
bool SGS_File_getd(SGS_File *o, double *var, bool allow_sign,
		uint32_t *str_len) {
	SGS_CBuf *cb = &o->cb;
	uint8_t c;
	long double num = 0.f, pos_mul = 1.f;
	double res;
	bool minus = false;
	bool truncate = false;
	uint32_t len = 0;
	c = SGS_CBuf_GETC(cb);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SGS_CBuf_GETC(cb);
		++len;
	}
	if (c != '.') {
		if (!IS_DIGIT(c)) {
			SGS_CBuf_UNGETN(cb, len);
			if (str_len) *str_len = 0;
			return true;
		}
		do {
			num = num * 10.f + (c - '0');
			c = SGS_CBuf_GETC(cb);
			++len;
		} while (IS_DIGIT(c));
		if (c != '.') goto DONE;
		c = SGS_CBuf_GETC(cb);
		++len;
	} else {
		c = SGS_CBuf_GETC(cb);
		++len;
		if (!IS_DIGIT(c)) {
			SGS_CBuf_UNGETN(cb, len);
			if (str_len) *str_len = 0;
			return true;
		}
	}
	while (IS_DIGIT(c)) {
		pos_mul *= 0.1f;
		num += (c - '0') * pos_mul;
		c = SGS_CBuf_GETC(cb);
		++len;
	}

DONE:
	res = (double) num;
	if (res == INFINITY) truncate = true;
	if (minus) res = -res;
	*var = res;
	SGS_CBufMode_DECP(&cb->r);
	--len;
	if (str_len) *str_len = len;
	return !truncate;
}
