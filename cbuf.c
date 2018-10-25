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
bool SSG_init_CBuf(SSG_CBuf *o) {
	o->buf = calloc(1, SSG_CBUF_SIZ);
	if (!o->buf) return false;
	SSG_CBufMode_reset(&o->r);
	o->r.ref = o;
	o->w = o->r;
	return true;
}

/**
 * Finalize instance. Must only be called once after each
 * initialization.
 */
void SSG_fini_CBuf(SSG_CBuf *o) {
	free(o->buf);
	o->buf = NULL;
}

/**
 * Zero the contents of the buffer.
 */
void SSG_CBuf_zero(SSG_CBuf *o) {
	memset(o->buf, 0, SSG_CBUF_SIZ);
}

/**
 * Reset buffer. Contents are zero'd and read and write modes
 * are reset.
 *
 * The ref field of the read and write modes are left untouched.
 */
void SSG_CBuf_reset(SSG_CBuf *o) {
	memset(o->buf, 0, SSG_CBUF_SIZ);
	SSG_CBufMode_reset(&o->r);
	SSG_CBufMode_reset(&o->w);
}

/**
 * Wrap to the beginning of the buffer.
 *
 * Default callback.
 *
 * \return length of entire buffer
 */
size_t SSG_CBufMode_wrap(SSG_CBufMode *o) {
	o->pos = 0;
	o->call_pos = SSG_CBUF_SIZ; /* pre-mask pos */
	return SSG_CBUF_SIZ;
}

/**
 * Reset a mode struct instance to default values, including
 * the default callback.
 *
 * The ref field is left untouched.
 */
void SSG_CBufMode_reset(SSG_CBufMode *o) {
	o->pos = 0;
	o->call_pos = SSG_CBUF_SIZ;
	o->f = SSG_CBufMode_wrap;
}
