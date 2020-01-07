/* mgensys: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: mgensys [-a|-m] [-r <srate>] [-p] [-o <wavfile>] <script>...\n"
"       mgensys [-a|-m] [-r <srate>] [-p] [-o <wavfile>] -e <string>...\n"
"       mgensys [-c] [-p] <script>...\n"
"       mgensys [-c] [-p] -e <string>...\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default "MGS_STREXP(MGS_DEFAULT_SRATE)");\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(MGS_CLINAME_STR" "MGS_VERSION_STR);
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
	if (errno || i <= 0 || endp == str || *endp)
		return -1;
	return i;
}

/*
 * Command line argument flags.
 */
enum {
	ARG_DO_RUN     = 1<<0, // more than checking
	ARG_ONLY_CHECK = 1<<1, // only checking
	ARG_ENABLE_AUDIO_DEV  = 1<<2,
	ARG_DISABLE_AUDIO_DEV = 1<<3,
	ARG_PRINT_INFO  = 1<<4,
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
		uint32_t *restrict flags,
		MGS_PtrArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	int i;
	*srate = MGS_DEFAULT_SRATE;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			MGS_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'a':
			if ((*flags & (ARG_DISABLE_AUDIO_DEV |
					ARG_ONLY_CHECK)) != 0)
				goto INVALID;
			*flags |= ARG_DO_RUN |
				ARG_ENABLE_AUDIO_DEV;
			break;
		case 'c':
			if ((*flags & ARG_DO_RUN) != 0)
				goto INVALID;
			*flags |= ARG_ONLY_CHECK;
			break;
		case 'e':
			*flags |= ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			goto CLEAR;
		case 'm':
			if ((*flags & (ARG_ENABLE_AUDIO_DEV |
					ARG_ONLY_CHECK)) != 0)
				goto INVALID;
			*flags |= ARG_DO_RUN |
				ARG_DISABLE_AUDIO_DEV;
			break;
		case 'o':
			if (arg[1] != '\0') goto INVALID;
			if ((*flags & ARG_ONLY_CHECK) != 0)
				goto INVALID;
			*flags |= ARG_DO_RUN;
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
			if ((*flags & ARG_ONLY_CHECK) != 0)
				goto INVALID;
			*flags |= ARG_DO_RUN;
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
			goto CLEAR;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (script_args->count != 0);
INVALID:
	print_usage(false);
CLEAR:
	MGS_PtrArr_clear(script_args);
	return false;
}

/*
 * Process the listed scripts.
 *
 * \return true if at least one script succesfully built
 */
static bool build(const MGS_PtrArr *restrict script_args,
		MGS_PtrArr *restrict prg_objs,
		uint32_t options) {
	bool are_paths = !(options & ARG_EVAL_STRING);
	bool print_info = (options & ARG_PRINT_INFO) != 0;
	size_t fails = MGS_build(script_args, are_paths, prg_objs, print_info);
	size_t built = prg_objs->count - fails;
	if (!built)
		return false;
	if ((options & ARG_ONLY_CHECK) != 0) {
		MGS_discard(prg_objs);
	}
	return true;
}

/*
 * Produce results from the list of programs, ignoring NULL entries.
 *
 * \return true unless error occurred
 */
static bool render(const MGS_PtrArr *restrict prg_objs,
		uint32_t srate, uint32_t options,
		const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & ARG_ENABLE_AUDIO_DEV) != 0) :
		((options & ARG_DISABLE_AUDIO_DEV) == 0);
	return MGS_render(prg_objs, srate, use_audiodev, wav_path);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	MGS_PtrArr script_args = (MGS_PtrArr){0};
	MGS_PtrArr prg_objs = (MGS_PtrArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = 0;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate)) {
		return 0;
	}
	bool error = !build(&script_args, &prg_objs, options);
	MGS_PtrArr_clear(&script_args);
	if (!error && prg_objs.count > 0) {
		error = !render(&prg_objs, srate, options, wav_path);
	}
	MGS_discard(&prg_objs);
	return error ? 1 : 0;
}
