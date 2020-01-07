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

#pragma once
#include "../common.h"

#define MGS_FILE_ALEN   4096
#define MGS_FILE_ANUM   2
#define MGS_FILE_BUFSIZ (MGS_FILE_ALEN * MGS_FILE_ANUM)

struct MGS_File;
typedef struct MGS_File MGS_File;

/**
 * Action callback type. Must update call position, may change position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of bytes successfully handled.
 */
typedef size_t (*MGS_FileAction_f)(MGS_File *restrict o);

size_t MGS_File_action_wrap(MGS_File *restrict o); // default & EOF'd callback

/**
 * Flip to the beginning of the next buffer area.
 *
 * \return new position
 */
#define MGS_File_ANEXT(o) \
	((o)->pos = ((o)->pos + MGS_FILE_ALEN) \
		& ((MGS_FILE_BUFSIZ - 1) & ~(MGS_FILE_ALEN - 1)))

/**
 * Flip to the next buffer area,
 * maintaining relative position within the area.
 *
 * \return new position
 */
#define MGS_File_AINC(o) \
	((o)->pos = ((o)->pos + MGS_FILE_ALEN) \
		& (MGS_FILE_BUFSIZ - 1))

/**
 * Get position relative to buffer area.
 *
 * \return position
 */
#define MGS_File_APOS(o) \
	((o)->pos & (MGS_FILE_ALEN - 1))

/**
 * Get remaining length (characters after position)
 * within current buffer area.
 *
 * \return length
 */
#define MGS_File_AREM(o) \
	((MGS_FILE_ALEN - 1) - ((o)->pos & (MGS_FILE_ALEN - 1)))

/**
 * Get remaining length (characters after position)
 * within buffer, i.e. before position must wrap.
 *
 * \return length
 */
#define MGS_File_BREM(o) \
	((MGS_FILE_BUFSIZ - 1) - ((o)->pos & (MGS_FILE_BUFSIZ - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define MGS_File_NEED_CALL(o) \
	((o)->pos == (o)->call_pos)

/**
 * Get remaining length (characters after position) before
 * callback position.
 *
 * If position has been decreased, as in e.g. character ungetting,
 * a length longer than that between calls may be returned.
 *
 * \return length
 */
#define MGS_File_CREM(o) \
	(((o)->call_pos - (o)->pos) & (MGS_FILE_BUFSIZ - 1))

/**
 * Increment position without limiting it to the buffer boundary.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which did not advance position.
 *
 * \return new position
 */
#define MGS_File_INCP(o) (++(o)->pos)

/**
 * Decrement position without limiting it to the buffer boundary.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which advanced the position.
 *
 * \return new position
 */
#define MGS_File_DECP(o) (--(o)->pos)

/**
 * Wrap position to within the buffer boundary.
 *
 * \return new position
 */
#define MGS_File_FIXP(o) ((o)->pos &= MGS_FILE_BUFSIZ - 1)

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most MGS_FILE_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	MGS_FILE_OK = 0,
	MGS_FILE_END = 1<<0,
	MGS_FILE_ERROR = 1<<1,
	MGS_FILE_CHANGE = 1<<2,
	MGS_FILE_MARKER = 0x07,
};

/**
 * Callback type for closing internal reference.
 * Should close file and set \a ref to NULL, otherwise
 * leaving state unchanged.
 */
typedef void (*MGS_FileClose_f)(MGS_File *restrict o);

/**
 * File type using circular buffer, meant for scanning and parsing.
 * Supports creating sub-instances, e.g. used for nested file includes.
 *
 * Maintains state for moving through the buffer and calling a function
 * to perform action at chosen positions in the buffer.
 *
 * The default callback simply increases and wraps the call position.
 * Opening a file for reading sets a callback to fill the buffer
 * one area at a time.
 */
struct MGS_File {
	size_t pos;
	size_t call_pos;
	MGS_FileAction_f call_f;
	uint8_t status;
	size_t end_pos;
	void *ref;
	const char *path;
	MGS_File *parent;
	MGS_FileClose_f close_f;
	uint8_t buf[MGS_FILE_BUFSIZ];
};

MGS_File *MGS_create_File(void) mgsMalloclike;
MGS_File *MGS_create_sub_File(MGS_File *restrict parent) mgsMalloclike;
MGS_File *MGS_destroy_File(MGS_File *restrict o);

void MGS_File_init(MGS_File *restrict o,
		MGS_FileAction_f call_f, void *restrict ref,
		const char *path, MGS_FileClose_f close_f);

bool MGS_File_fopenrb(MGS_File *restrict o, const char *restrict path);
bool MGS_File_stropenrb(MGS_File *restrict o,
		const char *restrict path, const char *restrict str);

void MGS_File_close(MGS_File *restrict o);
void MGS_File_reset(MGS_File *restrict o);

void MGS_File_end(MGS_File *restrict o, size_t keep_len, bool error);

/**
 * Check position and call callback if at the call position.
 *
 * Wraps position to within the buffer boundary.
 */
#define MGS_File_UPDATE(o) ((void) \
	((MGS_File_FIXP(o), MGS_File_NEED_CALL(o)) \
		&& (o)->call_f((o))))

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define MGS_File_RETC(o) \
	(MGS_File_UPDATE((o)), \
	 (o)->buf[(o)->pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define MGS_File_RETC_NC(o) \
	((o)->buf[(o)->pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to MGS_File_RETC() followed by MGS_File_INCP().
 *
 * \return current character
 */
#define MGS_File_GETC(o) \
	(MGS_File_UPDATE((o)), \
	 (o)->buf[(o)->pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define MGS_File_GETC_NC(o) \
	((o)->buf[(o)->pos++])

/**
 * Undo the getting of a character.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (MGS_FILE_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define MGS_File_UNGETC(o) \
	((o)->pos = ((o)->pos - 1) & (MGS_FILE_BUFSIZ - 1))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define MGS_File_TESTC(o, c) \
	(MGS_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to MGS_File_TESTC() followed by MGS_File_INCP()
 * when true.
 *
 * \return true if character got
 */
#define MGS_File_TRYC(o, c) \
	(MGS_File_TESTC((o), c) && (MGS_File_INCP((o)), true))

/**
 * Undo the getting of \p n number of characters.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (MGS_FILE_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define MGS_File_UNGETN(o, n) \
	((o)->pos = ((o)->pos - (n)) & (MGS_FILE_BUFSIZ - 1))

/**
 * Set current character, without advancing position.
 */
#define MGS_File_SETC(o, c) ((void) \
	(MGS_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define MGS_File_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define MGS_File_PUTC(o, c) ((void) \
	(MGS_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define MGS_File_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->pos++] = (c)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * MGS_File_AT_EOF()/MGS_File_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define MGS_File_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or MGS_File_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define MGS_File_AT_EOF(o) \
	((o)->end_pos == (o)->pos)

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or MGS_File_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define MGS_File_AFTER_EOF(o) \
	((o)->end_pos == (((o)->pos - 1) & (MGS_FILE_BUFSIZ - 1)))

/**
 * Get newline in portable way, advancing position if newline read.
 *
 * \return true if newline got
 */
static inline bool MGS_File_trynewline(MGS_File *restrict o) {
	uint8_t c = MGS_File_RETC(o);
	if (c == '\n') {
		MGS_File_INCP(o);
		MGS_File_TRYC(o, '\r');
		return true;
	}
	if (c == '\r') {
		MGS_File_INCP(o);
		return true;
	}
	return false;
}

/**
 * Callback type for filtering characters.
 * Should return the character to use, or 0 to indicate no match.
 */
typedef uint8_t (*MGS_FileFilter_f)(MGS_File *restrict o, uint8_t c);

bool MGS_File_getstr(MGS_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, MGS_FileFilter_f filter_f);
bool MGS_File_geti(MGS_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp);
bool MGS_File_getd(MGS_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp);
size_t MGS_File_skipstr(MGS_File *restrict o, MGS_FileFilter_f filter_f);
size_t MGS_File_skipspace(MGS_File *restrict o);
size_t MGS_File_skipline(MGS_File *restrict o);
