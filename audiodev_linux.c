/* Linux audio output support.
 * Copyright (c) 2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "audiodev_oss.c"
#include <alsa/asoundlib.h>
#define ALSA_NAME_OUT "default"

/*
 * Returns nonzero if the ALSA kernel module is loaded.
 */
static uchar alsa_enabled() {
	char buf[16];
	FILE *alsa_check = popen("lsmod | grep soundcore", "r");
	uchar ret = (fread(buf, 1, sizeof (buf), alsa_check) > 0);
	pclose(alsa_check);
	return ret;
}


// need snd_pcm_prepare for playback ?

/*
 * Returns 0 if successful, nonzero on error.
 */
static inline SGSAudioDev *open_linux_audio_dev(const char *alsa_name,
		const char *oss_name, int oss_mode, ushort channels,
		uint srate) {
	SGSAudioDev *ad;
	uint tmp;
	int err;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *params = NULL;

	if (!alsa_enabled())
		return open_oss_audio_dev(oss_name, oss_mode, channels, srate);

	if ((err = snd_pcm_open(&handle, alsa_name, SND_PCM_STREAM_PLAYBACK,
			0)) < 0)
		goto ERROR;

	snd_pcm_hw_params_alloca(&params);
	tmp = srate;
	if (!params ||
	    (err = snd_pcm_hw_params_any(handle, params)) < 0 ||
	    (err = snd_pcm_hw_params_set_access(handle, params,
			SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
	    (err = snd_pcm_hw_params_set_format(handle, params,
			SND_PCM_FORMAT_S16)) < 0 ||
	    (err = snd_pcm_hw_params_set_channels(handle, params,
			channels)) < 0 ||
	    (err = snd_pcm_hw_params_set_rate_near(handle, params, &tmp,
			0)) < 0 ||
	    (err = snd_pcm_hw_params(handle, params)) < 0)
		goto ERROR;
	if (tmp != srate) {
		fprintf(stderr, "warning: using audio device sample rate %d (rather than %d)\n",
				tmp, srate);
		srate = tmp;
	}

	ad = malloc(sizeof(SGSAudioDev));
	ad->ref.handle = handle;
	ad->type = TYPE_ALSA;
	ad->channels = channels;
	ad->srate = srate;
	return ad;

ERROR:
	if (handle) snd_pcm_close(handle);
	if (params) snd_pcm_hw_params_free(params);
	fprintf(stderr, "error: %s\n", snd_strerror(err));
	fprintf(stderr, "error: configuration for ALSA device \"%s\" failed\n",
			alsa_name);
	return NULL;
}

/*
 * Close the given audio device. The structure is freed.
 */
static inline void close_linux_audio_dev(SGSAudioDev *ad) {
	if (ad->type == TYPE_OSS) {
		close_oss_audio_dev(ad);
		return;
	}
	
	snd_pcm_drain(ad->ref.handle);
	snd_pcm_close(ad->ref.handle);
	free(ad);
}

/*
 * Returns zero upon suceessful write, otherwise non-zero.
 */
static inline uchar linux_audio_dev_write(SGSAudioDev *ad, const short *buf,
		uint samples) {
	snd_pcm_sframes_t written;

	if (ad->type == TYPE_OSS)
		return oss_audio_dev_write(ad, buf, samples);

	while ((written = snd_pcm_writei(ad->ref.handle, buf, samples)) < 0) {
		if (written == -EPIPE) {
			fputs("warning: audio device buffer underrun\n", stderr);
			snd_pcm_prepare(ad->ref.handle);
		} else {
			fprintf(stderr, "warning: %s\n", snd_strerror(written));
			break;
		}
	}

	return (written != (snd_pcm_sframes_t) samples);
}
