/* sgensys: Audio program renderer module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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

#include "sgensys.h"
#include "renderer/generator.h"
#include "audiodev.h"
#include "wavfile.h"

#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2

static int16_t audio_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Produce audio for the given SGS_Program, optionally sending it
 * to a given audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool produce_audio(SGS_Program *restrict prg, uint32_t srate,
		SGS_AudioDev *restrict ad, SGS_WAVFile *restrict wf) {
	SGS_Generator *gen = SGS_create_Generator(prg, srate);
	size_t len;
	bool error = false;
	bool run;
	do {
		run = SGS_Generator_run(gen, audio_buf, BUF_SAMPLES, &len);
		if (ad && !SGS_AudioDev_write(ad, audio_buf, len)) {
			error = true;
			SGS_error(NULL, "audio device write failed");
		}
		if (wf && !SGS_WAVFile_write(wf, audio_buf, len)) {
			error = true;
			SGS_error(NULL, "WAV file write failed");
		}
	} while (run);
	SGS_destroy_Generator(gen);
	return !error;
}

/*
 * Run the given program through the audio generator until completion.
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SGS_render(SGS_Program *restrict prg, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path) {
	SGS_AudioDev *ad = NULL;
	uint32_t ad_srate = srate;
	SGS_WAVFile *wf = NULL;
	bool status = true;
	if (use_audiodev) {
		ad = SGS_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!ad) goto CLEANUP;
	}
	if (wav_path) {
		wf = SGS_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!wf) goto CLEANUP;
	}

	if (ad && wf && (ad_srate != srate)) {
		SGS_warning(NULL,
			"generating audio twice, using different sample rates");
		status = produce_audio(prg, ad_srate, ad, NULL);
		status = status && produce_audio(prg, srate, NULL, wf);
	} else {
		status = produce_audio(prg, ad_srate, ad, wf);
	}

CLEANUP:
	if (ad) {
		SGS_close_AudioDev(ad);
	}
	if (wf) {
		status = status && (SGS_close_WAVFile(wf) == 0);
	}
	return status;
}
