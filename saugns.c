/* saugns: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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

#include "saugns.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SRATE 44100

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: saugns [-a|-m] [-r srate] [-p] [-o wavfile] scriptfile(s)\n"
"       saugns [-c] [-p] scriptfile(s)\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default 44100);\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
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
	puts(SAU_VERSION_STR);
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
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SAU_PtrList *restrict script_paths,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	int i;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_paths->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SAU_PtrList_add(script_paths, arg);
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
			goto CLEAR;
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
			goto CLEAR;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (script_paths->count != 0);

INVALID:
	print_usage(false);
CLEAR:
	SAU_PtrList_clear(script_paths);
	return false;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
static void discard_programs(SAU_PtrList *restrict prg_list) {
	SAU_Program **prgs = (SAU_Program**) SAU_PtrList_ITEMS(prg_list);
	for (size_t i = 0; i < prg_list->count; ++i) {
		SAU_Program *prg = prgs[i];
		if (prg != NULL) SAU_discard_Program(prg);
	}
	SAU_PtrList_clear(prg_list);
}

/*
 * Process the listed script files.
 *
 * \return true if at least one script succesfully built
 */
static bool build(const SAU_PtrList *restrict path_list,
		SAU_PtrList *restrict prg_list,
		uint32_t options) {
	if (!SAU_build(path_list, prg_list))
		return false;
	if ((options & ARG_PRINT_INFO) != 0) {
		const SAU_Program **prgs =
			(const SAU_Program**) SAU_PtrList_ITEMS(prg_list);
		for (size_t i = 0; i < prg_list->count; ++i) {
			const SAU_Program *prg = prgs[i];
			if (prg != NULL) SAU_Program_print_info(prg);
		}
	}
	if ((options & ARG_ONLY_COMPILE) != 0) {
		discard_programs(prg_list);
	}
	return true;
}

/*
 * Produce results from the list of programs, ignoring NULL entries.
 *
 * \return true unless error occurred
 */
static bool render(const SAU_PtrList *restrict prg_list,
		uint32_t srate, uint32_t options,
		const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & ARG_ENABLE_AUDIO_DEV) != 0) :
		((options & ARG_DISABLE_AUDIO_DEV) == 0);
	return SAU_render(prg_list, srate, use_audiodev, wav_path);
}

/**
 * Main function.
 */
int main(int argc, char **argv) {
	SAU_PtrList script_paths = (SAU_PtrList){0};
	SAU_PtrList prg_list = (SAU_PtrList){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = DEFAULT_SRATE;

	if (!parse_args(argc, argv, &options, &script_paths, &wav_path,
			&srate))
		return 0;

	bool error = !build(&script_paths, &prg_list, options);
	SAU_PtrList_clear(&script_paths);
	if (error)
		return 1;
	if (prg_list.count > 0) {
		error = !render(&prg_list, srate, options, wav_path);
		discard_programs(&prg_list);
		if (error)
			return 1;
	}
	return 0;
}
