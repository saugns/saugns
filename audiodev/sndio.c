/* saugns: sndio audio output support.
 * Copyright (c) 2018-2019 Joel K. Pettersson
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

#include <sndio.h>
#define SNDIO_NAME_OUT "default"

/*
 * \return instance or NULL on failure
 */
static inline SAU_AudioDev *open_sndio(const char *restrict name, unsigned mode,
		uint16_t channels, uint32_t *restrict srate) {
	struct sio_hdl *hdl = sio_open(name, mode, 0);
	if (!hdl) goto ERROR;

	struct sio_par par;
	sio_initpar(&par);
	par.bits = SOUND_BITS;
	par.bps = SOUND_BYTES;
	par.sig = 1;
	par.le = SIO_LE_NATIVE;
	par.rchan = channels;
	par.pchan = channels;
	par.rate = *srate;
	par.xrun = SIO_SYNC;
	if ((!sio_setpar(hdl, &par)) || (!sio_getpar(hdl, &par)))
		goto ERROR;
	if (par.rate != *srate) {
		SAU_warning("sndio", "sample rate %d unsupported, using %d",
			*srate, par.rate);
		*srate = par.rate;
	}

	if (!sio_start(hdl))
		goto ERROR;

	SAU_AudioDev *o = malloc(sizeof(SAU_AudioDev));
	o->ref.handle = hdl;
	o->type = TYPE_SNDIO;
	o->channels = channels;
	o->srate = *srate;
	return o;

ERROR:
	SAU_error("sndio", "configuration for device \"%s\" failed",
		name);
	return NULL;
}

/*
 * Destroy instance. Close sndio device,
 * ending playback in the process.
 */
static inline void close_sndio(SAU_AudioDev *o) {
	sio_close(o->ref.handle);
	free(o);
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
