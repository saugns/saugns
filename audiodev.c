/* Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "audiodev.h"
#include <stdio.h>
#include <stdlib.h>

union DevRef {
	int fd;
	void *handle;
};

enum {
	TYPE_ALSA = 0,
	TYPE_OSS
};

struct SGSAudioDev {
	union DevRef ref;
	uchar type;
	ushort channels;
	uint srate;
};

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

#ifdef linux
# include "audiodev_linux.c"
#else
# include "audiodev_oss.c"
#endif

/*
 * Open audio device for 16-bit sound output. Sound data may thereafter be
 * written any number of times using SGS_audio_dev_write().
 *
 * Returns NULL if opening the device fails.
 */
SGSAudioDev *SGS_open_audio_dev(ushort channels, uint srate) {
#ifdef linux
	return open_linux_audio_dev(ALSA_NAME_OUT, OSS_NAME_OUT, O_WRONLY,
			channels, srate);
#else
	return open_oss_audio_dev(OSS_NAME_OUT, O_WRONLY, channels, srate);
#endif
}

/*
 * Close the given audio device. The structure is freed.
 */
void SGS_close_audio_dev(SGSAudioDev *ad) {
#ifdef linux
	close_linux_audio_dev(ad);
#else
	close_oss_audio_dev(ad);
#endif
}

/*
 * Write the given number of samples from buf to the audio device, the former
 * assumed to be in the format for which the audio device was opened. If
 * opened for multiple channels, buf is assumed to be interleaved and of
 * channels * samples length.
 *
 * Returns zero upon suceessful write, otherwise non-zero.
 */
uchar SGS_audio_dev_write(SGSAudioDev *ad, const short *buf, uint samples) {
#ifdef linux
	return linux_audio_dev_write(ad, buf, samples);
#else
	return oss_audio_dev_write(ad, buf, samples);
#endif
}
