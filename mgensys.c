/* mgensys: Main module / Command-line interface
 * Copyright (c) 2011, 2017-2018, 2020 Joel K. Pettersson
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

#include "mgensys.h"
#include <stdint.h>
#include <stdbool.h>
#include "audiodev.h"
#include <stdio.h>
#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 96000

static int16_t audio_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Produce audio for the given MGSProgram, optionally sending it
 * to a given audio device.
 *
 * \return true, or false on error
 */
static bool produce_audio(struct MGSProgram *prg,
    MGSAudioDev *ad, uint32_t srate) {
  MGSGenerator *gen = MGSGenerator_create(srate, prg);
  bool error = false;
  bool run;
  do {
    run = MGSGenerator_run(gen, audio_buf, BUF_SAMPLES);
    if (ad && !MGS_audiodev_write(ad, audio_buf, BUF_SAMPLES)) {
      error = true;
      fputs("error: audio device write failed\n", stderr);
    }
  } while (run);
  MGSGenerator_destroy(gen);
  return !error;
}

/*
 * Run the given program through the audio generator until completion.
 * The output may be written to the audio device.
 *
 * \return true if signal generated and sent to any output(s) without
 * error, false if any error occurred.
 */
static bool run_program(struct MGSProgram *prg,
    bool use_audiodev, uint32_t srate) {
  MGSAudioDev *ad = NULL;
  uint32_t ad_srate = srate;
  bool status = true;
  if (use_audiodev) {
    ad = MGS_open_audiodev(NUM_CHANNELS, &ad_srate);
    if (!ad) goto CLEANUP;
  }
  status = produce_audio(prg, ad, ad_srate);

CLEANUP:
  if (ad) {
    MGS_close_audiodev(ad);
  }
  return status;
}

int main(int argc, char **argv) {
  MGSProgram *prg;
  uint srate = DEFAULT_SRATE;
  if (argc < 2) {
    puts("usage: mgensys scriptfile");
    return 0;
  }
  if (!(prg = MGSProgram_create(argv[1]))) {
    printf("error: couldn't open script file \"%s\"\n", argv[1]);
    return 1;
  }
  bool ok = run_program(prg, true, srate);
  MGSProgram_destroy(prg);
  return !ok;
}
