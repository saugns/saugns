/* saugns: Text file reader module.
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

#define SAU_FBUF_ALEN 4096
#define SAU_FBUF_ANUM 2
#define SAU_FBUF_SIZ  (SAU_FBUF_ALEN * SAU_FBUF_ANUM)

struct SAU_File;
typedef struct SAU_File SAU_File;

struct SAU_FBufMode;
typedef struct SAU_FBufMode SAU_FBufMode;

/**
 * Action callback type. Must update call position, may change position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of characters successfully handled.
 */
typedef size_t (*SAU_File_Action_f)(SAU_File *o, SAU_FBufMode *m);

size_t SAU_File_wrap(SAU_File *o, SAU_FBufMode *m); // default mode callback

/**
 * Mode subtype for SAU_File.
 *
 * Data for reading or writing, with callback for performing action
 * when \a pos == \a call_pos.
 */
struct SAU_FBufMode {
	size_t pos;
	size_t call_pos;
	SAU_File_Action_f f;
};

void SAU_FBufMode_reset(SAU_FBufMode *m);

/**
 * Flip to the beginning of the next buffer area.
 *
 * \return new position
 */
#define SAU_FBufMode_ANEXT(o) \
	((o)->pos = ((o)->pos + SAU_FBUF_ALEN) \
		& ((SAU_FBUF_SIZ - 1) & ~(SAU_FBUF_ALEN - 1)))

/**
 * Flip to the next buffer area,
 * maintaining relative position within the area.
 *
 * \return new position
 */
#define SAU_FBufMode_AINC(o) \
	((o)->pos = ((o)->pos + SAU_FBUF_ALEN) \
		& (SAU_FBUF_SIZ - 1))

/**
 * Get position relative to buffer area.
 *
 * \return position
 */
#define SAU_FBufMode_APOS(o) \
	((o)->pos & (SAU_FBUF_ALEN - 1))

/**
 * Get remaining length (characters after position)
 * within current buffer area.
 *
 * \return length
 */
#define SAU_FBufMode_AREM(o) \
	((SAU_FBUF_ALEN - 1) - ((o)->pos & (SAU_FBUF_ALEN - 1)))

/**
 * Get remaining length (characters after position)
 * within buffer, i.e. before position must wrap.
 *
 * \return length
 */
#define SAU_FBufMode_BREM(o) \
	((SAU_FBUF_SIZ - 1) - ((o)->pos & (SAU_FBUF_SIZ - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define SAU_FBufMode_NEED_CALL(o) \
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
#define SAU_FBufMode_CREM(o) \
	(((o)->call_pos - (o)->pos) & (SAU_FBUF_SIZ - 1)))

/**
 * Increment position without limiting it to the buffer boundary.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which did not advance position.
 *
 * \return new position
 */
#define SAU_FBufMode_INCP(o) (++(o)->pos)

/**
 * Decrement position without limiting it to the buffer boundary.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which advanced the position.
 *
 * \return new position
 */
#define SAU_FBufMode_DECP(o) (--(o)->pos)

/**
 * Wrap position to within the buffer boundary.
 */
#define SAU_FBufMode_FIXP(o) ((void) \
	((o)->pos &= SAU_FBUF_SIZ - 1))

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most SAU_File_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	SAU_FILE_OK = 0,
	SAU_FILE_END = 1<<0,
	SAU_FILE_ERROR = 1<<1,
	SAU_FILE_MARKER = 0x07,
};

/**
 * Callback type for closing internal reference.
 * Should close file and set \a ref to NULL, otherwise
 * leaving state unchanged.
 */
typedef void (*SAU_File_CloseRef_f)(SAU_File *o);

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
struct SAU_File {
	SAU_FBufMode mr;
	SAU_FBufMode mw;
	uint8_t status;
	size_t end_pos;
	void *ref;
	const char *path;
	SAU_File_CloseRef_f close_f;
	uint8_t buf[SAU_FBUF_SIZ];
};

SAU_File *SAU_create_File(void) SAU__malloclike;
void SAU_destroy_File(SAU_File *restrict o);

bool SAU_File_fopenrb(SAU_File *restrict o, const char *restrict path);

void SAU_File_close(SAU_File *restrict o);
void SAU_File_reset(SAU_File *restrict o);

/**
 * Check \p mode position and call its callback if at the call position.
 *
 * Wraps position to within the buffer boundary.
 */
#define SAU_File_UPDATE(o, mode) ((void) \
	((SAU_FBufMode_FIXP(mode), SAU_FBufMode_NEED_CALL(mode)) \
		&& (mode)->f((o), (mode))))

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define SAU_File_RETC(o) \
	(SAU_File_UPDATE((o), &(o)->mr), \
	 (o)->buf[(o)->mr.pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define SAU_File_RETC_NC(o) \
	((o)->buf[(o)->mr.pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to SAU_File_RETC() followed by SAU_FBufMode_INCP()
 * for the read mode.
 *
 * \return current character
 */
#define SAU_File_GETC(o) \
	(SAU_File_UPDATE((o), &(o)->mr), \
	 (o)->buf[(o)->mr.pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define SAU_File_GETC_NC(o) \
	((o)->buf[(o)->mr.pos++])

/**
 * Undo the getting of a character.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SAU_FBUF_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 */
#define SAU_File_UNGETC(o) ((void) \
	((o)->mr.pos = ((o)->mr.pos - 1) & (SAU_FBUF_SIZ - 1)))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define SAU_File_TESTC(o, c) \
	(SAU_File_UPDATE((o), &(o)->mr), \
	 ((o)->buf[(o)->mr.pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to SAU_File_TESTC() followed by SAU_FBufMode_INCP()
 * when true.
 *
 * \return true if character got
 */
#define SAU_File_TRYC(o, c) \
	(SAU_File_TESTC(o, c) && (SAU_FBufMode_INCP(&(o)->mr), true))

/**
 * Undo the getting of \p n number of characters.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SAU_FBUF_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 */
#define SAU_File_UNGETN(o, n) ((void) \
	((o)->mr.pos = ((o)->mr.pos - (n)) & (SAU_FBUF_SIZ - 1)))

/**
 * Set current character, without advancing position.
 */
#define SAU_File_SETC(o, c) ((void) \
	(SAU_File_UPDATE((o), &(o)->mw), \
	 ((o)->buf[(o)->mw.pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define SAU_File_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->mw.pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define SAU_File_PUTC(o, c) ((void) \
	(SAU_File_UPDATE((o), &(o)->mw), \
	 ((o)->buf[(o)->mw.pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define SAU_File_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->mw.pos++] = (c)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * SAU_File_AT_EOF()/SAU_File_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define SAU_File_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SAU_File_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define SAU_File_AT_EOF(o) \
	((o)->end_pos == (o)->mr.pos)

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SAU_File_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define SAU_File_AFTER_EOF(o) \
	((o)->end_pos == ((o)->mr.pos - 1))

/**
 * Get newline in portable way, advancing position if newline read.
 *
 * \return true if newline got
 */
static inline bool SAU_File_trynewline(SAU_File *restrict o) {
	uint8_t c = SAU_File_RETC(o);
	if (c == '\n') {
		SAU_FBufMode_INCP(&o->mr);
		SAU_File_TRYC(o, '\r');
		return true;
	}
	if (c == '\r') {
		SAU_FBufMode_INCP(&o->mr);
		return true;
	}
	return false;
}

/**
 * Callback type for filtering characters.
 * Should return the character to use, or 0 to indicate no match.
 */
typedef uint8_t (*SAU_File_CFilter_f)(SAU_File *o, uint8_t c);

bool SAU_File_gets(SAU_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict str_len, SAU_File_CFilter_f c_filter);
bool SAU_File_geti(SAU_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SAU_File_getd(SAU_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len);
size_t SAU_File_skips(SAU_File *restrict o, SAU_File_CFilter_f c_filter);
size_t SAU_File_skipspace(SAU_File *restrict o);
size_t SAU_File_skipline(SAU_File *restrict o);
