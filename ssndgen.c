/* ssndgen: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2021 Joel K. Pettersson
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

#include "ssndgen.h"
#include "help.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define NAME SSG_CLINAME_STR

/*
 * Print help list for \p topic,
 * with an optional \p description in parentheses.
 */
static void print_help(const char *restrict topic,
		const char *restrict description) {
	const char *const *contents = SSG_find_help(topic);
	if (!contents || /* silence warning */ !topic) {
		topic = SSG_Help_names[SSG_HELP_HELP];
		contents = SSG_Help_names;
	}
	fprintf(stderr, "\nList of %s types", topic);
	if (description != NULL)
		fprintf(stderr, " (%s)", description);
	fputs(":\n", stderr);
	SSG_print_names(contents, "\t", stderr);
}

/*
 * Print command line usage instructions.
 */
static void print_usage(bool h_arg, const char *restrict h_type) {
	fputs(
"Usage: "NAME" [-a|-m] [-r <srate>] [-o <wavfile>] [options] <script>...\n"
"       "NAME" [-c] [options] <script>...\n"
"Common options: [-e] [-p]\n",
		stderr);
	if (!h_type)
		fputs(
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default "SSG_STREXP(SSG_DEFAULT_SRATE)");\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this and list help topics, or print help for '-h <topic>'.\n"
"  -v \tPrint version.\n",
			stderr);
	if (h_arg) {
		const char *description = (h_type != NULL) ?
			"pass '-h' without topic for general usage" :
			"pass with '-h' as topic";
		print_help(h_type, description);
	}
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(NAME" "SSG_VERSION_STR);
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
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SSG_PtrArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	struct SSG_opt opt = (struct SSG_opt){0};
	int c;
	int32_t i;
	bool dashdash = false;
	bool h_arg = false;
	const char *h_type = NULL;
	*srate = SSG_DEFAULT_SRATE;
	opt.err = 1;
REPARSE:
	while ((c = SSG_getopt(argc, argv, "amr:o:ecphv", &opt)) != -1) {
		switch (c) {
		case 'a':
			if ((*flags & (SSG_ARG_AUDIO_DISABLE |
					SSG_ARG_MODE_CHECK)) != 0)
				goto USAGE;
			*flags |= SSG_ARG_MODE_FULL |
				SSG_ARG_AUDIO_ENABLE;
			break;
		case 'c':
			if ((*flags & SSG_ARG_MODE_FULL) != 0)
				goto USAGE;
			*flags |= SSG_ARG_MODE_CHECK;
			break;
		case 'e':
			*flags |= SSG_ARG_EVAL_STRING;
			break;
		case 'h':
			h_arg = true;
			h_type = opt.arg; /* optional argument for -h */
			goto USAGE;
		case 'm':
			if ((*flags & (SSG_ARG_AUDIO_ENABLE |
					SSG_ARG_MODE_CHECK)) != 0)
				goto USAGE;
			*flags |= SSG_ARG_MODE_FULL |
				SSG_ARG_AUDIO_DISABLE;
			break;
		case 'o':
			if ((*flags & SSG_ARG_MODE_CHECK) != 0)
				goto USAGE;
			*flags |= SSG_ARG_MODE_FULL;
			*wav_path = opt.arg;
			continue;
		case 'p':
			*flags |= SSG_ARG_PRINT_INFO;
			break;
		case 'r':
			if ((*flags & SSG_ARG_MODE_CHECK) != 0)
				goto USAGE;
			*flags |= SSG_ARG_MODE_FULL;
			i = get_piarg(opt.arg);
			if (i < 0) goto USAGE;
			*srate = i;
			continue;
		case 'v':
			print_version();
			goto ABORT;
		default:
			fputs("Pass -h for general usage help.\n", stderr);
			goto ABORT;
		}
	}
	if (opt.ind > 1 && !strcmp(argv[opt.ind - 1], "--")) dashdash = true;
	for (;;) {
		if (opt.ind >= argc || !argv[opt.ind]) {
			if (!script_args->count) goto USAGE;
			break;
		}
		const char *arg = argv[opt.ind];
		if (!dashdash && c != -1 && arg[0] == '-') goto REPARSE;
		SSG_PtrArr_add(script_args, (void*) arg);
		++opt.ind;
		c = 0; /* only goto REPARSE after advancing, to prevent hang */
	}
	return true;
USAGE:
	print_usage(h_arg, h_type);
ABORT:
	SSG_PtrArr_clear(script_args);
	return false;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SSG_PtrArr script_args = (SSG_PtrArr){0};
	SSG_PtrArr prg_objs = (SSG_PtrArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = 0;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate))
		return 0;
	bool error = !SSG_build(&script_args, options, &prg_objs);
	SSG_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		error = !SSG_play(&prg_objs, srate, options, wav_path);
		SSG_discard(&prg_objs);
		if (error)
			return 1;
	}
	return 0;
}
