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

#include "sgensys.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SRATE 44100

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: sgensys [-a|-m] [-r srate] [-p] [-o wavfile] scriptfile\n"
"       sgensys [-c] [-p] scriptfile\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default 44100);\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -c \tCheck script only, reporting any errors or requested info.\n"
"  -p \tPrint info for script after loading.\n"
"  -h \tPrint this message.\n",
	(by_arg) ? stdout : stderr);
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
	ARG_PRINT_INFO = 1<<4,
};

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **argv, uint32_t *flags,
		const char **script_path, const char **wav_path,
		uint32_t *srate) {
	int i;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!*script_path) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			if (*script_path) goto INVALID;
			*script_path = arg;
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
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (*script_path != NULL);

INVALID:
	print_usage(false);
	return false;
}

/*
 * Process the given script file.
 *
 * \return true unless error occurred
 */
static bool build(const char *fname, SGS_Program **prg_out,
		uint32_t options) {
	SGS_Program *prg;
	if (!(prg = SGS_build(fname)))
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

/*
 * Produce results from the given program.
 *
 * \return true unless error occurred
 */
static bool render(SGS_Program *prg, uint32_t srate,
		uint32_t options, const char *wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
			((options & ARG_ENABLE_AUDIO_DEV) != 0) :
			((options & ARG_DISABLE_AUDIO_DEV) == 0);
	return SGS_render(prg, srate, use_audiodev, wav_path);
}

/**
 * Main function.
 */
int main(int argc, char **argv) {
	const char *script_path = NULL, *wav_path = NULL;
	uint32_t options = 0;
	SGS_Program *prg;
	uint32_t srate = DEFAULT_SRATE;

	if (!parse_args(argc, argv, &options, &script_path, &wav_path,
			&srate))
		return 0;
	if (!build(script_path, &prg, options))
		return 1;
	if (prg != NULL) {
		bool error = !render(prg, srate, options, wav_path);
		SGS_discard_Program(prg);
		if (error)
			return 1;
	}

	return 0;
}
