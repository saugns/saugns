/* saugns: OSS audio output support.
 * Copyright (c) 2011-2014, 2017-2021 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#ifdef __NetBSD__
# include <soundcard.h>
# define OSS_NAME_OUT "/dev/audio"
#else
# include <sys/soundcard.h>
# define OSS_NAME_OUT "/dev/dsp"
#endif

/*
 * \return instance or NULL on failure
 */
static inline bool open_oss(SGS_AudioDev *restrict o,
		int mode) {
	const char *dev_name = o->name;
	const char *err_name = NULL;
	int tmp, fd;

	if (dev_name != NULL && !strcmp(dev_name, "default")) dev_name = NULL;
	if (!dev_name) dev_name = getenv_nonblank("OSS_AUDIODEV");
	if (!dev_name) dev_name = OSS_NAME_OUT;
	if ((fd = open(dev_name, mode, 0)) == -1) {
		err_name = dev_name;
		goto ERROR;
	}

	tmp = AFMT_S16_NE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
		err_name = "SNDCTL_DSP_SETFMT";
		goto ERROR;
	}
	if (tmp != AFMT_S16_NE) {
		sau_error("OSS", "16-bit signed integer native endian format unsupported");
		goto ERROR;
	}

	tmp = o->channels;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		err_name = "SNDCTL_DSP_CHANNELS";
		goto ERROR;
	}
	if (tmp != o->channels) {
		sau_error("OSS", "%d channels unsupported",
			o->channels);
		goto ERROR;
	}

	tmp = o->srate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		err_name = "SNDCTL_DSP_SPEED";
		goto ERROR;
	}
	if ((uint32_t) tmp != o->srate) {
		sau_warning("OSS", "sample rate %d unsupported, using %d",
			o->srate, tmp);
		o->srate = tmp;
	}

	o->ref.fd = fd;
	o->name = dev_name;
	o->type = TYPE_OSS;
	return true;
ERROR:
	if (err_name)
		sau_error("OSS", "%s: %s", err_name, strerror(errno));
	if (fd != -1)
		close(fd);
	sau_error("OSS", "configuration for device \"%s\" failed", dev_name);
	return false;
}

/*
 * Destroy instance. Close OSS device,
 * ending playback in the process.
 */
static inline void close_oss(SGS_AudioDev *restrict o) {
	close(o->ref.fd);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool oss_write(SGS_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples) {
	size_t length = samples * o->channels * SOUND_BYTES;
	size_t written = write(o->ref.fd, buf, length);

	return (written == length);
}
