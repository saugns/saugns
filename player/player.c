/* mgensys: Audio program player module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "../mgensys.h"
#include "../program.h"
#include "audiodev.h"
#include "sndfile.h"
#include "../math.h"
#include <stdlib.h>
#include <stdio.h>

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1

typedef struct MGS_Output {
	MGS_AudioDev *ad;
	MGS_SndFile *sf;
	int16_t *buf, *ad_buf;
	uint32_t srate, ad_srate;
	uint32_t options;
	uint32_t ch_count;
	uint32_t ch_len, ad_ch_len;
} MGS_Output;

/*
 * Set up use of system audio device, raw audio to stdout,
 * and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool MGS_init_Output(MGS_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool split_gen = false;
	bool use_audiodev = (wav_path) ?
		((options & MGS_OPT_SYMGS_ENABLE) != 0) :
		((options & MGS_OPT_SYMGS_DISABLE) == 0);
	bool use_stdout = (options & MGS_OPT_AUDIO_STDOUT);
	uint32_t ad_srate = srate;
	*o = (MGS_Output){0};
	o->options = options;
	o->ch_count = (options & MGS_OPT_AUDIO_MONO) ? 1 : 2;
	if ((options & MGS_OPT_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = MGS_open_AudioDev(o->ch_count, &ad_srate);
		if (!o->ad)
			return false;
	}
	if (wav_path) {
		if (options & MGS_OPT_AUFILE_STDOUT)
			o->sf = MGS_create_SndFile(NULL, MGS_SNDFILE_AU,
					o->ch_count, srate);
		else
			o->sf = MGS_create_SndFile(wav_path, MGS_SNDFILE_WAV,
					o->ch_count, srate);
		if (!o->sf)
			return false;
	}
	if (ad_srate != srate) {
		if (use_stdout || o->sf)
			split_gen = true;
		else
			srate = ad_srate;
	}

	o->srate = srate;
	o->ch_len = MGS_ms_in_samples(BUF_TIME_MS, srate);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf = calloc(o->ch_len * o->ch_count, sizeof(int16_t));
	if (!o->buf)
		return false;
	if (split_gen) {
		/*
		 * For alternating buffered generation with non-ad_* version.
		 */
		o->ad_srate = ad_srate;
		o->ad_ch_len = MGS_ms_in_samples(BUF_TIME_MS, ad_srate);
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
static bool MGS_fini_Output(MGS_Output *restrict o) {
	free(o->buf);
	free(o->ad_buf);
	if (o->ad != NULL) MGS_close_AudioDev(o->ad);
	if (o->sf != NULL)
		return (MGS_close_SndFile(o->sf) == 0);
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
	return samples == fwrite(buf, channels * sizeof(int16_t), samples, f);
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool MGS_Output_run(MGS_Output *restrict o,
		const MGS_Program *restrict prg) {
	bool use_stereo = !(o->options & MGS_OPT_AUDIO_MONO);
	bool use_stdout = (o->options & MGS_OPT_AUDIO_STDOUT);
	bool split_gen = o->ad_buf;
	bool run = !(o->options & MGS_OPT_MODE_CHECK);
	bool error = false;
	MGS_Generator *gen = NULL, *ad_gen = NULL;
	if (!(gen = MGS_create_Generator(prg, o->srate)))
		return false;
	if (split_gen && !(ad_gen = MGS_create_Generator(prg, o->ad_srate))) {
		error = true;
		goto ERROR;
	}
	while (run) {
		int16_t *buf = o->buf, *ad_buf = NULL;
		uint32_t len, ad_len;
		run = MGS_Generator_run(gen, buf, o->ch_len, use_stereo, &len);
		if (split_gen) {
			ad_buf = o->ad_buf;
			run |= MGS_Generator_run(ad_gen, ad_buf, o->ad_ch_len,
					use_stereo, &ad_len);
		} else {
			ad_buf = o->buf;
			ad_len = len;
		}
		if (o->ad && !MGS_AudioDev_write(o->ad, ad_buf, ad_len)) {
			MGS_error(NULL, "system audio write failed");
			error = true;
		}
		if (use_stdout && !raw_audio_write(stdout,
					o->ch_count, buf, len)) {
			MGS_error(NULL, "raw audio stdout write failed");
			error = true;
		}
		if (o->sf && !MGS_SndFile_write(o->sf, buf, len)) {
			MGS_error(NULL, "%s file write failed",
					MGS_SndFile_formats[
					(o->options & MGS_OPT_AUFILE_STDOUT) ?
					MGS_SNDFILE_AU :
					MGS_SNDFILE_WAV]);
			error = true;
		}
	}
ERROR:
	MGS_destroy_Generator(gen);
	MGS_destroy_Generator(ad_gen);
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
bool MGS_play(const MGS_PtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	MGS_Output out;
	bool status = true;
	if (!MGS_init_Output(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen = out.ad_buf;
	if (split_gen) MGS_warning(NULL,
			"generating audio twice, using different sample rates");
	const MGS_Program **prgs =
		(const MGS_Program**) MGS_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const MGS_Program *prg = prgs[i];
		if (!prg) continue;
		if ((options & MGS_OPT_PRINT_INFO) != 0)
			(void)0; //MGS_Program_print_info(prg);
		if ((options & MGS_OPT_PRINT_VERBOSE) != 0)
			MGS_printf((options & MGS_OPT_MODE_CHECK) != 0 ?
					"Checked \"%s\".\n" :
					"Playing \"%s\".\n", prg->name);
		if (!MGS_Output_run(&out, prg))
			status = false;
	}

CLEANUP:
	if (!MGS_fini_Output(&out))
		status = false;
	return status;
}
