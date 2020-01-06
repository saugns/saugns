/* mgensys: OSS audio output support (individually licensed)
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
static inline MGSAudioDev *open_oss(const char *name, int mode,
		uint16_t channels, uint32_t *srate) {
	const char *errp = NULL;
	int tmp, fd;

	if ((fd = open(name, mode, 0)) == -1) {
		errp = name;
		goto ERROR;
	}

	tmp = AFMT_S16_NE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
		errp = "SNDCTL_DSP_SETFMT";
		goto ERROR;
	}
	if (tmp != AFMT_S16_NE) {
		fputs("error [OSS]: 16-bit signed integer native endian format unsupported\n",
			stderr);
                goto ERROR;
        }

	tmp = channels;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		errp = "SNDCTL_DSP_CHANNELS";
		goto ERROR;
	}
	if (tmp != channels) {
		fprintf(stderr, "error [OSS]: %d channels unsupported\n",
			channels);
                goto ERROR;
        }

	tmp = *srate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		errp = "SNDCTL_DSP_SPEED";
		goto ERROR;
	}
	if ((uint32_t) tmp != *srate) {
		fprintf(stderr, "warning [OSS]: sample rate %d unsupported, using %d\n",
			*srate, tmp);
		*srate = tmp;
	}

	MGSAudioDev *o = malloc(sizeof(MGSAudioDev));
	o->ref.fd = fd;
	o->type = TYPE_OSS;
	o->channels = channels;
	o->srate = *srate;
	return o;

ERROR:
	if (errp)
		fprintf(stderr, "error [OSS]: %s: %s\n", errp, strerror(errno));
	if (fd != -1)
		close(fd);
	fprintf(stderr, "error [OSS]: configuration for device \"%s\" failed\n",
		name);
	return NULL;
}

/*
 * Destroy instance. Close OSS device,
 * ending playback in the process.
 */
static inline void close_oss(MGSAudioDev *o) {
	close(o->ref.fd);
	free(o);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool oss_write(MGSAudioDev *o, const int16_t *buf,
		uint32_t samples) {
	size_t length = samples * o->channels * SOUND_BYTES;
	size_t written = write(o->ref.fd, buf, length);

	return (written == length);
}
