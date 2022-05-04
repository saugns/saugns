/* saugns: Audio program player module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
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

#include "../saugns.h"
#include "../renderer/generator.h"
#include "audiodev.h"
#include "wavfile.h"
#include "../math.h"
#include <stdlib.h>
#include <stdio.h>

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1

typedef struct SAU_Output {
	SAU_AudioDev *ad;
	SAU_WAVFile *wf;
	int16_t *buf, *ad_buf;
	uint32_t srate, ad_srate;
	uint32_t options;
	uint32_t ch_count;
	uint32_t ch_len, ad_ch_len;
} SAU_Output;

/*
 * Set up use of system audio device, raw audio to stdout,
 * and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SAU_init_Output(SAU_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool split_gen = false;
	bool use_audiodev = (wav_path) ?
		((options & SAU_OPT_SYSAU_ENABLE) != 0) :
		((options & SAU_OPT_SYSAU_DISABLE) == 0);
	bool use_stdout = (options & SAU_OPT_AUDIO_STDOUT);
	uint32_t ad_srate = srate;
	*o = (SAU_Output){0};
	o->options = options;
	o->ch_count = (options & SAU_OPT_AUDIO_MONO) ? 1 : 2;
	if ((options & SAU_OPT_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SAU_open_AudioDev(o->ch_count, &ad_srate);
		if (!o->ad)
			return false;
	}
	if (wav_path) {
		o->wf = SAU_create_WAVFile(wav_path, o->ch_count, srate);
		if (!o->wf)
			return false;
	}
	if (ad_srate != srate) {
		if (use_stdout || o->wf)
			split_gen = true;
		else
			srate = ad_srate;
	}

	o->srate = srate;
	o->ch_len = SAU_ms_in_samples(BUF_TIME_MS, srate);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf = calloc(o->ch_len * o->ch_count, sizeof(int16_t));
	if (!o->buf)
		return false;
	if (split_gen) {
		/*
		 * For alternating buffered generation with non-ad_* version.
		 */
		o->ad_srate = ad_srate;
		o->ad_ch_len = SAU_ms_in_samples(BUF_TIME_MS, ad_srate);
		if (o->ad_ch_len < CH_MIN_LEN) o->ad_ch_len = CH_MIN_LEN;
		o->ad_buf = calloc(o->ad_ch_len * o->ch_count, sizeof(int16_t));
		if (!o->ad_buf)
			return false;
	}
	return true;
}

/*
 * \return true unless error occurred
 */
static bool SAU_fini_Output(SAU_Output *restrict o) {
	free(o->buf);
	free(o->ad_buf);
	if (o->ad != NULL) SAU_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (SAU_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Write \p samples from \p buf to raw file. Channels are assumed
 * to be interleaved in the buffer, and the buffer of length
 * (channels * samples).
 *
 * \return true if write successful
 */
static bool raw_audio_write(FILE *restrict f, uint32_t channels,
		const int16_t *restrict buf, uint32_t samples) {
	return fwrite(buf, channels * sizeof(int16_t), samples, f) == samples;
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool SAU_Output_run(SAU_Output *restrict o,
		const SAU_Program *restrict prg) {
	bool use_stereo = !(o->options & SAU_OPT_AUDIO_MONO);
	bool use_stdout = (o->options & SAU_OPT_AUDIO_STDOUT);
	bool split_gen = o->ad_buf;
	bool run = !(o->options & SAU_OPT_MODE_CHECK);
	bool error = false;
	SAU_Generator *gen = NULL, *ad_gen = NULL;
	if (!(gen = SAU_create_Generator(prg, o->srate)))
		return false;
	if (split_gen && !(ad_gen = SAU_create_Generator(prg, o->ad_srate))) {
		error = true;
		goto ERROR;
	}
	if (!split_gen) {
		while (run) {
			size_t len;
			run = SAU_Generator_run(gen, o->buf,
					o->ch_len, use_stereo, &len);
			if (o->ad && !SAU_AudioDev_write(o->ad, o->buf, len)) {
				error = true;
				SAU_error(NULL, "system audio write failed");
			}
			if (use_stdout && !raw_audio_write(stdout,
						o->ch_count, o->buf, len)) {
				error = true;
				SAU_error(NULL, "audio to stdout write failed");
			}
			if (o->wf && !SAU_WAVFile_write(o->wf, o->buf, len)) {
				error = true;
				SAU_error(NULL, "WAV file write failed");
			}
		}
	} else {
		bool ad_run = run;
		while (run || ad_run) {
			size_t len, ad_len;
			run = SAU_Generator_run(gen, o->buf,
					o->ch_len, use_stereo, &len);
			ad_run = SAU_Generator_run(ad_gen, o->ad_buf,
					o->ad_ch_len, use_stereo, &ad_len);
			if (!SAU_AudioDev_write(o->ad, o->ad_buf, ad_len)) {
				error = true;
				SAU_error(NULL, "system audio write failed");
			}
			if (use_stdout && !raw_audio_write(stdout,
						o->ch_count, o->buf, len)) {
				error = true;
				SAU_error(NULL, "audio to stdout write failed");
			}
			if (o->wf && !SAU_WAVFile_write(o->wf, o->buf, len)) {
				error = true;
				SAU_error(NULL, "WAV file write failed");
			}
		}
	}
ERROR:
	SAU_destroy_Generator(gen);
	SAU_destroy_Generator(ad_gen);
	return !error;
}

/**
 * Run the listed scripts through the audio generator until completion,
 * ignoring NULL entries.
 *
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SAU_play(const SAU_PtrArr *restrict script_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!script_objs->count)
		return true;

	SAU_Output out;
	bool status = true;
	if (!SAU_init_Output(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen = out.ad_buf;
	if (split_gen) SAU_warning(NULL,
			"generating audio twice, using different sample rates");
	SAU_Script **scripts = (SAU_Script**) SAU_PtrArr_ITEMS(script_objs);
	for (size_t i = 0; i < script_objs->count; ++i) {
		SAU_Script *script = scripts[i];
		const SAU_Program *prg;
		if (!script) continue;
		if (!script->program) SAU_build_Program(script);
		if (!(prg = script->program)) continue;
		if ((options & SAU_OPT_PRINT_INFO) != 0)
			SAU_Program_print_info(prg);
		if (!SAU_Output_run(&out, prg))
			status = false;
	}

CLEANUP:
	if (!SAU_fini_Output(&out))
		status = false;
	return status;
}
