/* ssndgen: OSS audio output support.
 * Copyright (c) 2011-2014, 2017-2020 Joel K. Pettersson
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
# include <soundcard.h>
# define OSS_NAME_OUT "/dev/sound"
#else
# include <sys/soundcard.h>
# define OSS_NAME_OUT "/dev/dsp"
#endif

/*
 * \return instance or NULL on failure
 */
static inline SSG_AudioDev *open_oss(const char *restrict name, int mode,
		uint16_t channels, uint32_t *restrict srate) {
	const char *err_name = NULL;
	int tmp, fd;

	if ((fd = open(name, mode, 0)) == -1) {
		err_name = name;
		goto ERROR;
	}

	tmp = AFMT_S16_NE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
		err_name = "SNDCTL_DSP_SETFMT";
		goto ERROR;
	}
	if (tmp != AFMT_S16_NE) {
		SSG_error("OSS", "16-bit signed integer native endian format unsupported");
		goto ERROR;
	}

	tmp = channels;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		err_name = "SNDCTL_DSP_CHANNELS";
		goto ERROR;
	}
	if (tmp != channels) {
		SSG_error("OSS", "%d channels unsupported",
			channels);
		goto ERROR;
	}

	tmp = *srate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		err_name = "SNDCTL_DSP_SPEED";
		goto ERROR;
	}
	if ((uint32_t) tmp != *srate) {
		SSG_warning("OSS", "sample rate %d unsupported, using %d",
			*srate, tmp);
		*srate = tmp;
	}

	SSG_AudioDev *o = malloc(sizeof(SSG_AudioDev));
	o->ref.fd = fd;
	o->type = TYPE_OSS;
	o->channels = channels;
	o->srate = *srate;
	return o;

ERROR:
	if (err_name)
		SSG_error("OSS", "%s: %s", err_name, strerror(errno));
	if (fd != -1)
		close(fd);
	SSG_error("OSS", "configuration for device \"%s\" failed",
		name);
	return NULL;
}

/*
 * Destroy instance. Close OSS device,
 * ending playback in the process.
 */
static inline void close_oss(SSG_AudioDev *restrict o) {
	close(o->ref.fd);
	free(o);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool oss_write(SSG_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples) {
	size_t length = samples * o->channels * SOUND_BYTES;
	size_t written = write(o->ref.fd, buf, length);

	return (written == length);
}
