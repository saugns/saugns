/* sgensys: Audio program player module.
 * Copyright (c) 2011-2013, 2017-2021 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "../sgensys.h"
#include "../renderer/generator.h"
#include "audiodev.h"
#include "wavfile.h"
#include "../math.h"
#include <stdlib.h>

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1
#define NUM_CHANNELS 2

typedef struct SGS_Output {
	SGS_AudioDev *ad;
	SGS_WAVFile *wf;
	int16_t *buf;
	uint32_t ad_srate;
	uint32_t options;
	size_t buf_len;
	size_t ch_len;
} SGS_Output;

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SGS_init_Output(SGS_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & SGS_OPT_AUDIO_ENABLE) != 0) :
		((options & SGS_OPT_AUDIO_DISABLE) == 0);
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (SGS_Output){0};
	o->options = options;
	if ((options & SGS_OPT_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SGS_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!o->ad)
			return false;
		o->ad_srate = ad_srate;
	}
	if (wav_path != NULL) {
		o->wf = SGS_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!o->wf)
			return false;
	}
	if (ad_srate != srate) {
		if (!o->wf || ad_srate > srate)
			max_srate = ad_srate;
	}

	o->ch_len = SGS_ms_in_samples(BUF_TIME_MS, max_srate);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf_len = o->ch_len * NUM_CHANNELS;
	o->buf = calloc(o->buf_len, sizeof(int16_t));
	if (!o->buf)
		return false;
	return true;
}

/*
 * \return true unless error occurred
 */
static bool SGS_fini_Output(SGS_Output *restrict o) {
	free(o->buf);
	if (o->ad != NULL) SGS_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (SGS_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool SGS_Output_run(SGS_Output *restrict o,
		const SGS_Program *restrict prg, uint32_t srate,
		bool use_audiodev, bool use_wavfile) {
	SGS_Generator *gen = SGS_create_Generator(prg, srate);
	if (!gen)
		return false;
	size_t len;
	bool error = false;
	bool run = !(o->options & SGS_OPT_MODE_CHECK);
	use_audiodev = use_audiodev && (o->ad != NULL);
	use_wavfile = use_wavfile && (o->wf != NULL);
	while (run) {
		run = SGS_Generator_run(gen, o->buf, o->ch_len, &len);
		if (use_audiodev && !SGS_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			SGS_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !SGS_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			SGS_error(NULL, "WAV file write failed");
		}
	}
	SGS_destroy_Generator(gen);
	return !error;
}

/**
 * Run the listed programs through the audio generator until completion,
 * ignoring NULL entries.
 *
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SGS_play(const SGS_PtrList *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	SGS_Output out;
	bool status = true;
	if (!SGS_init_Output(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen;
	if (out.ad != NULL && out.wf != NULL && (out.ad_srate != srate)) {
		split_gen = true;
		SGS_warning(NULL,
"generating audio twice, using different sample rates");
	} else {
		split_gen = false;
		if (out.ad != NULL) srate = out.ad_srate;
	}
	const SGS_Program **prgs =
		(const SGS_Program**) SGS_PtrList_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const SGS_Program *prg = prgs[i];
		if (!prg) continue;
		if ((options & SGS_OPT_PRINT_INFO) != 0)
			SGS_Program_print_info(prg);
		if (split_gen) {
			if (!SGS_Output_run(&out, prg, out.ad_srate,
						true, false))
				status = false;
			if (!SGS_Output_run(&out, prg, srate,
						false, true))
				status = false;
		} else {
			if (!SGS_Output_run(&out, prg, srate,
						true, true))
				status = false;
		}
	}

CLEANUP:
	if (!SGS_fini_Output(&out))
		status = false;
	return status;
}
