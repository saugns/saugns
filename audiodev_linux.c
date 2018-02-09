/* sgensys: Linux audio output support.
 * Copyright (c) 2013, 2017-2018 Joel K. Pettersson
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

#include "audiodev_oss.c" /* used in fallback mechanism */
#include <alsa/asoundlib.h>
#define ALSA_NAME_OUT "default"

/*
 * Returns true if the ALSA kernel module is loaded.
 */
static bool alsa_enabled() {
	int8_t buf[16];
	FILE *alsa_check = popen("lsmod | grep soundcore", "r");
	bool ret = (fread(buf, 1, sizeof(buf), alsa_check) > 0);
	pclose(alsa_check);
	return ret;
}

/*
 * Returns SGSAudioDev instance if successful, NULL on error.
 */
static inline SGSAudioDev *open_linux_audiodev(const char *alsa_name,
		const char *oss_name, int oss_mode, uint16_t channels,
		uint32_t *srate) {
	if (!alsa_enabled())
		return open_oss_audiodev(oss_name, oss_mode, channels, srate);

	SGSAudioDev *ad;
	uint32_t tmp;
	int err;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *params = NULL;

	if ((err = snd_pcm_open(&handle, alsa_name, SND_PCM_STREAM_PLAYBACK,
			0)) < 0)
		goto ERROR;

	snd_pcm_hw_params_alloca(&params);
	tmp = *srate;
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
	if (tmp != *srate) {
		fprintf(stderr, "warning: ALSA output didn't support sample rate %d, setting it to %d\n",
				*srate, tmp);
		*srate = tmp;
	}

	ad = malloc(sizeof(SGSAudioDev));
	ad->ref.handle = handle;
	ad->type = TYPE_ALSA;
	ad->channels = channels;
	ad->srate = *srate;
	return ad;

ERROR:
	if (handle) snd_pcm_close(handle);
	if (params) snd_pcm_hw_params_free(params);
	fprintf(stderr, "error: %s\n", snd_strerror(err));
	fprintf(stderr, "error: ALSA configuration for audio device \"%s\" failed\n",
			alsa_name);
	return NULL;
}

/*
 * Close the given audio device. The structure is freed.
 */
static inline void close_linux_audiodev(SGSAudioDev *ad) {
	if (ad->type == TYPE_OSS) {
		close_oss_audiodev(ad);
		return;
	}
	
	snd_pcm_drain(ad->ref.handle);
	snd_pcm_close(ad->ref.handle);
	free(ad);
}

/*
 * Returns true upon suceessful write, otherwise false.
 */
static inline bool linux_audiodev_write(SGSAudioDev *ad, const int16_t *buf,
		uint32_t samples) {
	if (ad->type == TYPE_OSS)
		return oss_audiodev_write(ad, buf, samples);

	snd_pcm_sframes_t written;

	while ((written = snd_pcm_writei(ad->ref.handle, buf, samples)) < 0) {
		if (written == -EPIPE) {
			fputs("warning: ALSA audio device buffer underrun\n", stderr);
			snd_pcm_prepare(ad->ref.handle);
		} else {
			fprintf(stderr, "warning: %s\n", snd_strerror(written));
			break;
		}
	}

	return (written == (snd_pcm_sframes_t) samples);
}
