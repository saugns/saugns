/* The main function and command-line interface.
 * Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
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
#include "audiodev.h"
#include "wavfile.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define BUF_SAMPLES 1024
#define NUM_CHANNELS 2
#define DEFAULT_SRATE 44100

/*
 * Generate sound for the given program until completion. The output is sent
 * to either none, one, or both of the audio device or a WAV file.
 */
static int run_program(struct SGSProgram *prg, uchar audio_device,
		const char *wav_path, uint srate) {
	short buf[BUF_SAMPLES * NUM_CHANNELS];
	uchar run;
	uint len;
	SGSAudioDev *ad = NULL;
	SGSWAVFile *wf = NULL;
	SGSGenerator *gen;
	if (wav_path) {
		wf = SGS_begin_wav_file(wav_path, NUM_CHANNELS, srate);
		if (!wf) {
			fprintf(stderr, "error: couldn't open wav file \"%s\"\n",
					wav_path);
			return 1;
		}
	}
	if (audio_device) {
		ad = SGS_open_audio_dev(NUM_CHANNELS, srate);
		if (!ad) {
			if (wf) SGS_end_wav_file(wf);
			return 1;
		}
	}
	gen = SGS_generator_create(srate, prg);

	do {
		run = SGS_generator_run(gen, buf, BUF_SAMPLES, &len);
		if (ad && SGS_audio_dev_write(ad, buf, len) != 0)
			fputs("warning: audio device write failed\n", stderr);
		if (wf && SGS_wav_file_write(wf, buf, len) != 0)
			fputs("warning: WAV file write failed\n", stderr);
	} while (run);

	if (ad) SGS_close_audio_dev(ad);
	if (wf) SGS_end_wav_file(wf);
	SGS_generator_destroy(gen);
	return 0;
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
static int parse_args(int argc, char **argv, uint *flags,
		const char **script_path, const char **wav_path,
		uint *srate) {
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
	uint options = 0;
	uchar audio_dev;
	uchar run_status;
	SGSProgram *prg;
	uint srate = DEFAULT_SRATE;
	if (parse_args(argc, argv, &options, &script_path, &wav_path,
			&srate) != 0)
		return 0;
	if (!(prg = SGS_program_create(script_path))) {
		fprintf(stderr, "error: couldn't open script file \"%s\"\n",
				script_path);
		return 1;
	}
	if (options & ARG_ONLY_COMPILE)
		return 0;
	audio_dev = (wav_path ?
			(options & ARG_ENABLE_AUDIO_DEV) :
			!(options & ARG_DISABLE_AUDIO_DEV));
	run_status = run_program(prg, audio_dev, wav_path, srate);
	SGS_program_destroy(prg);
	return run_status;
}
