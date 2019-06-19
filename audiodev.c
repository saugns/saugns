/* saugns: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2019 Joel K. Pettersson
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

#ifdef __linux
// Needed to avoid type conflicts on Linux.
// For now, simply define it on Linux only.
// DragonFly's sys/soundcard.h uses BSD type names,
// which are missing if _POSIX_C_SOURCE is defined.
# define _POSIX_C_SOURCE 200809L
#endif
#include "audiodev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

union DevRef {
	int fd;
	void *handle;
};

enum {
	TYPE_OSS = 0,
	TYPE_ALSA,
	TYPE_SNDIO,
};

struct SAU_AudioDev {
	union DevRef ref;
	uint8_t type;
	uint16_t channels;
	uint32_t srate;
};

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

#ifdef __linux
# include "audiodev/linux.c"
#elif defined(__OpenBSD__)
# include "audiodev/sndio.c"
#else
# include "audiodev/oss.c"
#endif

/**
 * Open audio device for 16-bit sound output. Sound data may thereafter be
 * written any number of times using SAU_AudioDev_write().
 *
 * \return instance or NULL on failure
 */
SAU_AudioDev *SAU_open_AudioDev(uint16_t channels, uint32_t *restrict srate) {
	SAU_AudioDev *o;
#ifdef __linux
	o = open_linux(ALSA_NAME_OUT, OSS_NAME_OUT, O_WRONLY,
			channels, srate);
#elif defined(__OpenBSD__)
	o = open_sndio(SNDIO_NAME_OUT, SIO_PLAY, channels, srate);
#else
	o = open_oss(OSS_NAME_OUT, O_WRONLY, channels, srate);
#endif
	if (!o) {
		SAU_error(NULL, "couldn't open audio device for output");
		return NULL;
	}
	return o;
}

/**
 * Close the given audio device. Destroys the instance.
 */
void SAU_close_AudioDev(SAU_AudioDev *restrict o) {
#ifdef __linux
	close_linux(o);
#elif defined(__OpenBSD__)
	close_sndio(o);
#else
	close_oss(o);
#endif
}

/**
 * Return sample rate set for system audio output.
 */
uint32_t SAU_AudioDev_get_srate(const SAU_AudioDev *restrict o) {
	return o->srate;
}

/**
 * Write the given number of samples from buf to the audio device, the former
 * assumed to be in the format for which the audio device was opened. If
 * opened for multiple channels, buf is assumed to be interleaved and of
 * (channels * samples) length.
 *
 * \return true upon suceessful write, otherwise false
 */
bool SAU_AudioDev_write(SAU_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples) {
#ifdef __linux
	return linux_write(o, buf, samples);
#elif defined(__OpenBSD__)
	return sndio_write(o, buf, samples);
#else
	return oss_write(o, buf, samples);
#endif
}
