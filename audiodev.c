/* sgensys: System audio output support module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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

#include "audiodev.h"
#include <stdio.h>
#include <stdlib.h>

union DevRef {
	int fd;
	void *handle;
};

enum {
	TYPE_OSS = 0,
	TYPE_ALSA,
};

struct SGS_AudioDev {
	union DevRef ref;
	uint8_t type;
	uint16_t channels;
	uint32_t srate;
};

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

#ifdef linux
# include "audiodev_linux.c"
#else
# include "audiodev_oss.c"
#endif

/**
 * Open audio device for 16-bit sound output. Sound data may thereafter be
 * written any number of times using SGS_AudioDev_write().
 *
 * Returns instance or NULL if opening the device fails.
 */
SGS_AudioDev *SGS_open_AudioDev(uint16_t channels, uint32_t *srate) {
#ifdef linux
	return open_AudioDev_linux(ALSA_NAME_OUT, OSS_NAME_OUT, O_WRONLY,
			channels, srate);
#else
	return open_AudioDev_oss(OSS_NAME_OUT, O_WRONLY, channels, srate);
#endif
}

/**
 * Close the given audio device. Destroys the instance.
 */
void SGS_close_AudioDev(SGS_AudioDev *o) {
#ifdef linux
	close_AudioDev_linux(o);
#else
	close_AudioDev_oss(o);
#endif
}

/**
 * Return sample rate set for this instance.
 */
uint32_t SGS_AudioDev_get_srate(const SGS_AudioDev *o) {
	return o->srate;
}

/**
 * Write the given number of samples from buf to the audio device, the former
 * assumed to be in the format for which the audio device was opened. If
 * opened for multiple channels, buf is assumed to be interleaved and of
 * channels * samples length.
 *
 * Returns true upon suceessful write, otherwise false;
 */
bool SGS_AudioDev_write(SGS_AudioDev *o, const int16_t *buf,
		uint32_t samples) {
#ifdef linux
	return audiodev_linux_write(o, buf, samples);
#else
	return audiodev_oss_write(o, buf, samples);
#endif
}
