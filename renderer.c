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
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "renderer/generator.h"
#include "audiodev.h"
#include "wavfile.h"

#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2

static int16_t audio_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Produce audio for the given SGSProgram, optionally sending it
 * to a given audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool produce_audio(SGSProgram *prg, uint32_t srate,
		SGSAudioDev *ad, SGSWAVFile *wf) {
	SGSGenerator *gen = SGS_create_generator(prg, srate);
	size_t len;
	bool error = false;
	bool run;
	do {
		run = SGS_generator_run(gen, audio_buf, BUF_SAMPLES, &len);
		if (ad && !SGS_audiodev_write(ad, audio_buf, len)) {
			error = true;
			SGS_error(NULL, "audio device write failed");
		}
		if (wf && !SGS_wavfile_write(wf, audio_buf, len)) {
			error = true;
			SGS_error(NULL, "WAV file write failed");
		}
	} while (run);
	SGS_destroy_generator(gen);
	return !error;
}

/*
 * Run the given program through the audio generator until completion.
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SGS_render(SGSProgram *prg, uint32_t srate,
		bool use_audiodev, const char *wav_path) {
	SGSAudioDev *ad = NULL;
	uint32_t ad_srate = srate;
	SGSWAVFile *wf = NULL;
	bool status = true;
	if (use_audiodev) {
		ad = SGS_open_audiodev(NUM_CHANNELS, &ad_srate);
		if (!ad) goto CLEANUP;
	}
	if (wav_path) {
		wf = SGS_create_wavfile(wav_path, NUM_CHANNELS, srate);
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
		SGS_close_audiodev(ad);
	}
	if (wf) {
		status = status && (SGS_close_wavfile(wf) == 0);
	}
	return status;
}
