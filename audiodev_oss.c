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
 * Returns SGSAudioDev instance if successful, NULL on error.
 */
static inline SGSAudioDev *open_oss_audiodev(const char *name, int mode,
		uint16_t channels, uint32_t *srate) {
	SGSAudioDev *ad;
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

	ad = malloc(sizeof(SGSAudioDev));
	ad->ref.fd = fd;
	ad->type = TYPE_OSS;
	ad->channels = channels;
	ad->srate = *srate;
	return ad;

ERROR:
	close(fd);
	fprintf(stderr, "error: OSS output configuration for device \"%s\" failed\n",
			name);
	return NULL;
}

/*
 * Close the given audio device. The structure is freed.
 */
static inline void close_oss_audiodev(SGSAudioDev *ad) {
	close(ad->ref.fd);
	free(ad);
}

/*
 * Returns true upon suceessful write, otherwise false;
 */
static inline bool oss_audiodev_write(SGSAudioDev *ad, const int16_t *buf,
		uint32_t samples) {
	size_t length = samples * ad->channels * SOUND_BYTES, written;

	written = write(ad->ref.fd, buf, length);
	return (written == length);
}
