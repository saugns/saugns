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

#include "cbuf.h"
#include <stdlib.h>
#include <string.h>

/**
 * Initialize instance. Must only be called once before a
 * finalization.
 *
 * The ref field of the read and write modes are set to the
 * instance.
 *
 * \return true unless allocation fails
 */
bool SGS_init_CBuf(SGS_CBuf *o) {
	o->buf = calloc(1, SGS_CBUF_SIZ);
	if (!o->buf) return false;
	SGS_CBufMode_reset(&o->r);
	o->r.ref = o;
	o->w = o->r;
	return true;
}

/**
 * Finalize instance. Must only be called once after each
 * initialization.
 */
void SGS_fini_CBuf(SGS_CBuf *o) {
	free(o->buf);
	o->buf = NULL;
}

/**
 * Zero the contents of the buffer.
 */
void SGS_CBuf_zero(SGS_CBuf *o) {
	memset(o->buf, 0, SGS_CBUF_SIZ);
}

/**
 * Reset buffer. Contents are zero'd and read and write modes
 * are reset.
 *
 * The ref field of the read and write modes are left untouched.
 */
void SGS_CBuf_reset(SGS_CBuf *o) {
	memset(o->buf, 0, SGS_CBUF_SIZ);
	SGS_CBufMode_reset(&o->r);
	SGS_CBufMode_reset(&o->w);
}

/**
 * Wrap to the beginning of the buffer.
 *
 * Default callback.
 *
 * \return length of entire buffer
 */
size_t SGS_CBufMode_wrap(SGS_CBufMode *o) {
	o->pos = 0;
	o->call_pos = SGS_CBUF_SIZ; /* pre-mask pos */
	return SGS_CBUF_SIZ;
}

/**
 * Reset a mode struct instance to default values, including
 * the default callback.
 *
 * The ref field is left untouched.
 */
void SGS_CBufMode_reset(SGS_CBufMode *o) {
	o->pos = 0;
	o->call_pos = SGS_CBUF_SIZ;
	o->f = SGS_CBufMode_wrap;
}
