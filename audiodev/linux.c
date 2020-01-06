/* mgensys: Linux audio output support (individually licensed)
 * Copyright (c) 2013, 2017-2018, 2020 Joel K. Pettersson
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

#include "oss.c" /* used in fallback mechanism */
#include <alsa/asoundlib.h>
#define ALSA_NAME_OUT "default"

/*
 * Open instance for Linux, trying ALSA first,
 * then OSS if the first ALSA call fails.
 *
 * \return instance or NULL on failure
 */
static inline MGSAudioDev *open_linux(const char *alsa_name,
		const char *oss_name, int oss_mode, uint16_t channels,
		uint32_t *srate) {
	MGSAudioDev *o;
	uint32_t tmp;
	int err;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *params = NULL;

	if ((err = snd_pcm_open(&handle, alsa_name, SND_PCM_STREAM_PLAYBACK,
			0)) < 0) {
		o = open_oss(oss_name, oss_mode, channels, srate);
		if (o) {
			return o;
		}
		fprintf(stderr, "error: could neither use ALSA nor OSS\n");
		goto ERROR;
	}

	if (snd_pcm_hw_params_malloc(&params) < 0)
		goto ERROR;
	tmp = *srate;
	if (!params
	    || (err = snd_pcm_hw_params_any(handle, params)) < 0
	    || (err = snd_pcm_hw_params_set_access(handle, params,
		SND_PCM_ACCESS_RW_INTERLEAVED)) < 0
	    || (err = snd_pcm_hw_params_set_format(handle, params,
		SND_PCM_FORMAT_S16)) < 0
	    || (err = snd_pcm_hw_params_set_channels(handle, params,
		channels)) < 0
	    || (err = snd_pcm_hw_params_set_rate_near(handle, params, &tmp,
		0)) < 0
	    || (err = snd_pcm_hw_params(handle, params)) < 0)
		goto ERROR;
	if (tmp != *srate) {
		fprintf(stderr, "warning [ALSA]: sample rate %d unsupported, using %d\n",
			*srate, tmp);
		*srate = tmp;
	}

	o = malloc(sizeof(MGSAudioDev));
	o->ref.handle = handle;
	o->type = TYPE_ALSA;
	o->channels = channels;
	o->srate = *srate;
	return o;

ERROR:
	fprintf(stderr, "error [ALSA]: %s\n", snd_strerror(err));
	if (handle) snd_pcm_close(handle);
	if (params) snd_pcm_hw_params_free(params);
	fprintf(stderr,
		"error [ALSA]: configuration for device \"%s\" failed\n",
		alsa_name);
	return NULL;
}

/*
 * Destroy instance. Close ALSA or OSS device,
 * ending playback in the process.
 */
static inline void close_linux(MGSAudioDev *o) {
	if (o->type == TYPE_OSS) {
		close_oss(o);
		return;
	}
	
	snd_pcm_drain(o->ref.handle);
	snd_pcm_close(o->ref.handle);
	free(o);
}

/*
 * Write audio data.
 *
 * \return true if write sucessful, otherwise false
 */
static inline bool linux_write(MGSAudioDev *o, const int16_t *buf,
		uint32_t samples) {
	if (o->type == TYPE_OSS) {
		return oss_write(o, buf, samples);
	}

	snd_pcm_sframes_t written;
	while ((written = snd_pcm_writei(o->ref.handle, buf, samples)) < 0) {
		if (written == -EPIPE) {
			fputs("warning: ALSA audio device buffer underrun\n",
				stderr);
			snd_pcm_prepare(o->ref.handle);
		} else {
			fprintf(stderr,
				"warning: %s\n", snd_strerror(written));
			break;
		}
	}

	return (written == (snd_pcm_sframes_t) samples);
}
