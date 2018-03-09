/* sgensys: OSS audio output support.
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
 * Returns instance if successful, NULL on error.
 */
static inline SGS_AudioDev *open_AudioDev_oss(const char *name, int mode,
		uint16_t channels, uint32_t *srate) {
	int tmp, fd;

	if ((fd = open(name, mode, 0)) == -1) {
		perror(name);
		return NULL;
	}

	tmp = AFMT_S16_NE;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &tmp) == -1) {
		perror("SNDCTL_DSP_SETFMT");
		goto ERROR;
	}
	if (tmp != AFMT_S16_NE) {
		fputs("error: OSS output didn't support 16-bit signed integer native endian format\n",
			stderr);
                goto ERROR;
        }

	tmp = channels;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		perror("SNDCTL_DSP_CHANNELS");
		goto ERROR;
	}
	if (tmp != channels) {
		fprintf(stderr, "error: OSS output didn't support %d channels\n",
			channels);
                goto ERROR;
        }

	tmp = *srate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		perror("SNDCTL_DSP_SPEED");
		goto ERROR;
	}
	if (tmp != (int)*srate) {
		fprintf(stderr, "warning: OSS output didn't support sample rate %d, setting it to %d\n",
			*srate, tmp);
		*srate = tmp;
	}

	SGS_AudioDev *o = malloc(sizeof(SGS_AudioDev));
	o->ref.fd = fd;
	o->type = TYPE_OSS;
	o->channels = channels;
	o->srate = *srate;
	return o;

ERROR:
	close(fd);
	fprintf(stderr, "error: OSS output configuration for device \"%s\" failed\n",
			name);
	return NULL;
}

/*
 * Close the given audio device. Destroys the instance.
 */
static inline void close_AudioDev_oss(SGS_AudioDev *o) {
	close(o->ref.fd);
	free(o);
}

/*
 * Returns true upon suceessful write, otherwise false;
 */
static inline bool audiodev_oss_write(SGS_AudioDev *o, const int16_t *buf,
		uint32_t samples) {
	size_t length = samples * o->channels * SOUND_BYTES, written;

	written = write(o->ref.fd, buf, length);
	return (written == length);
}
