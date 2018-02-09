/* sgensys: OSS audio output support.
 * Copyright (c) 2011-2014, 2017-2018 Joel K. Pettersson
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
static inline SGSAudioDev *open_oss(const char *name, int mode,
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

	SGSAudioDev *o = malloc(sizeof(SGSAudioDev));
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
static inline void close_oss(SGSAudioDev *o) {
	close(o->ref.fd);
	free(o);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool oss_write(SGSAudioDev *o, const int16_t *buf,
		uint32_t samples) {
	size_t length = samples * o->channels * SOUND_BYTES;
	size_t written = write(o->ref.fd, buf, length);

	return (written == length);
}
