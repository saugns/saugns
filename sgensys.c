/* sgensys: Command-line interface & main module.
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

#include "parser.h"
#include "builder.h"
#if OLD_SOUNDGEN
# include "generator.h"
#else
# include "interpreter.h"
# include "renderer.h"
#endif
#include "audiodev.h"
#include "wavfile.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 44100

#if OLD_SOUNDGEN /* SGSProgram -> SGSGenerator */

static int16_t sound_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Produce sound for the given program, optionally sending it
 * to a given audio device and/or WAV file.
 *
 * Return true if no error occurred.
 */
static bool produce_sound(SGSProgram_t prg,
		SGSAudioDev_t ad, SGSWAVFile_t wf, uint32_t srate) {
	SGSGenerator_t gen = SGS_create_generator(prg, srate);
	size_t len;
	bool error = false;
	bool run;
	do {
		run = SGS_generator_run(gen, sound_buf, BUF_SAMPLES, &len);
		if (ad && !SGS_audiodev_write(ad, sound_buf, len)) {
			error = true;
			fputs("error: audio device write failed\n", stderr);
		}
		if (wf && !SGS_wavfile_write(wf, sound_buf, len)) {
			error = true;
			fputs("error: WAV file write failed\n", stderr);
		}
	} while (run);
	SGS_destroy_generator(gen);
	return !error;
}

/*
 * Run the given program through the sound generator until completion.
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * Return true if signal generated and sent to any output(s) without
 * error, false if any error occurred.
 */
static bool run_program(SGSProgram_t prg,
		bool use_audiodev, const char *wav_path, uint32_t srate) {
	SGSAudioDev_t ad = NULL;
	uint32_t ad_srate = srate;
	SGSWAVFile_t wf = NULL;
	if (use_audiodev) {
		ad = SGS_open_audiodev(NUM_CHANNELS, &ad_srate);
		if (!ad) {
			return false;
		}
	}
	if (wav_path) {
		wf = SGS_create_wavfile(wav_path, NUM_CHANNELS, srate);
		if (!wf) {
			if (ad) SGS_close_audiodev(ad);
			return false;
		}
	}

	bool status;
	if (ad && wf && (ad_srate != srate)) {
		fputs("warning: generating sound twice, using a different sample rate for each output\n", stderr);
		status = produce_sound(prg, ad, NULL, ad_srate);
		status = status && produce_sound(prg, NULL, wf, srate);
	} else {
		status = produce_sound(prg, ad, wf, ad_srate);
	}

	if (ad) {
		SGS_close_audiodev(ad);
	}
	if (wf) {
		status = status && (SGS_close_wavfile(wf) == 0);
	}
	return status;
}

#else /* SGSProgram -> SGSInterpreter; SGSResult -> SGSRenderer */

static int16_t audio_buf[BUF_SAMPLES * NUM_CHANNELS];

/*
 * Render audio for the given program result, optionally sending it
 * to a given audio device and/or WAV file.
 *
 * Return true if no error occurred.
 */
static bool produce_audio(SGSResult_t res,
		SGSAudioDev_t ad, SGSWAVFile_t wf, uint32_t srate) {
	SGSRenderer_t ar = SGS_create_renderer(res, srate);
	size_t len;
	bool error = false;
	bool run;
	do {
		run = SGS_renderer_run(ar, audio_buf, BUF_SAMPLES, &len);
		if (ad && !SGS_audiodev_write(ad, audio_buf, len)) {
			error = true;
			fputs("error: audio device write failed\n", stderr);
		}
		if (wf && !SGS_wavfile_write(wf, audio_buf, len)) {
			error = true;
			fputs("error: WAV file write failed\n", stderr);
		}
	} while (run);
	SGS_destroy_renderer(ar);
	return !error;
}

static bool run_program(SGSProgram_t prg,
		bool use_audiodev, const char *wav_path, uint32_t srate) {
	SGSInterpreter_t intrp = SGS_create_interpreter();
	SGSResult_t res = SGS_interpreter_run(intrp, prg);

	SGSAudioDev_t ad = NULL;
	uint32_t ad_srate = srate;
	SGSWAVFile_t wf = NULL;
	if (use_audiodev) {
		ad = SGS_open_audiodev(NUM_CHANNELS, &ad_srate);
		if (!ad) {
			return false;
		}
	}
	if (wav_path) {
		wf = SGS_create_wavfile(wav_path, NUM_CHANNELS, srate);
		if (!wf) {
			if (ad) SGS_close_audiodev(ad);
			return false;
		}
	}

	bool status;
	if (ad && wf && (ad_srate != srate)) {
		fputs("warning: generating sound twice, using a different sample rate for each output\n", stderr);
		status = produce_audio(res, ad, NULL, ad_srate);
		status = status && produce_audio(res, NULL, wf, srate);
	} else {
		status = produce_audio(res, ad, wf, ad_srate);
	}

	if (ad) {
		SGS_close_audiodev(ad);
	}
	if (wf) {
		status = status && (SGS_close_wavfile(wf) == 0);
	}
	return status;
}

#endif

/*
 * Print command line usage instructions.
 */
static void print_usage(void) {
	puts(
"Usage: sgensys [[-a|-m] [-r srate] [-o wavfile]|-p] scriptfile\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default 44100); if the audio device does not\n"
"     \tsupport the rate requested, a warning will be printed along with\n"
"     \tthe rate used for the audio device instead.\n"
"  -o \tWrite a 16-bit PCM WAV file; by default, this disables audio device\n"
"     \toutput.\n"
"  -p \tStop after parsing the script, upon success or failure; mutually\n"
"     \texclusive with all other options.\n"
"  -h \tPrint this message."
	);
}

/*
 * Read a positive integer from the given string. Returns the integer,
 * or -1 on error.
 */
static int get_piarg(const char *str) {
	char *endp;
	int i;
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
	ARG_ONLY_PARSE = 1<<3,
};

/*
 * Parse command line arguments.
 *
 * Returns 0 if the arguments are valid and include a script to run,
 * otherwise print usage instructions and returns 1.
 */
static int parse_args(int argc, char **argv, uint32_t *flags,
		const char **script_path, const char **wav_path,
		uint32_t *srate) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!*script_path) USAGE: {
				print_usage();
				return 1;
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
						ARG_ONLY_PARSE))
					goto USAGE;
				*flags |= ARG_FULL_RUN |
					ARG_ENABLE_AUDIO_DEV;
			} else if (*arg == 'm') {
				if (*flags & (ARG_ENABLE_AUDIO_DEV |
						ARG_ONLY_PARSE))
					goto USAGE;
				*flags |= ARG_FULL_RUN |
					 ARG_DISABLE_AUDIO_DEV;
			} else if (!strcmp(arg, "r")) {
				int i;
				if (*flags & ARG_ONLY_PARSE)
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
				if (*flags & ARG_ONLY_PARSE)
					goto USAGE;
				*flags |= ARG_FULL_RUN;
				--argc;
				++argv;
				if (argc < 1) goto USAGE;
				arg = *argv;
				*wav_path = arg;
				break;
			} else if (*arg == 'p') {
				if (*flags & ARG_FULL_RUN)
					goto USAGE;
				*flags |= ARG_ONLY_PARSE;
			} else
				goto USAGE;
		}
	}
	return 0;
}

/**
 * Main function.
 */
int main(int argc, char **argv) {
	const char *script_path = NULL, *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = DEFAULT_SRATE;
	bool error = false;

	if (parse_args(argc, argv, &options, &script_path, &wav_path,
			&srate) != 0)
		goto DONE;

	SGSParser_t parser = SGS_create_parser();
	SGSParseResult_t parse_res = SGS_parser_process(parser, script_path);
	if (!parse_res) {
		error = true;
		goto DONE;
	}
	if (options & ARG_ONLY_PARSE) {
		SGS_destroy_parser(parser);
		goto DONE;
	}

	SGSProgram_t prg = SGS_build_program(parse_res);
	SGS_destroy_parser(parser);
	bool use_audiodev = (wav_path ?
			(options & ARG_ENABLE_AUDIO_DEV) :
			!(options & ARG_DISABLE_AUDIO_DEV));
	error = !run_program(prg, use_audiodev, wav_path, srate);
	SGS_destroy_program(prg);

DONE:
	return error;
}
