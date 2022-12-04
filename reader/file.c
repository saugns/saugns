/* mgensys: Text file buffer module.
 * Copyright (c) 2014, 2017-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

#include "file.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Default callback. Moves through the circular buffer in
 * one of two ways, depending on whether or not file status
 * has MGS_FILE_END set.
 *
 * If clear, increases call position to the beginning
 * of the next buffer area, wrapping it to within the
 * buffer boundary.
 *
 * If set, instead calls MGS_File_end(), writing out the
 * end marker to the current character in the buffer and
 * increasing the wrapped position by one.
 *
 * \return call position difference in clear case, or 0
 */
size_t MGS_File_action_wrap(MGS_File *restrict o) {
	if (o->status & MGS_FILE_END) {
		MGS_File_end(o, 0, false); // repeat end marker
		return 0;
	}
	size_t skip_len = o->call_pos - (o->call_pos & ~(MGS_FILE_ALEN - 1));
	size_t len = MGS_FILE_ALEN - skip_len;
	o->call_pos = (o->call_pos + len) & (MGS_FILE_BUFSIZ - 1);
	return len;
}

/**
 * Reset all state other than buffer contents.
 * Used for opening and closing.
 */
void MGS_File_init(MGS_File *restrict o,
		MGS_FileAction_f call_f, void *restrict ref,
		const char *path, MGS_FileClose_f close_f) {
	if (o->close_f != NULL) o->close_f(o);

	o->pos = 0;
	o->call_pos = 0;
	o->call_f = call_f;
	o->status = MGS_FILE_OK;
	o->end_pos = (size_t) -1;
	o->ref = ref;
	o->path = path;
	o->close_f = close_f;
}

/**
 * Create instance. Sets the default callback.
 */
MGS_File *MGS_create_File(void) {
	MGS_File *o = calloc(1, sizeof(MGS_File));
	if (!o)
		return NULL;
	MGS_File_init(o, MGS_File_action_wrap, NULL, NULL, NULL);
	return o;
}

/**
 * Create instance with parent. Sets the default callback.
 */
MGS_File *MGS_create_sub_File(MGS_File *restrict parent) {
	if (!parent)
		return NULL;
	MGS_File *o = MGS_create_File();
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
MGS_File *MGS_destroy_File(MGS_File *restrict o) {
	if (!o)
		return NULL;

	if (o->close_f != NULL) o->close_f(o);
	MGS_File *parent = o->parent;
	free(o);
	return parent;
}

static size_t mode_fread(MGS_File *restrict o);
static size_t mode_strread(MGS_File *restrict o);

static void ref_fclose(MGS_File *restrict o);

/**
 * Open stdio file for reading.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with a new open call or a call
 * to MGS_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool MGS_File_fopenrb(MGS_File *restrict o, const char *restrict path) {
	if (!path)
		return false;
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	MGS_File_init(o, mode_fread, f, path, ref_fclose);
	return true;
}

/**
 * Open string as file for reading. The string must be NULL-terminated.
 * The path is optional and only used to name the file.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed upon a NULL byte,
 * but \a path is only cleared with a new open call or a call
 * to MGS_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool MGS_File_stropenrb(MGS_File *restrict o,
		const char *restrict path, const char *restrict str) {
	if (!str)
		return false;
	MGS_File_init(o, mode_strread, (void*) str, path, NULL);
	return true;
}

/**
 * Close and clear internal reference if open. Sets MGS_FILE_END status
 * and restores the callback to MGS_File_action_wrap(). If there is a
 * parent file instance, will set MGS_FILE_CHANGE status.
 *
 * Leaves buffer contents and remaining parts of state untouched.
 *
 * Automatically used by MGS_File_end().
 * Re-opening also automatically closes.
 */
void MGS_File_close(MGS_File *restrict o) {
	if (o->status & MGS_FILE_END)
		return;
	o->status |= MGS_FILE_END;
	if (o->parent != NULL)
		o->status |= MGS_FILE_CHANGE;
	if (o->close_f != NULL) {
		o->close_f(o);
		o->close_f = NULL;
	}
	o->ref = NULL;
	o->call_pos = (o->pos + 1) & (MGS_FILE_BUFSIZ - 1);
	o->call_f = MGS_File_action_wrap;
}

/**
 * Reset state. Closes if open, clears file status (to MGS_FILE_OK),
 * and zeroes the buffer.
 */
void MGS_File_reset(MGS_File *restrict o) {
	MGS_File_init(o, MGS_File_action_wrap, NULL, NULL, NULL);
	memset(o->buf, 0, MGS_FILE_BUFSIZ);
}

/**
 * Mark currently opened file as ended. Used automatically on and
 * after EOF, but can also be called manually to act as if EOF
 * follows the current buffer contents.
 *
 * The first time this is called for an open file, will call
 * MGS_File_close(), which will close the internal reference
 * and reset the callback. If \p error is true, will
 * additionally set MGS_FILE_ERROR status beforehand.
 *
 * On each call, an end marker will be written \p keep_len bytes
 * after the current position in the buffer. The callback call
 * position is set to the position after the marker.
 */
void MGS_File_end(MGS_File *restrict o, size_t keep_len, bool error) {
	MGS_File_close(o);
	if (error)
		o->status |= MGS_FILE_ERROR;
	o->end_pos = (o->pos + keep_len) & (MGS_FILE_BUFSIZ - 1);
	o->buf[o->end_pos] = o->status;
	o->call_pos = (o->end_pos + 1) & (MGS_FILE_BUFSIZ - 1);
}

/*
 * Read up to a buffer area of data from a stdio file.
 * Closes file upon EOF or read error.
 *
 * Upon short read, inserts MGS_File_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_fread(MGS_File *restrict o) {
	FILE *f = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (MGS_FILE_BUFSIZ - 1) & ~(MGS_FILE_ALEN - 1);
	len = fread(&o->buf[o->pos], 1, MGS_FILE_ALEN, f);
	o->call_pos = (o->pos + len) & (MGS_FILE_BUFSIZ - 1);
	if (len < MGS_FILE_ALEN) MGS_File_end(o, len, ferror(f) != 0);
	return len;
}

/*
 * Read up to a buffer area of data from a string, advancing
 * the pointer, unless the string is NULL. Closes file
 * (setting the string to NULL) upon NULL byte.
 *
 * Upon short read, inserts MGS_File_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_strread(MGS_File *restrict o) {
	const char *str = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (MGS_FILE_BUFSIZ - 1) & ~(MGS_FILE_ALEN - 1);
	len = strlen(str);
	if (len >= MGS_FILE_ALEN) {
		len = MGS_FILE_ALEN;
		o->ref = &((char*)o->ref)[len];
		o->call_pos = (o->pos + len) & (MGS_FILE_BUFSIZ - 1);
	} else {
		MGS_File_end(o, len, false);
	}
	memcpy(&o->buf[o->pos], str, len);
	return len;
}

/*
 * Close stdio file without clearing state.
 */
static void ref_fclose(MGS_File *restrict o) {
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
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be set to the string length.
 * If \p filter_f is not NULL, it will be used to filter characters
 * and end the string when 0 is returned; otherwise, characters
 * will be read until the buffer is filled or the file ends.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
bool MGS_File_getstr(MGS_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, MGS_FileFilter_f filter_f) {
	uint8_t *dst = buf;
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	if (filter_f != NULL) for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = filter_f(o, MGS_File_GETC(o));
		if (c == '\0') {
			MGS_File_DECP(o);
			break;
		}
		dst[i++] = c;
	} else for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = MGS_File_GETC(o);
		if (c <= MGS_FILE_MARKER && MGS_File_AFTER_EOF(o)) {
			MGS_File_DECP(o);
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
bool MGS_File_geti(MGS_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	int32_t num = 0;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = MGS_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = MGS_File_GETC(o);
		++len;
	}
	if (!IS_DIGIT(c)) {
		MGS_File_UNGETN(o, len);
		if (lenp) *lenp = 0;
		return true;
	}
	if (minus) {
		do {
			int32_t new_num = num * 10 - (c - '0');
			if (new_num > num) truncate = true;
			else num = new_num;
			c = MGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MIN;
	} else {
		do {
			int32_t new_num = num * 10 + (c - '0');
			if (new_num < num) truncate = true;
			else num = new_num;
			c = MGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MAX;
	}
	*var = num;
	MGS_File_DECP(o);
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
bool MGS_File_getd(MGS_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	long double num = 0.f, pos_mul = 1.f;
	double res;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = MGS_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = MGS_File_GETC(o);
		++len;
	}
	if (c != '.') {
		if (!IS_DIGIT(c)) {
			MGS_File_UNGETN(o, len);
			if (lenp) *lenp = 0;
			return true;
		}
		do {
			num = num * 10.f + (c - '0');
			c = MGS_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (c != '.') goto DONE;
		c = MGS_File_GETC(o);
		++len;
	} else {
		c = MGS_File_GETC(o);
		++len;
		if (!IS_DIGIT(c)) {
			MGS_File_UNGETN(o, len);
			if (lenp) *lenp = 0;
			return true;
		}
	}
	while (IS_DIGIT(c)) {
		pos_mul *= 0.1f;
		num += (c - '0') * pos_mul;
		c = MGS_File_GETC(o);
		++len;
	}
DONE:
	res = (double) num;
	if (isinf(res)) truncate = true;
	if (minus) res = -res;
	*var = res;
	MGS_File_DECP(o);
	--len;
	if (lenp) *lenp = len;
	return !truncate;
}

/**
 * Advance past characters until \p filter_f returns zero.
 *
 * \return number of characters skipped
 */
size_t MGS_File_skipstr(MGS_File *restrict o, MGS_FileFilter_f filter_f) {
	size_t i = 0;
	for (;;) {
		uint8_t c = filter_f(o, MGS_File_GETC(o));
		if (c == '\0') break;
		++i;
	}
	MGS_File_DECP(o);
	return i;
}

/**
 * Advance past characters until the next is neither a space nor a tab.
 *
 * \return number of characters skipped
 */
size_t MGS_File_skipspace(MGS_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = MGS_File_GETC(o);
		if (!IS_SPACE(c)) break;
		++i;
	}
	MGS_File_DECP(o);
	return i;
}

/**
 * Advance past characters until the next marks the end of the line (or file).
 *
 * \return number of characters skipped
 */
size_t MGS_File_skipline(MGS_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = MGS_File_GETC(o);
		if (IS_LNBRK(c) ||
			(c <= MGS_FILE_MARKER && MGS_File_AFTER_EOF(o))) break;
		++i;
	}
	MGS_File_DECP(o);
	return i;
}
