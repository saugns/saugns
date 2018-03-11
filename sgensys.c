/* sgensys: Main module / Command-line interface.
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

#include "program.h"
#include "generator.h"
#include "audiodev.h"
#include "wavfile.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 44100

static int16_t audio_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Produce audio for the given SGSProgram, optionally sending it
 * to a given audio device and/or WAV file.
 *
 * \return true, or false on error
 */
static bool produce_audio(struct SGSProgram *prg,
		SGSAudioDev *ad, SGSWAVFile *wf, uint32_t srate) {
	SGSGenerator *gen = SGS_create_generator(prg, srate);
	size_t len;
	bool error = false;
	bool run;
	do {
		run = SGS_generator_run(gen, audio_buf, BUF_SAMPLES, &len);
		if (ad && !SGS_audiodev_write(ad, audio_buf, len)) {
			error = true;
			fputs("error: audio device write failed\n", stderr);
		}
		if (wf && !SGS_wavfile_write(wf, audio_buf, len)) {
			error = true;
			fputs("error: WAV file write failed\n", stderr);
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
 * Return true if signal generated and sent to any output(s) without
 * error, false if any error occurred.
 */
static bool run_program(struct SGSProgram *prg,
		bool use_audiodev, const char *wav_path, uint32_t srate) {
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
		fputs("warning: generating audio twice, using a different sample rate for each output\n", stderr);
		status = produce_audio(prg, ad, NULL, ad_srate);
		status = status && produce_audio(prg, NULL, wf, srate);
	} else {
		status = produce_audio(prg, ad, wf, ad_srate);
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

/*
 * Print command line usage instructions.
 */
static void print_usage(void) {
	puts(
"Usage: sgensys [-a|-m] [-r srate] [-o wavfile] scriptfile\n"
"       sgensys [-c] scriptfile\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default 44100);\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -c \tCheck script only, reporting any errors.\n"
"  -h \tPrint this message."
	);
}

/*
 * Read a positive integer from the given string.
 *
 * \return positive value or -1 if invalid
 */
static int32_t get_piarg(const char *str) {
	char *endp;
	int32_t i;
	errno = 0;
	i = strtol(str, &endp, 10);
	if (errno || i <= 0 || endp == str || *endp) return -1;
	return i;
}

/*
 * Command line argument flags.
 */
enum {
	ARG_FULL_RUN = 1<<0, /* identifies any non-compile-only flags */
	ARG_ENABLE_AUDIO_DEV = 1<<1,
	ARG_DISABLE_AUDIO_DEV = 1<<2,
	ARG_ONLY_COMPILE = 1<<3,
};

/*
 * Parse command line arguments.
 *
 * Print usage instructions if either requested or args invalid.
 *
 * \return true if args valid, false if not
 */
static bool parse_args(int argc, char **argv, uint32_t *flags,
		const char **script_path, const char **wav_path,
		uint32_t *srate) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!*script_path) USAGE: {
				print_usage();
				return false;
			}
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			if (*script_path) goto USAGE;
			*script_path = arg;
			continue;
		}
		while (*++arg) {
			if (*arg == 'a') {
				if (*flags & (ARG_DISABLE_AUDIO_DEV |
						ARG_ONLY_COMPILE))
					goto USAGE;
				*flags |= ARG_FULL_RUN |
					ARG_ENABLE_AUDIO_DEV;
			} else if (*arg == 'm') {
				if (*flags & (ARG_ENABLE_AUDIO_DEV |
						ARG_ONLY_COMPILE))
					goto USAGE;
				*flags |= ARG_FULL_RUN |
					 ARG_DISABLE_AUDIO_DEV;
			} else if (!strcmp(arg, "r")) {
				int i;
				if (*flags & ARG_ONLY_COMPILE)
					goto USAGE;
				*flags |= ARG_FULL_RUN;
				--argc;
				++argv;
				if (argc < 1) goto USAGE;
				arg = *argv;
				i = get_piarg(arg);
				if (i < 0) goto USAGE;
				*srate = i;
				break;
			} else if (!strcmp(arg, "o")) {
				if (*flags & ARG_ONLY_COMPILE)
					goto USAGE;
				*flags |= ARG_FULL_RUN;
				--argc;
				++argv;
				if (argc < 1) goto USAGE;
				arg = *argv;
				*wav_path = arg;
				break;
			} else if (*arg == 'c') {
				if (*flags & ARG_FULL_RUN)
					goto USAGE;
				*flags |= ARG_ONLY_COMPILE;
			} else
				goto USAGE;
		}
	}
	return true;
}

/**
 * Main function.
 */
int main(int argc, char **argv) {
	const char *script_path = NULL, *wav_path = NULL;
	uint32_t options = 0;
	bool use_audiodev;
	SGSProgram *prg;
	uint32_t srate = DEFAULT_SRATE;
	bool error = false;

	if (!parse_args(argc, argv, &options, &script_path, &wav_path,
			&srate))
		goto RETURN;
	if (!(prg = SGS_open_program(script_path))) {
		fprintf(stderr, "error: couldn't open script file \"%s\" for reading\n",
				script_path);
		error = true;
		goto RETURN;
	}
	if ((options & ARG_ONLY_COMPILE) != 0)
		goto RETURN;

	use_audiodev = (wav_path ?
			((options & ARG_ENABLE_AUDIO_DEV) != 0) :
			((options & ARG_DISABLE_AUDIO_DEV) == 0));
	error = !run_program(prg, use_audiodev, wav_path, srate);
	SGS_close_program(prg);

RETURN:
	return (error) ? 1 : 0;
}
