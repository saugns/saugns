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

#pragma once
#include "../common.h"

#define SGS_FBUF_ALEN 4096
#define SGS_FBUF_ANUM 2
#define SGS_FBUF_SIZ  (SGS_FBUF_ALEN * SGS_FBUF_ANUM)

struct SGS_File;
typedef struct SGS_File SGS_File;

struct SGS_FBufMode;
typedef struct SGS_FBufMode SGS_FBufMode;

/**
 * Action callback type. Must update call position, may change position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of characters successfully handled.
 */
typedef size_t (*SGS_File_Action_f)(SGS_File *o, SGS_FBufMode *m);

size_t SGS_File_wrap(SGS_File *o, SGS_FBufMode *m); // default mode callback

/**
 * Mode subtype for SGS_File.
 *
 * Data for reading or writing, with callback for performing action
 * when \a pos == \a call_pos.
 */
struct SGS_FBufMode {
	size_t pos;
	size_t call_pos;
	SGS_File_Action_f f;
};

void SGS_FBufMode_reset(SGS_FBufMode *m);

/**
 * Flip to the beginning of the next buffer area.
 *
 * \return new position
 */
#define SGS_FBufMode_ANEXT(o) \
	((o)->pos = ((o)->pos + SGS_FBUF_ALEN) \
		& ((SGS_FBUF_SIZ - 1) & ~(SGS_FBUF_ALEN - 1)))

/**
 * Flip to the next buffer area,
 * maintaining relative position within the area.
 *
 * \return new position
 */
#define SGS_FBufMode_AINC(o) \
	((o)->pos = ((o)->pos + SGS_FBUF_ALEN) \
		& (SGS_FBUF_SIZ - 1))

/**
 * Get position relative to buffer area.
 *
 * \return position
 */
#define SGS_FBufMode_APOS(o) \
	((o)->pos & (SGS_FBUF_ALEN - 1))

/**
 * Get remaining length (characters after position)
 * within current buffer area.
 *
 * \return length
 */
#define SGS_FBufMode_AREM(o) \
	((SGS_FBUF_ALEN - 1) - ((o)->pos & (SGS_FBUF_ALEN - 1)))

/**
 * Get remaining length (characters after position)
 * within buffer, i.e. before position must wrap.
 *
 * \return length
 */
#define SGS_FBufMode_BREM(o) \
	((SGS_FBUF_SIZ - 1) - ((o)->pos & (SGS_FBUF_SIZ - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define SGS_FBufMode_NEED_CALL(o) \
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
#define SGS_FBufMode_CREM(o) \
	(((o)->call_pos - (o)->pos) & (SGS_FBUF_SIZ - 1)))

/**
 * Increment position without limiting it to the buffer boundary.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which did not advance position.
 *
 * \return new position
 */
#define SGS_FBufMode_INCP(o) (++(o)->pos)

/**
 * Decrement position without limiting it to the buffer boundary.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which advanced the position.
 *
 * \return new position
 */
#define SGS_FBufMode_DECP(o) (--(o)->pos)

/**
 * Wrap position to within the buffer boundary.
 */
#define SGS_FBufMode_FIXP(o) ((void) \
	((o)->pos &= SGS_FBUF_SIZ - 1))

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most SGS_File_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	SGS_FILE_OK = 0,
	SGS_FILE_END = 1<<0,
	SGS_FILE_ERROR = 1<<1,
	SGS_FILE_MARKER = 0x07,
};

/**
 * Callback type for closing internal reference.
 * Should close file and set \a ref to NULL, otherwise
 * leaving state unchanged.
 */
typedef void (*SGS_File_CloseRef_f)(SGS_File *o);

/**
 * File type using circular buffer, meant for scanning and parsing.
 *
 * Maintains states for reading and writing to the buffer and
 * calling a function for each mode to perform action at chosen
 * positions in the buffer.
 *
 * The default callback simply increases and wraps the call position.
 * Opening a file for reading sets a callback to fill the buffer
 * one area at a time.
 */
struct SGS_File {
	SGS_FBufMode mr;
	SGS_FBufMode mw;
	uint8_t status;
	size_t end_pos;
	void *ref;
	const char *path;
	SGS_File_CloseRef_f close_f;
	uint8_t buf[SGS_FBUF_SIZ];
};

SGS_File *SGS_create_File(void) SGS__malloclike;
void SGS_destroy_File(SGS_File *restrict o);

bool SGS_File_fopenrb(SGS_File *restrict o, const char *restrict path);

void SGS_File_close(SGS_File *restrict o);
void SGS_File_reset(SGS_File *restrict o);

/**
 * Check \p mode position and call its callback if at the call position.
 *
 * Wraps position to within the buffer boundary.
 */
#define SGS_File_UPDATE(o, mode) ((void) \
	((SGS_FBufMode_FIXP(mode), SGS_FBufMode_NEED_CALL(mode)) \
		&& (mode)->f((o), (mode))))

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define SGS_File_RETC(o) \
	(SGS_File_UPDATE((o), &(o)->mr), \
	 (o)->buf[(o)->mr.pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define SGS_File_RETC_NC(o) \
	((o)->buf[(o)->mr.pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to SGS_File_RETC() followed by SGS_FBufMode_INCP()
 * for the read mode.
 *
 * \return current character
 */
#define SGS_File_GETC(o) \
	(SGS_File_UPDATE((o), &(o)->mr), \
	 (o)->buf[(o)->mr.pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define SGS_File_GETC_NC(o) \
	((o)->buf[(o)->mr.pos++])

/**
 * Undo the getting of a character.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SGS_FBUF_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 */
#define SGS_File_UNGETC(o) ((void) \
	((o)->mr.pos = ((o)->mr.pos - 1) & (SGS_FBUF_SIZ - 1)))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define SGS_File_TESTC(o, c) \
	(SGS_File_UPDATE((o), &(o)->mr), \
	 ((o)->buf[(o)->mr.pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to SGS_File_TESTC() followed by SGS_FBufMode_INCP()
 * when true.
 *
 * \return true if character got
 */
#define SGS_File_TRYC(o, c) \
	(SGS_File_TESTC(o, c) && (SGS_FBufMode_INCP(&(o)->mr), true))

/**
 * Undo the getting of \p n number of characters.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SGS_FBUF_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 */
#define SGS_File_UNGETN(o, n) ((void) \
	((o)->mr.pos = ((o)->mr.pos - (n)) & (SGS_FBUF_SIZ - 1)))

/**
 * Set current character, without advancing position.
 */
#define SGS_File_SETC(o, c) ((void) \
	(SGS_File_UPDATE((o), &(o)->mw), \
	 ((o)->buf[(o)->mw.pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define SGS_File_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->mw.pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define SGS_File_PUTC(o, c) ((void) \
	(SGS_File_UPDATE((o), &(o)->mw), \
	 ((o)->buf[(o)->mw.pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define SGS_File_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->mw.pos++] = (c)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * SGS_File_AT_EOF()/SGS_File_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define SGS_File_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SGS_File_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define SGS_File_AT_EOF(o) \
	((o)->end_pos == (o)->mr.pos)

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SGS_File_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define SGS_File_AFTER_EOF(o) \
	((o)->end_pos == ((o)->mr.pos - 1))

/**
 * Get newline in portable way, advancing position if newline read.
 *
 * \return true if newline got
 */
static inline bool SGS_File_trynewline(SGS_File *restrict o) {
	uint8_t c = SGS_File_RETC(o);
	if (c == '\n') {
		SGS_FBufMode_INCP(&o->mr);
		SGS_File_TRYC(o, '\r');
		return true;
	}
	if (c == '\r') {
		SGS_FBufMode_INCP(&o->mr);
		return true;
	}
	return false;
}

/**
 * Callback type for filtering characters.
 * Should return the character to use, or 0 to indicate no match.
 */
typedef uint8_t (*SGS_File_CFilter_f)(SGS_File *o, uint8_t c);

bool SGS_File_gets(SGS_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict str_len, SGS_File_CFilter_f c_filter);
bool SGS_File_geti(SGS_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SGS_File_getd(SGS_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len);
size_t SGS_File_skips(SGS_File *restrict o, SGS_File_CFilter_f c_filter);
size_t SGS_File_skipspace(SGS_File *restrict o);
size_t SGS_File_skipline(SGS_File *restrict o);
