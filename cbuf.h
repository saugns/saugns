/* ssndgen: Circular buffer module.
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

#define SSG_CBUF_ALEN 4096
#define SSG_CBUF_ANUM 2
#define SSG_CBUF_SIZ  (SSG_CBUF_ALEN * SSG_CBUF_ANUM)

/*
 * Mode subtype...
 */

struct SSG_CBufMode;
typedef struct SSG_CBufMode SSG_CBufMode;

/**
 * Mode action callback type. Must wrap position, update call position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of characters successfully handled.
 */
typedef size_t (*SSG_CBufMode_f)(SSG_CBufMode *o);

/**
 * Data for reading or writing, with callback for performing action
 * when pos == call_pos.
 */
struct SSG_CBufMode {
	size_t pos;
	size_t call_pos;
	SSG_CBufMode_f f;
	void *ref; // set to SSG_CBuf instance by SSG_CBuf, but may be changed
};

size_t SSG_CBufMode_wrap(SSG_CBufMode *o); // default callback

void SSG_CBufMode_reset(SSG_CBufMode *o);

/**
 * Flip to the beginning of the next buffer area.
 */
#define SSG_CBufMode_ANEXT(o) \
	((o)->pos = ((o)->pos + SSG_CBUF_ALEN) \
		& ((SSG_CBUF_SIZ - 1) & ~(SSG_CBUF_ALEN - 1)))

/**
 * Flip to the next buffer area, maintaining relative position within
 * the area.
 */
#define SSG_CBufMode_AINC(o) \
	((o)->pos = ((o)->pos + SSG_CBUF_ALEN) \
		& (SSG_CBUF_SIZ - 1))

/**
 * Get position relative to buffer area.
 */
#define SSG_CBufMode_APOS(o) \
	((o)->pos & (SSG_CBUF_ALEN - 1))

/**
 * Get remaining length (characters after current position) within current
 * buffer area.
 */
#define SSG_CBufMode_AREM(o) \
	((SSG_CBUF_ALEN - 1) - ((o)->pos & (SSG_CBUF_ALEN - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define SSG_CBufMode_NEED_CALL(o) \
	((o)->pos == (o)->call_pos)

/**
 * Call callback if needed.
 */
#define SSG_CBufMode_HANDLE_CALL(o) ((void) \
	(SSG_CBufMode_NEED_CALL(o) && (o)->f(o)))

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
#define SSG_CBufMode_CBREM(o) ((size_t) \
	((o)->call_pos < (o)->pos) ? \
	((SSG_CBUF_SIZ + (o)->call_pos - (o)->pos) & (SSG_CBUF_SIZ - 1)) : \
	((o)->call_pos - (o)->pos))

/**
 * Increment position. No checking is done.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which handles callback but does not advance position. In other cases,
 * may require SSG_CBufMode_FIXP() after.
 *
 * \return new position
 */
#define SSG_CBufMode_INCP(o) (++(o)->pos)

/**
 * Decrement position. No checking is done.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which handles callback and did advance position. In other cases,
 * may require SSG_CBufMode_FIXP() after.
 *
 * The more flexible alternatives are SSG_CBuf_UNGETC() and
 * SSG_CBuf_UNGETN().
 *
 * \return new position
 */
#define SSG_CBufMode_DECP(o) (--(o)->pos)

/**
 * Ensure position is correct after unsafe alteration.
 */
#define SSG_CBufMode_FIXP(o) ((void) \
	(SSG_CBufMode_NEED_CALL(o) || ((o)->pos &= SSG_CBUF_SIZ - 1)))

/*
 * Main type...
 */

/**
 * Circular buffer type. Contains buffer, and reading and writing modes.
 */
typedef struct SSG_CBuf {
	uint8_t *buf;
	SSG_CBufMode r;
	SSG_CBufMode w;
} SSG_CBuf;

bool SSG_init_CBuf(SSG_CBuf *o);
void SSG_fini_CBuf(SSG_CBuf *o);

void SSG_CBuf_zero(SSG_CBuf *o);
void SSG_CBuf_reset(SSG_CBuf *o);

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define SSG_CBuf_RETC(o) \
	(SSG_CBufMode_HANDLE_CALL(&(o)->r), \
	 (o)->buf[(o)->r.pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define SSG_CBuf_RETC_NC(o) \
	((o)->buf[(o)->r.pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to SSG_CBuf_RETC() followed by SSG_CBuf_INCP().
 *
 * \return current character
 */
#define SSG_CBuf_GETC(o) \
	(SSG_CBufMode_HANDLE_CALL(&(o)->r), \
	 (o)->buf[(o)->r.pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define SSG_CBuf_GETC_NC(o) \
	((o)->buf[(o)->r.pos++])

/**
 * Undo the getting of a character. This can safely be done a number of
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SSG_CBUF_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 */
#define SSG_CBuf_UNGETC(o) ((void) \
	((o)->r.pos = ((o)->r.pos - 1) & (SSG_CBUF_SIZ - 1)))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define SSG_CBuf_TESTC(o, c) \
	(SSG_CBufMode_HANDLE_CALL(&(o)->r), \
	 ((o)->buf[(o)->r.pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to SSG_CBuf_TESTC() followed by SSG_CBuf_INCP()
 * when true.
 *
 * \return true if character got
 */
#define SSG_CBuf_TRYC(o, c) \
	(SSG_CBuf_TESTC(o, c) && (++(o)->r.pos, true))

//bool SSG_CBuf_getn(SSG_CBuf *o, char *buf, size_t *n);

/**
 * Undo the getting of \p n number of characters.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SSG_CBUF_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 */
#define SSG_CBuf_UNGETN(o, n) ((void) \
	(((n) > 0) && \
	 ((o)->r.pos = ((o)->r.pos - (n)) & (SSG_CBUF_SIZ - 1))))

/**
 * Set current character, without advancing position.
 */
#define SSG_CBuf_SETC(o, c) ((void) \
	(SSG_CBufMode_HANDLE_CALL(&(o)->w), \
	 ((o)->buf[(o)->w.pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define SSG_CBuf_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->w.pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define SSG_CBuf_PUTC(o, c) ((void) \
	(SSG_CBufMode_HANDLE_CALL(&(o)->w), \
	 ((o)->buf[(o)->w.pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define SSG_CBuf_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->w.pos++] = (c)))

//bool SSG_CBuf_putn(SSG_CBuf *o, const char *buf, size_t *n);
