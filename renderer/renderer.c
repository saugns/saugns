/* mgensys: Audio program renderer module.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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

#include "../mgensys.h"
#include "audiodev.h"
#include "wavfile.h"
#include "../math.h"
#include <stdlib.h>

#define BUF_TIME_MS  256
#define NUM_CHANNELS 2

typedef struct MGS_Renderer {
	MGS_AudioDev *ad;
	MGS_WAVFile *wf;
	uint32_t ad_srate;
	int16_t *buf;
	size_t buf_len;
	size_t ch_len;
} MGS_Renderer;

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool MGS_init_Renderer(MGS_Renderer *restrict o, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path) {
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (MGS_Renderer){0};
	if (use_audiodev) {
		o->ad = MGS_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!o->ad)
			return false;
		o->ad_srate = ad_srate;
	}
	if (wav_path != NULL) {
		o->wf = MGS_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!o->wf)
			return false;
	}
	if (ad_srate != srate) {
		if (!o->wf || ad_srate > srate)
			max_srate = ad_srate;
	}

	o->ch_len = MGS_MS_IN_SAMPLES(BUF_TIME_MS, max_srate);
	o->buf_len = o->ch_len * NUM_CHANNELS;
	o->buf = calloc(o->buf_len, sizeof(int16_t));
	if (!o->buf)
		return false;
	return true;
}

/*
 * \return true unless error occurred
 */
static bool MGS_fini_Renderer(MGS_Renderer *restrict o) {
	free(o->buf);
	if (o->ad != NULL) MGS_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (MGS_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool MGS_Renderer_run(MGS_Renderer *restrict o,
		const MGS_Program *restrict prg, uint32_t srate,
		bool use_audiodev, bool use_wavfile) {
	MGS_Generator *gen = MGS_create_Generator(prg, srate);
	uint32_t len;
	bool error = false;
	bool run;
	use_audiodev = use_audiodev && (o->ad != NULL);
	use_wavfile = use_wavfile && (o->wf != NULL);
	do {
		run = MGS_Generator_run(gen, o->buf, o->ch_len, &len);
		if (use_audiodev && !MGS_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			MGS_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !MGS_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			MGS_error(NULL, "WAV file write failed");
		}
	} while (run);
	MGS_destroy_Generator(gen);
	return !error;
}

/*
 * Run the listed programs through the audio generator until completion,
 * ignoring NULL entries.
 *
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool MGS_render(const MGS_PtrArr *restrict prg_objs, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	MGS_Renderer re;
	bool status = true;
	if (!MGS_init_Renderer(&re, srate, use_audiodev, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	if (re.ad != NULL && re.wf != NULL && (re.ad_srate != srate)) {
		MGS_warning(NULL,
"generating audio twice, using different sample rates");
		const MGS_Program **prgs =
			(const MGS_Program**) MGS_PtrArr_ITEMS(prg_objs);
		for (size_t i = 0; i < prg_objs->count; ++i) {
			if (!MGS_Renderer_run(&re, prgs[i], re.ad_srate,
						true, false))
				status = false;
			if (!MGS_Renderer_run(&re, prgs[i], srate,
						false, true))
				status = false;
		}
	} else {
		if (re.ad != NULL) srate = re.ad_srate;
		const MGS_Program **prgs =
			(const MGS_Program**) MGS_PtrArr_ITEMS(prg_objs);
		for (size_t i = 0; i < prg_objs->count; ++i) {
			if (!MGS_Renderer_run(&re, prgs[i], srate,
						true, true))
				status = false;
		}
	}

CLEANUP:
	if (!MGS_fini_Renderer(&re))
		status = false;
	return status;
}
