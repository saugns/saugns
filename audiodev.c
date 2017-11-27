/* sgensys: system audio output support module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
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

struct SGSAudioDev {
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
 * written any number of times using SGS_audiodev_write().
 *
 * Returns SGSAudioDev or NULL if opening the device fails.
 */
SGSAudioDev *SGS_open_audiodev(uint16_t channels, uint32_t *srate) {
#ifdef linux
	return open_linux_audiodev(ALSA_NAME_OUT, OSS_NAME_OUT, O_WRONLY,
			channels, srate);
#else
	return open_oss_audiodev(OSS_NAME_OUT, O_WRONLY, channels, srate);
#endif
}

/**
 * Close the given audio device. The structure is freed.
 */
void SGS_close_audiodev(SGSAudioDev *ad) {
#ifdef linux
	close_linux_audiodev(ad);
#else
	close_oss_audiodev(ad);
#endif
}

/**
 * Return sample rate set for system audio output.
 */
uint32_t SGS_audiodev_get_srate(const SGSAudioDev *ad) {
	return ad->srate;
}

/**
 * Write the given number of samples from buf to the audio device, the former
 * assumed to be in the format for which the audio device was opened. If
 * opened for multiple channels, buf is assumed to be interleaved and of
 * channels * samples length.
 *
 * Returns true upon suceessful write, otherwise false;
 */
bool SGS_audiodev_write(SGSAudioDev *ad, const int16_t *buf, uint32_t samples) {
#ifdef linux
	return linux_audiodev_write(ad, buf, samples);
#else
	return oss_audiodev_write(ad, buf, samples);
#endif
}
