/* saugns: sndio audio output support.
 * Copyright (c) 2018-2021 Joel K. Pettersson
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

#include <sndio.h>
#define SNDIO_NAME_OUT "default"

/*
 * \return instance or NULL on failure
 */
static inline bool open_sndio(SAU_AudioDev *restrict o,
		unsigned mode) {
	const char *dev_name = (o->name != NULL) ? o->name : SNDIO_NAME_OUT;
	struct sio_hdl *hdl = sio_open(dev_name, mode, 0);
	if (!hdl) goto ERROR;

	struct sio_par par;
	sio_initpar(&par);
	par.bits = SOUND_BITS;
	par.bps = SOUND_BYTES;
	par.sig = 1;
	par.le = SIO_LE_NATIVE;
	par.rchan = o->channels;
	par.pchan = o->channels;
	par.rate = o->srate;
	par.xrun = SIO_SYNC;
	if ((!sio_setpar(hdl, &par)) || (!sio_getpar(hdl, &par)))
		goto ERROR;
	if (par.rate != o->srate) {
		SAU_warning("sndio", "sample rate %d unsupported, using %d",
			o->srate, par.rate);
		o->srate = par.rate;
	}

	if (!sio_start(hdl)) goto ERROR;

	o->ref.handle = hdl;
	o->name = dev_name;
	o->type = TYPE_SNDIO;
	return true;
ERROR:
	SAU_error("sndio", "configuration for device \"%s\" failed", dev_name);
	return false;
}

/*
 * Destroy instance. Close sndio device,
 * ending playback in the process.
 */
static inline void close_sndio(SAU_AudioDev *restrict o) {
	sio_close(o->ref.handle);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool sndio_write(SAU_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples) {
	size_t bytes = samples * o->channels * SOUND_BYTES;
	size_t wlen;

	wlen = sio_write(o->ref.handle, buf, bytes);
	return (wlen == bytes);
}
