/* sgensys: Circular buffer module.
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

#pragma once
#include "common.h"

/*
 * Circular buffer type, maintaining states for reading and writing and
 * using a callback for each to perform action at chosen positions in
 * the buffer.
 *
 * The default callback simply wraps the buffer position. More advanced
 * callbacks may e.g. read from or write to a file. Each call, the
 * callback sets the position of the next call.
 */

#define SGS_CBUF_ALEN 4096
#define SGS_CBUF_ANUM 2
#define SGS_CBUF_SIZ  (SGS_CBUF_ALEN * SGS_CBUF_ANUM)

/*
 * Mode subtype...
 */

struct SGS_CBufMode;
typedef struct SGS_CBufMode SGS_CBufMode;

/**
 * Mode action callback type. Must wrap position, update call position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of characters successfully handled.
 */
typedef size_t (*SGS_CBufMode_f)(SGS_CBufMode *o);

/**
 * Data for reading or writing, with callback for performing action
 * when pos == call_pos.
 */
struct SGS_CBufMode {
	size_t pos;
	size_t call_pos;
	SGS_CBufMode_f f;
	void *ref; // set to SGS_CBuf instance by SGS_CBuf, but may be changed
};

size_t SGS_CBufMode_wrap(SGS_CBufMode *o); // default callback

void SGS_CBufMode_reset(SGS_CBufMode *o);

/**
 * Flip to the beginning of the next buffer area.
 */
#define SGS_CBufMode_ANEXT(o) \
	((o)->pos = ((o)->pos + SGS_CBUF_ALEN) \
		& ((SGS_CBUF_SIZ - 1) & ~(SGS_CBUF_ALEN - 1)))

/**
 * Flip to the next buffer area, maintaining relative position within
 * the area.
 */
#define SGS_CBufMode_AINC(o) \
	((o)->pos = ((o)->pos + SGS_CBUF_ALEN) \
		& (SGS_CBUF_SIZ - 1))

/**
 * Get position relative to buffer area.
 */
#define SGS_CBufMode_APOS(o) \
	((o)->pos & (SGS_CBUF_ALEN - 1))

/**
 * Get remaining length (characters after current position) within current
 * buffer area.
 */
#define SGS_CBufMode_AREM(o) \
	((SGS_CBUF_ALEN - 1) - ((o)->pos & (SGS_CBUF_ALEN - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define SGS_CBufMode_NEED_CALL(o) \
	((o)->pos == (o)->call_pos)

/**
 * Call callback if needed.
 */
#define SGS_CBufMode_HANDLE_CALL(o) ((void) \
	(SGS_CBufMode_NEED_CALL(o) && (o)->f(o)))

/**
 * Get remaining length (characters after position) before
 * callback should be called.
 *
 * If position is greater than call position, assumes that
 * it has been decreased and wrapped around as in e.g. character
 * ungetting. If length between calls is smaller than the buffer
 * size (e.g. the buffer area length), a length longer than that
 * between calls may then be returned.
 */
#define SGS_CBufMode_CBREM(o) ((size_t) \
	((o)->call_pos < (o)->pos) ? \
	((SGS_CBUF_SIZ + (o)->call_pos - (o)->pos) & (SGS_CBUF_SIZ - 1)) : \
	((o)->call_pos - (o)->pos))

/**
 * Increment position. No checking is done.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which handles callback but does not advance position. In other cases,
 * may require SGS_CBufMode_FIXP() after.
 *
 * \return new position
 */
#define SGS_CBufMode_INCP(o) (++(o)->pos)

/**
 * Decrement position. No checking is done.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which handles callback and did advance position. In other cases,
 * may require SGS_CBufMode_FIXP() after.
 *
 * The more flexible alternatives are SGS_CBuf_UNGETC() and
 * SGS_CBuf_UNGETN().
 *
 * \return new position
 */
#define SGS_CBufMode_DECP(o) (--(o)->pos)

/**
 * Ensure position is correct after unsafe alteration.
 */
#define SGS_CBufMode_FIXP(o) ((void) \
	(SGS_CBufMode_NEED_CALL(o) || ((o)->pos &= SGS_CBUF_SIZ - 1)))

/*
 * Main type...
 */

/**
 * Circular buffer type. Contains buffer, and reading and writing modes.
 */
typedef struct SGS_CBuf {
	uint8_t *buf;
	SGS_CBufMode r;
	SGS_CBufMode w;
} SGS_CBuf;

bool SGS_init_CBuf(SGS_CBuf *o);
void SGS_fini_CBuf(SGS_CBuf *o);

void SGS_CBuf_zero(SGS_CBuf *o);
void SGS_CBuf_reset(SGS_CBuf *o);

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define SGS_CBuf_RETC(o) \
	(SGS_CBufMode_HANDLE_CALL(&(o)->r), \
	 (o)->buf[(o)->r.pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define SGS_CBuf_RETC_NC(o) \
	((o)->buf[(o)->r.pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to SGS_CBuf_RETC() followed by SGS_CBuf_INCP().
 *
 * \return current character
 */
#define SGS_CBuf_GETC(o) \
	(SGS_CBufMode_HANDLE_CALL(&(o)->r), \
	 (o)->buf[(o)->r.pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define SGS_CBuf_GETC_NC(o) \
	((o)->buf[(o)->r.pos++])

/**
 * Undo the getting of a character. This can safely be done a number of
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SGS_CBUF_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 */
#define SGS_CBuf_UNGETC(o) ((void) \
	((o)->r.pos = ((o)->r.pos - 1) & (SGS_CBUF_SIZ - 1)))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define SGS_CBuf_TESTC(o, c) \
	(SGS_CBufMode_HANDLE_CALL(&(o)->r), \
	 ((o)->buf[(o)->r.pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to SGS_CBuf_TESTC() followed by SGS_CBuf_INCP()
 * when true.
 *
 * \return true if character got
 */
#define SGS_CBuf_TRYC(o, c) \
	(SGS_CBuf_TESTC(o, c) && (++(o)->r.pos, true))

//bool SGS_CBuf_getn(SGS_CBuf *o, char *buf, size_t *n);

/**
 * Undo the getting of \p n number of characters.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SGS_CBUF_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 */
#define SGS_CBuf_UNGETN(o, n) ((void) \
	(((n) > 0) && \
	 ((o)->r.pos = ((o)->r.pos - (n)) & (SGS_CBUF_SIZ - 1))))

/**
 * Set current character, without advancing position.
 */
#define SGS_CBuf_SETC(o, c) ((void) \
	(SGS_CBufMode_HANDLE_CALL(&(o)->w), \
	 ((o)->buf[(o)->w.pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define SGS_CBuf_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->w.pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define SGS_CBuf_PUTC(o, c) ((void) \
	(SGS_CBufMode_HANDLE_CALL(&(o)->w), \
	 ((o)->buf[(o)->w.pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define SGS_CBuf_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->w.pos++] = (c)))

//bool SGS_CBuf_putn(SGS_CBuf *o, const char *buf, size_t *n);
