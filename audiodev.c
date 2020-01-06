/* mgensys: System audio output support module (individually licensed)
 * Copyright (c) 2011-2014, 2017-2018, 2020 Joel K. Pettersson
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

#ifdef __linux
// Needed to avoid type conflicts on Linux.
// For now, simply define it on Linux only.
// DragonFly's sys/soundcard.h uses BSD type names,
// which are missing if _POSIX_C_SOURCE is defined.
# define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdbool.h>
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

struct MGSAudioDev {
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
 * written any number of times using MGS_audiodev_write().
 *
 * \return instance or NULL on failure
 */
MGSAudioDev *MGS_open_audiodev(uint16_t channels, uint32_t *srate) {
	MGSAudioDev *o;
#ifdef __linux
	o = open_linux(ALSA_NAME_OUT, OSS_NAME_OUT, O_WRONLY,
			channels, srate);
#elif defined(__OpenBSD__)
	o = open_sndio(SNDIO_NAME_OUT, SIO_PLAY, channels, srate);
#else
	o = open_oss(OSS_NAME_OUT, O_WRONLY, channels, srate);
#endif
	if (!o) {
		fprintf(stderr, "error: couldn't open audio device for output\n");
		return NULL;
	}
	return o;
}

/**
 * Close the given audio device. Destroys the instance.
 */
void MGS_close_audiodev(MGSAudioDev *o) {
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
uint32_t MGS_audiodev_get_srate(const MGSAudioDev *o) {
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
bool MGS_audiodev_write(MGSAudioDev *o, const int16_t *buf, uint32_t samples) {
#ifdef __linux
	return linux_write(o, buf, samples);
#elif defined(__OpenBSD__)
	return sndio_write(o, buf, samples);
#else
	return oss_write(o, buf, samples);
#endif
}
