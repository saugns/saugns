/* OSS audio output support.
 * Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#define OSS_NAME_OUT "/dev/dsp"

/*
 * Returns 0 if successful, nonzero on error.
 */
static inline SGSAudioDev *open_oss_audio_dev(const char *name, int mode,
		ushort channels, uint srate) {
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
		fputs("error: 16-bit signed integer native endian format unsupported by device\n",
				stderr);
                goto ERROR;
        }

	tmp = channels;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &tmp) == -1) {
		perror("SNDCTL_DSP_CHANNELS");
		goto ERROR;
	}
	if (tmp != channels) {
		fprintf(stderr, "error: %d channels unsupported by device\n",
				channels);
                goto ERROR;
        }

	tmp = srate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		perror("SNDCTL_DSP_SPEED");
		goto ERROR;
	}
	if (tmp != (int)srate) {
		fprintf(stderr, "warning: using audio device sample rate %d (rather than %d)\n",
				tmp, srate);
		srate = tmp;
	}

	ad = malloc(sizeof(SGSAudioDev));
	ad->ref.fd = fd;
	ad->type = TYPE_OSS;
	ad->channels = channels;
	ad->srate = srate;
	return ad;

ERROR:
	close(fd);
	fprintf(stderr, "error: configuration for OSS device \"%s\" failed\n",
			name);
	return NULL;
}

/*
 * Close the given audio device. The structure is freed.
 */
static inline void close_oss_audio_dev(SGSAudioDev *ad) {
	close(ad->ref.fd);
	free(ad);
}

/*
 * Returns zero upon suceessful write, otherwise non-zero.
 */
static inline uchar oss_audio_dev_write(SGSAudioDev *ad, const short *buf,
		uint samples) {
	size_t length = samples * ad->channels * SOUND_BYTES, written;

	written = write(ad->ref.fd, buf, length);
	return (written != length);
}
