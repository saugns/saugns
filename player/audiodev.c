/* saugns: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2021 Joel K. Pettersson
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
	const char *name;
	uint8_t type;
	uint16_t channels;
	uint32_t srate;
};

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

static const char *getenv_nonblank(const char *restrict env_name) {
	const char *name = getenv(env_name);
	if (name != NULL && name[0] == '\0') name = NULL;
	return name;
}

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
	SAU_AudioDev *o = calloc(1, sizeof(SAU_AudioDev));
	if (!o) goto ERROR;
	/*
	 * Set generic values before platform-specific handling...
	 */
	o->name = getenv_nonblank("AUDIODEV");
	o->channels = channels;
	o->srate = *srate; /* requested, ideal rate */
#ifdef __linux
	if (!open_linux(o, O_WRONLY)) goto ERROR;
#elif defined(__OpenBSD__)
	if (!open_sndio(o, SIO_PLAY)) goto ERROR;
#else
	if (!open_oss(o, O_WRONLY)) goto ERROR;
#endif
	*srate = o->srate;
	return o;
ERROR:
	SAU_error(NULL, "couldn't open audio device for output");
	free(o);
	return NULL;
}

/**
 * Close the given audio device. Destroys the instance.
 */
void SAU_close_AudioDev(SAU_AudioDev *restrict o) {
	if (!o)
		return;
#ifdef __linux
	close_linux(o);
#elif defined(__OpenBSD__)
	close_sndio(o);
#else
	close_oss(o);
#endif
	free(o);
}

/**
 * \return sample rate set for system audio output
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
