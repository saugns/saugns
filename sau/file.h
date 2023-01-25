/* SAU library: Text file buffer module.
 * Copyright (c) 2014, 2017-2022 Joel K. Pettersson
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
#include "common.h"

#define SAU_FILE_ALEN   4096
#define SAU_FILE_ANUM   2
#define SAU_FILE_BUFSIZ (SAU_FILE_ALEN * SAU_FILE_ANUM)

struct sauFile;
typedef struct sauFile sauFile;

/**
 * Action callback type. Must update call position, may change position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of bytes successfully handled.
 */
typedef size_t (*sauFileAction_f)(sauFile *restrict o);

size_t sauFile_action_wrap(sauFile *restrict o); // default & EOF'd callback

/**
 * Flip to the beginning of the next buffer area.
 *
 * \return new position
 */
#define sauFile_ANEXT(o) \
	((o)->pos = ((o)->pos + SAU_FILE_ALEN) \
		& ((SAU_FILE_BUFSIZ - 1) & ~(SAU_FILE_ALEN - 1)))

/**
 * Flip to the next buffer area,
 * maintaining relative position within the area.
 *
 * \return new position
 */
#define sauFile_AINC(o) \
	((o)->pos = ((o)->pos + SAU_FILE_ALEN) \
		& (SAU_FILE_BUFSIZ - 1))

/**
 * Get position relative to buffer area.
 *
 * \return position
 */
#define sauFile_APOS(o) \
	((o)->pos & (SAU_FILE_ALEN - 1))

/**
 * Get remaining length (characters after position)
 * within current buffer area.
 *
 * \return length
 */
#define sauFile_AREM(o) \
	((SAU_FILE_ALEN - 1) - ((o)->pos & (SAU_FILE_ALEN - 1)))

/**
 * Get remaining length (characters after position)
 * within buffer, i.e. before position must wrap.
 *
 * \return length
 */
#define sauFile_BREM(o) \
	((SAU_FILE_BUFSIZ - 1) - ((o)->pos & (SAU_FILE_BUFSIZ - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define sauFile_NEED_CALL(o) \
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
#define sauFile_CREM(o) \
	(((o)->call_pos - (o)->pos) & (SAU_FILE_BUFSIZ - 1))

/**
 * Increment position without limiting it to the buffer boundary.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which did not advance position.
 *
 * \return new position
 */
#define sauFile_INCP(o) (++(o)->pos)

/**
 * Decrement position without limiting it to the buffer boundary.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which advanced the position.
 *
 * \return new position
 */
#define sauFile_DECP(o) (--(o)->pos)

/**
 * Wrap position to within the buffer boundary.
 *
 * \return new position
 */
#define sauFile_FIXP(o) ((o)->pos &= SAU_FILE_BUFSIZ - 1)

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most SAU_FILE_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	SAU_FILE_OK = 0,
	SAU_FILE_END = 1<<0,
	SAU_FILE_ERROR = 1<<1,
	SAU_FILE_CHANGE = 1<<2,
	SAU_FILE_MARKER = 0x07,
};

/**
 * Callback type for closing internal reference.
 * Should close file and set \a ref to NULL, otherwise
 * leaving state unchanged.
 */
typedef void (*sauFileClose_f)(sauFile *restrict o);

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
struct sauFile {
	size_t pos;
	size_t call_pos;
	sauFileAction_f call_f;
	uint8_t status;
	size_t end_pos;
	void *ref;
	const char *path;
	sauFile *parent;
	sauFileClose_f close_f;
	uint8_t buf[SAU_FILE_BUFSIZ];
};

sauFile *sau_create_File(void) sauMalloclike;
sauFile *sau_create_sub_File(sauFile *restrict parent) sauMalloclike;
sauFile *sau_destroy_File(sauFile *restrict o);

void sauFile_init(sauFile *restrict o,
		sauFileAction_f call_f, void *restrict ref,
		const char *path, sauFileClose_f close_f);

bool sauFile_fopenrb(sauFile *restrict o, const char *restrict path);
bool sauFile_stropenrb(sauFile *restrict o,
		const char *restrict path, const char *restrict str);

void sauFile_close(sauFile *restrict o);
void sauFile_reset(sauFile *restrict o);

void sauFile_end(sauFile *restrict o, size_t keep_len, bool error);

/**
 * Check position and call callback if at the call position.
 *
 * Wraps position to within the buffer boundary.
 */
#define sauFile_UPDATE(o) ((void) \
	((sauFile_FIXP(o), sauFile_NEED_CALL(o)) \
		&& (o)->call_f((o))))

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define sauFile_RETC(o) \
	(sauFile_UPDATE((o)), \
	 (o)->buf[(o)->pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define sauFile_RETC_NC(o) \
	((o)->buf[(o)->pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to sauFile_RETC() followed by sauFile_INCP().
 *
 * \return current character
 */
#define sauFile_GETC(o) \
	(sauFile_UPDATE((o)), \
	 (o)->buf[(o)->pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define sauFile_GETC_NC(o) \
	((o)->buf[(o)->pos++])

/**
 * Undo the getting of a character, writing \p c in place of what was got.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SAU_FILE_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define sauFile_UNGETC(o, c) \
	(((o)->buf[(o)->pos = ((o)->pos - 1) & (SAU_FILE_BUFSIZ - 1)] = (c)), \
	 ((o)->pos = (o)->pos) /* bogus pos assignment to silence -Wunused */)

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define sauFile_TESTC(o, c) \
	(sauFile_UPDATE((o)), \
	 ((o)->buf[(o)->pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to sauFile_TESTC() followed by sauFile_INCP()
 * when true.
 *
 * \return true if character got
 */
#define sauFile_TRYC(o, c) \
	(sauFile_TESTC((o), c) && (sauFile_INCP((o)), true))

/**
 * Undo the getting of \p n number of characters.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SAU_FILE_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define sauFile_UNGETN(o, n) \
	((o)->pos = ((o)->pos - (n)) & (SAU_FILE_BUFSIZ - 1))

/**
 * Set current character, without advancing position.
 */
#define sauFile_SETC(o, c) ((void) \
	(sauFile_UPDATE((o)), \
	 ((o)->buf[(o)->pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define sauFile_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define sauFile_PUTC(o, c) ((void) \
	(sauFile_UPDATE((o)), \
	 ((o)->buf[(o)->pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define sauFile_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->pos++] = (c)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * sauFile_AT_EOF()/sauFile_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define sauFile_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or sauFile_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define sauFile_AT_EOF(o) \
	((o)->end_pos == (o)->pos)

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or sauFile_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define sauFile_AFTER_EOF(o) \
	((o)->end_pos == (((o)->pos - 1) & (SAU_FILE_BUFSIZ - 1)))

/**
 * Get newline in portable way, advancing position if newline read.
 *
 * \return true if newline got
 */
static inline bool sauFile_trynewline(sauFile *restrict o) {
	uint8_t c = sauFile_RETC(o);
	if (c == '\n') {
		sauFile_INCP(o);
		sauFile_TRYC(o, '\r');
		return true;
	}
	if (c == '\r') {
		sauFile_INCP(o);
		return true;
	}
	return false;
}

/**
 * Callback type for filtering characters.
 * Should return the character to use, or 0 to indicate no match.
 */
typedef uint8_t (*sauFileFilter_f)(sauFile *restrict o, uint8_t c);

bool sauFile_getstr(sauFile *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, sauFileFilter_f filter_f);
bool sauFile_geti(sauFile *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp);
bool sauFile_getd(sauFile *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp);
size_t sauFile_skipstr(sauFile *restrict o, sauFileFilter_f filter_f);
size_t sauFile_skipspace(sauFile *restrict o);
size_t sauFile_skipline(sauFile *restrict o);
