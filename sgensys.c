/* sgensys: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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
#include "script.h"
#include "file.h"
#include "generator.h"
#include "audiodev.h"
#include "wavfile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SRATE 44100

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: sgensys [-a|-m] [-r srate] [-p] [-o wavfile] [-e] script\n"
"       sgensys [-c] [-p] [-e] script\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default 44100);\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -e \tEvaluate string instead of file.\n"
"  -c \tCheck script only, reporting any errors or requested info.\n"
"  -p \tPrint info for script after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(SGS_VERSION_STR);
}

/*
 * Read a positive integer from the given string.
 *
 * \return positive value or -1 if invalid
 */
static int32_t get_piarg(const char *restrict str) {
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
	ARG_PRINT_INFO = 1<<4,
	ARG_EVAL_STRING = 1<<5,
};

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags, const char **restrict script_arg,
		uint32_t *restrict srate, const char **restrict wav_path) {
	int i;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!*script_arg) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			if (*script_arg) goto INVALID;
			*script_arg = arg;
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'a':
			if ((*flags & (ARG_DISABLE_AUDIO_DEV |
					ARG_ONLY_COMPILE)) != 0)
				goto INVALID;
			*flags |= ARG_FULL_RUN |
				ARG_ENABLE_AUDIO_DEV;
			break;
		case 'c':
			if ((*flags & ARG_FULL_RUN) != 0)
				goto INVALID;
			*flags |= ARG_ONLY_COMPILE;
			break;
		case 'e':
			*flags |= ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			return false;
		case 'm':
			if ((*flags & (ARG_ENABLE_AUDIO_DEV |
					ARG_ONLY_COMPILE)) != 0)
				goto INVALID;
			*flags |= ARG_FULL_RUN |
				ARG_DISABLE_AUDIO_DEV;
			break;
		case 'o':
			if (arg[1] != '\0') goto INVALID;
			if ((*flags & ARG_ONLY_COMPILE) != 0)
				goto INVALID;
			*flags |= ARG_FULL_RUN;
			--argc;
			++argv;
			if (argc < 1) goto INVALID;
			arg = *argv;
			*wav_path = arg;
			continue;
		case 'p':
			*flags |= ARG_PRINT_INFO;
			break;
		case 'r':
			if (arg[1] != '\0') goto INVALID;
			if ((*flags & ARG_ONLY_COMPILE) != 0)
				goto INVALID;
			*flags |= ARG_FULL_RUN;
			--argc;
			++argv;
			if (argc < 1) goto INVALID;
			arg = *argv;
			i = get_piarg(arg);
			if (i < 0) goto INVALID;
			*srate = i;
			continue;
		case 'v':
			print_version();
			return false;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (*script_arg != NULL);

INVALID:
	print_usage(false);
	return false;
}

/*
 * Open file for script arg.
 *
 * \return instance or NULL on error
 */
static SGS_File *open_file(const char *restrict script_arg, bool is_path) {
	SGS_File *f = SGS_create_File();
	if (!f) return NULL;
	if (!is_path) {
		SGS_File_stropenrb(f, "<string>", script_arg);
		return f;
	}
	if (!SGS_File_fopenrb(f, script_arg)) {
		SGS_error(NULL,
"couldn't open script file \"%s\" for reading", script_arg);
		SGS_destroy_File(f);
		return NULL;
	}
	return f;
}

/**
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build(const char *restrict script_arg, bool is_path) {
	SGS_File *f = open_file(script_arg, is_path);
	if (!f) return NULL;

	SGS_Program *o = NULL;
	SGS_Script *sd = SGS_load_Script(f);
	if (!sd) goto CLOSE;
	o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
CLOSE:
	SGS_destroy_File(f);
	return o;
}

/*
 * Process the given script file.
 *
 * \return true unless error occurred
 */
static bool build(const char *restrict script_arg,
		SGS_Program **restrict prg_out,
		uint32_t options) {
	SGS_Program *prg;
	bool is_path = !(options & ARG_EVAL_STRING);
	if (!(prg = SGS_build(script_arg, is_path)))
		return false;
	if ((options & ARG_PRINT_INFO) != 0)
		SGS_Program_print_info(prg);
	if ((options & ARG_ONLY_COMPILE) != 0) {
		SGS_discard_Program(prg);
		*prg_out = NULL;
		return true;
	}

	*prg_out = prg;
	return true;
}

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

/*
 * Produce results from the given program.
 *
 * \return true unless error occurred
 */
static bool render(SGS_Program *restrict prg, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
			((options & ARG_ENABLE_AUDIO_DEV) != 0) :
			((options & ARG_DISABLE_AUDIO_DEV) == 0);
	return SGS_render(prg, srate, use_audiodev, wav_path);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	const char *script_arg = NULL, *wav_path = NULL;
	uint32_t options = 0;
	SGS_Program *prg;
	uint32_t srate = DEFAULT_SRATE;

	if (!parse_args(argc, argv, &options, &script_arg, &srate, &wav_path))
		return 0;
	if (!build(script_arg, &prg, options))
		return 1;
	if (prg != NULL) {
		bool error = !render(prg, srate, options, wav_path);
		SGS_discard_Program(prg);
		if (error)
			return 1;
	}

	return 0;
}
