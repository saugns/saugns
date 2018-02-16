/* sgensys: Main module and command-line interface.
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
#include "parser.h"
#include "interpreter.h"
#include "renderer.h"
#include "audiodev.h"
#include "wavfile.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 44100

/*
 * Parse the given script and go through the steps to return
 * the result.
 */
static struct SGSProgram *read_script(const char *filename) {
	struct SGSParser *parser = SGS_create_parser();
	struct SGSParseList *result = SGS_parser_parse(parser, filename);
	SGS_destroy_parser(parser);
	return SGS_build_program(result);
}

/*
 * Run the given program through the sound generator until completion.
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 */
static bool run_program(struct SGSProgram *prg, uint8_t audio_device,
		const char *wav_path, uint32_t srate) {
	int16_t buf[BUF_SAMPLES * NUM_CHANNELS];
	size_t len;
	SGSAudioDev *ad = NULL;
	SGSWAVFile *wf = NULL;
	SGSInterpreter_t in;
	SGSRenderer *ar;
	if (wav_path) {
		wf = SGS_begin_wav_file(wav_path, NUM_CHANNELS, srate);
		if (!wf) {
			fprintf(stderr, "error: couldn't open wav file \"%s\"\n",
					wav_path);
			return false;
		}
	}
	if (audio_device) {
		ad = SGS_open_audio_dev(NUM_CHANNELS, srate);
		if (!ad) {
			if (wf) SGS_end_wav_file(wf);
			return false;
		}
	}
	in = SGS_create_interpreter();
	ar = SGS_create_renderer(srate, SGS_interpreter_run(in, prg));

	bool run;
	do {
		run = SGS_renderer_run(ar, buf, BUF_SAMPLES, &len);
		if (ad && SGS_audio_dev_write(ad, buf, len) != 0)
			fputs("warning: audio device write failed\n", stderr);
		if (wf && SGS_wav_file_write(wf, buf, len) == false)
			fputs("warning: WAV file write failed\n", stderr);
	} while (run);

	if (ad) SGS_close_audio_dev(ad);
	if (wf) SGS_end_wav_file(wf);
	SGS_destroy_renderer(ar);
	SGS_destroy_interpreter(in);
	return true;
}

/*
 * Print command line usage instructions.
 */
static void print_usage(void) {
	puts(
"Usage: sgensys [[-a|-m] [-r srate] [-o wavfile]|-c] scriptfile\n"
"If no options are given, audio output is sent to the audio device.\n"
"\n"
"  -a \tAlways enable audio device output.\n"
"  -m \tMute; always disable audio device output.\n"
"  -r \tSet sample rate in Hz (default 44100); if audio device output is\n"
"     \tenabled and the exact rate is not supported by the device, a\n"
"     \twarning will be printed along with the rate used instead.\n"
"  -o \tWrite output to a 16-bit PCM WAV file; by default, this disables\n"
"     \taudio device output.\n"
"  -c \tMerely compile the script, stopping upon success or error; mutually\n"
"     \texclusive with all other options.\n"
"  -h \tPrint this message."
	);
}

/*
 * Read a positive integer from the given string. Returns the integer, or -1
 * on error.
 */
static int get_piarg(const char *str) {
	char *endp;
	int i;
	errno = 0;
	i = strtol(str, &endp, 10);
	if (errno || i < 0 || endp == str || *endp) return -1;
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
 * Parse command line arguments. Returns 0 if the arguments are valid and
 * include a script to run, otherwise prints usage instructions and returns 1.
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
	return 0;
}

int main(int argc, char **argv) {
	const char *script_path = NULL, *wav_path = NULL;
	uint32_t options = 0;
	uint8_t audio_dev;
	struct SGSProgram *prg;
	uint32_t srate = DEFAULT_SRATE;
	if (parse_args(argc, argv, &options, &script_path, &wav_path,
			&srate) != 0)
		return 0;
	if (!(prg = read_script(script_path))) {
		fprintf(stderr, "error: couldn't open script file \"%s\"\n",
				script_path);
		return 1;
	}
	if (options & ARG_ONLY_COMPILE)
		return 0;
	audio_dev = (wav_path ?
			(options & ARG_ENABLE_AUDIO_DEV) :
			!(options & ARG_DISABLE_AUDIO_DEV));
	int run_status = !run_program(prg, audio_dev, wav_path, srate);
	SGS_destroy_program(prg);
	return run_status;
}
