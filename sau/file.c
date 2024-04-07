/* SAU library: Text file buffer module.
 * Copyright (c) 2014, 2017-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sau/file.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define GETD_ALLOW_TAIL_DOT 0

/**
 * Default callback. Moves through the circular buffer in
 * one of two ways, depending on whether or not file status
 * has SAU_FILE_END set.
 *
 * If clear, increases call position to the beginning
 * of the next buffer area, wrapping it to within the
 * buffer boundary.
 *
 * If set, instead calls sauFile_end(), writing out the
 * end marker to the current character in the buffer and
 * increasing the wrapped position by one.
 *
 * \return call position difference in clear case, or 0
 */
size_t sauFile_action_wrap(sauFile *restrict o) {
	if (o->status & SAU_FILE_END) {
		sauFile_end(o, 0, false); // repeat end marker
		return 0;
	}
	size_t skip_len = o->call_pos - (o->call_pos & ~(SAU_FILE_ALEN - 1));
	size_t len = SAU_FILE_ALEN - skip_len;
	o->call_pos = (o->call_pos + len) & (SAU_FILE_BUFSIZ - 1);
	return len;
}

/**
 * Reset all state other than buffer contents.
 * Used for opening and closing.
 */
void sauFile_init(sauFile *restrict o,
		sauFileAction_f call_f, void *restrict ref,
		const char *path, sauFileClose_f close_f) {
	if (o->close_f != NULL) o->close_f(o);

	o->pos = 0;
	o->call_pos = 0;
	o->call_f = call_f;
	o->status = SAU_FILE_OK;
	o->end_pos = (size_t) -1;
	o->ref = ref;
	o->path = path;
	o->close_f = close_f;
}

/**
 * Create instance. Sets the default callback.
 */
sauFile *sau_create_File(void) {
	sauFile *o = calloc(1, sizeof(sauFile));
	if (!o)
		return NULL;
	sauFile_init(o, sauFile_action_wrap, NULL, NULL, NULL);
	return o;
}

/**
 * Create instance with parent. Sets the default callback.
 */
sauFile *sau_create_sub_File(sauFile *restrict parent) {
	if (!parent)
		return NULL;
	sauFile *o = sau_create_File();
	if (!o)
		return NULL;
	o->parent = parent;
	return o;
}

/**
 * Destroy instance. Closes file if open.
 *
 * \return parent instance or NULL
 */
sauFile *sau_destroy_File(sauFile *restrict o) {
	if (!o)
		return NULL;

	if (o->close_f != NULL) o->close_f(o);
	sauFile *parent = o->parent;
	free(o);
	return parent;
}

static size_t mode_fread(sauFile *restrict o);
static size_t mode_strread(sauFile *restrict o);

static void ref_fclose(sauFile *restrict o);

/**
 * Open stdio file for reading.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with a new open call or a call
 * to sauFile_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool sauFile_fopenrb(sauFile *restrict o, const char *restrict path) {
	if (!path)
		return false;
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	sauFile_init(o, mode_fread, f, path, ref_fclose);
	return true;
}

/**
 * Open string as file for reading. The string must be NULL-terminated.
 * The path is optional and only used to name the file.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed upon a NULL byte,
 * but \a path is only cleared with a new open call or a call
 * to sauFile_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool sauFile_stropenrb(sauFile *restrict o,
		const char *restrict path, const char *restrict str) {
	if (!str)
		return false;
	sauFile_init(o, mode_strread, (void*) str, path, NULL);
	return true;
}

/**
 * Close and clear internal reference if open. Sets SAU_FILE_END status
 * and restores the callback to sauFile_action_wrap(). If there is a
 * parent file instance, will set SAU_FILE_CHANGE status.
 *
 * Leaves buffer contents and remaining parts of state untouched.
 *
 * Automatically used by sauFile_end().
 * Re-opening also automatically closes.
 */
void sauFile_close(sauFile *restrict o) {
	if (o->status & SAU_FILE_END)
		return;
	o->status |= SAU_FILE_END;
	if (o->parent != NULL)
		o->status |= SAU_FILE_CHANGE;
	if (o->close_f != NULL) {
		o->close_f(o);
		o->close_f = NULL;
	}
	o->ref = NULL;
	o->call_pos = (o->pos + 1) & (SAU_FILE_BUFSIZ - 1);
	o->call_f = sauFile_action_wrap;
}

/**
 * Reset state. Closes if open, clears file status (to SAU_FILE_OK),
 * and zeroes the buffer.
 */
void sauFile_reset(sauFile *restrict o) {
	sauFile_init(o, sauFile_action_wrap, NULL, NULL, NULL);
	memset(o->buf, 0, SAU_FILE_BUFSIZ);
}

/**
 * Mark currently opened file as ended. Used automatically on and
 * after EOF, but can also be called manually to act as if EOF
 * follows the current buffer contents.
 *
 * The first time this is called for an open file, will call
 * sauFile_close(), which will close the internal reference
 * and reset the callback. If \p error is true, will
 * additionally set SAU_FILE_ERROR status beforehand.
 *
 * On each call, an end marker will be written \p keep_len bytes
 * after the current position in the buffer. The callback call
 * position is set to the position after the marker.
 */
void sauFile_end(sauFile *restrict o, size_t keep_len, bool error) {
	sauFile_close(o);
	if (error)
		o->status |= SAU_FILE_ERROR;
	o->end_pos = (o->pos + keep_len) & (SAU_FILE_BUFSIZ - 1);
	o->buf[o->end_pos] = o->status;
	o->call_pos = (o->end_pos + 1) & (SAU_FILE_BUFSIZ - 1);
}

/*
 * Read up to a buffer area of data from a stdio file.
 * Closes file upon EOF or read error.
 *
 * Upon short read, inserts sauFile_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_fread(sauFile *restrict o) {
	FILE *f = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (SAU_FILE_BUFSIZ - 1) & ~(SAU_FILE_ALEN - 1);
	len = fread(&o->buf[o->pos], 1, SAU_FILE_ALEN, f);
	o->call_pos = (o->pos + len) & (SAU_FILE_BUFSIZ - 1);
	if (len < SAU_FILE_ALEN) sauFile_end(o, len, ferror(f) != 0);
	return len;
}

/*
 * Read up to a buffer area of data from a string, advancing
 * the pointer, unless the string is NULL. Closes file
 * (setting the string to NULL) upon NULL byte.
 *
 * Upon short read, inserts sauFile_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_strread(sauFile *restrict o) {
	const char *str = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (SAU_FILE_BUFSIZ - 1) & ~(SAU_FILE_ALEN - 1);
	len = strlen(str);
	if (len >= SAU_FILE_ALEN) {
		len = SAU_FILE_ALEN;
		o->ref = &((char*)o->ref)[len];
		o->call_pos = (o->pos + len) & (SAU_FILE_BUFSIZ - 1);
	} else {
		sauFile_end(o, len, false);
	}
	memcpy(&o->buf[o->pos], str, len);
	return len;
}

/*
 * Close stdio file without clearing state.
 */
static void ref_fclose(sauFile *restrict o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}

/**
 * Read characters into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be set to the string length.
 * If \p filter_f is not NULL, it will be used to filter characters
 * and end the string when 0 is returned; otherwise, characters
 * will be read until the buffer is filled or the file ends.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
bool sauFile_getstr(sauFile *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, sauFileFilter_f filter_f) {
	uint8_t *dst = buf;
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	if (filter_f != NULL) for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = filter_f(o, sauFile_GETC(o));
		if (c == '\0') {
			sauFile_DECP(o);
			break;
		}
		dst[i++] = c;
	} else for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = sauFile_GETC(o);
		if (c <= SAU_FILE_MARKER && sauFile_AFTER_EOF(o)) {
			sauFile_DECP(o);
			break;
		}
		dst[i++] = c;
	}
	dst[i] = '\0';
	if (lenp) *lenp = i;
	return !truncate;
}

/**
 * Read integer into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits.
 *
 * If \p lenp is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool sauFile_geti(sauFile *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	int32_t num = 0;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = sauFile_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = sauFile_GETC(o);
		++len;
	}
	if (!SAU_IS_DIGIT(c)) {
		sauFile_UNGETN(o, len);
		if (lenp) *lenp = 0;
		return true;
	}
	if (minus) {
		do {
			int32_t new_num = num * 10 - (c - '0');
			if (new_num > num) truncate = true;
			else num = new_num;
			c = sauFile_GETC(o);
			++len;
		} while (SAU_IS_DIGIT(c));
		if (truncate) num = INT32_MIN;
	} else {
		do {
			int32_t new_num = num * 10 + (c - '0');
			if (new_num < num) truncate = true;
			else num = new_num;
			c = sauFile_GETC(o);
			++len;
		} while (SAU_IS_DIGIT(c));
		if (truncate) num = INT32_MAX;
	}
	*var = num;
	sauFile_DECP(o);
	--len;
	if (lenp) *lenp = len;
	return !truncate;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits and/or point followed by digits.
 *
 * If \p lenp is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool sauFile_getd(sauFile *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	double num_a = 0.f, pos_div = 1.f;
	int64_t num_b = 0;
	double res;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = sauFile_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = sauFile_GETC(o);
		++len;
	}
	if (c != '.') {
		if (!SAU_IS_DIGIT(c)) goto NO_NUM;
		do {
			num_a = num_a * 10.f + (c - '0');
			c = sauFile_GETC(o);
			++len;
		} while (SAU_IS_DIGIT(c));
		if (c != '.') goto DONE;
		c = sauFile_GETC(o);
#if GETD_ALLOW_TAIL_DOT
		++len;
		if (!SAU_IS_DIGIT(c)) goto DONE;
#else
		if (!SAU_IS_DIGIT(c)) {
			sauFile_UNGETN(o, 2);
			sauFile_INCP(o);
			goto DONE;
		}
#endif
	} else {
		c = sauFile_GETC(o);
		++len;
		if (!SAU_IS_DIGIT(c)) goto NO_NUM;
	}
	while (SAU_IS_DIGIT(c)) {
		int64_t b = num_b * 10 + (c - '0');
		if (num_b <= b) {
			num_b = b;
			pos_div *= 10.f; // may become inf
		}
		c = sauFile_GETC(o);
		++len;
	}
	num_a += num_b / pos_div; // importantly, num_b is never inf
DONE:
	res = (double) num_a;
	if (isinf(res)) truncate = true;
	if (minus) res = -res;
	*var = res;
	sauFile_DECP(o);
	--len;
	if (lenp) *lenp = len;
	return !truncate;
NO_NUM:
	sauFile_UNGETN(o, len);
	if (lenp) *lenp = 0;
	return true;
}

/**
 * Advance past characters until \p filter_f returns zero.
 *
 * \return number of characters skipped
 */
size_t sauFile_skipstr(sauFile *restrict o, sauFileFilter_f filter_f) {
	size_t i = 0;
	for (;;) {
		uint8_t c = filter_f(o, sauFile_GETC(o));
		if (c == '\0') break;
		++i;
	}
	sauFile_DECP(o);
	return i;
}

/**
 * Advance past characters until the next is neither a space nor a tab.
 *
 * \return number of characters skipped
 */
size_t sauFile_skipspace(sauFile *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = sauFile_GETC(o);
		if (!SAU_IS_SPACE(c)) break;
		++i;
	}
	sauFile_DECP(o);
	return i;
}

/**
 * Advance past characters until the next marks the end of the line (or file).
 *
 * \return number of characters skipped
 */
size_t sauFile_skipline(sauFile *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = sauFile_GETC(o);
		if (SAU_IS_LNBRK(c) ||
			(c <= SAU_FILE_MARKER && sauFile_AFTER_EOF(o))) break;
		++i;
	}
	sauFile_DECP(o);
	return i;
}
