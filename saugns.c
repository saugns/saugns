/* saugns: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
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
#include "help.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define NAME SAU_CLINAME_STR
#if SAU_ADD_TESTOPT
# define TESTOPT "?:"
int SAU_testopt = 0;
#else
# define TESTOPT
#endif

/*
 * Print help list for \p topic,
 * with an optional \p description in parentheses.
 */
static void print_help(const char *restrict topic,
		const char *restrict description) {
	const char *const *contents = SAU_find_help(topic);
	if (!contents || /* silence warning */ !topic) {
		topic = SAU_Help_names[SAU_HELP_HELP];
		contents = SAU_Help_names;
	}
	fprintf(stderr, "\nList of '%s' names", topic);
	if (description != NULL)
		fprintf(stderr, " (%s)", description);
	fputs(":\n", stderr);
	SAU_print_names(contents, "\t", stderr);
}

/*
 * Print command line usage instructions.
 */
static void print_usage(bool h_arg, const char *restrict h_type) {
	fputs(
"Usage: "NAME" [-a | -m] [-r <srate>] [--mono] [-o <wavfile>] [--stdout]\n"
"              [-p] [-e] <script>...\n"
"       "NAME" -c [-p] [-e] <script>...\n",
		stderr);
	if (!h_type)
		fputs(
"\n"
"Audio output options (by default, system audio output is enabled):\n"
"  -a \tAudible; always enable system audio output.\n"
"  -m \tMuted; always disable system audio output.\n"
"  -r \tSample rate in Hz (default "SAU_STREXP(SAU_DEFAULT_SRATE)");\n"
"     \tif unsupported for system audio, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested.\n"
"     \tOr for AU over stdout, \"-\". Disables system audio output by default.\n"
"  --mono \tDownmix and output audio as mono; this applies to all outputs.\n"
"  --stdout \tSend a raw 16-bit output to stdout, -r or default sample rate.\n"
"\n"
"Other options:\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -e \tEvaluate strings instead of files.\n"
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
	fputs(NAME" "SAU_VERSION_STR"\n", stderr);
}

/*
 * Read an integer from the given string.
 *
 * \return true, or false on error
 */
static bool get_iarg(const char *restrict str, int32_t *i) {
	char *endp;
	errno = 0;
	*i = strtol(str, &endp, 10);
	if (errno || endp == str || *endp)
		return false;
	return true;
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
		SAU_PtrArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	struct SAU_opt opt = (struct SAU_opt){0};
	int c;
	int32_t i;
	bool dashdash = false;
	bool h_arg = false;
	const char *h_type = NULL;
	*srate = SAU_DEFAULT_SRATE;
	opt.err = 1;
REPARSE:
	while ((c = SAU_getopt(argc, argv,
	                       "amr:o:ecphv"TESTOPT
			       "-mono-stdout", &opt)) != -1) {
		switch (c) {
		case '-':
			if (!strcmp(opt.arg, "mono")) {
				if (*flags & SAU_OPT_MODE_CHECK)
					goto USAGE;
				*flags |= SAU_OPT_MODE_FULL |
					SAU_OPT_AUDIO_MONO;
			}
			else if (!strcmp(opt.arg, "stdout")) {
				if (*flags & (SAU_OPT_MODE_CHECK |
				              SAU_OPT_AUFILE_STDOUT))
					goto USAGE;
				*flags |= SAU_OPT_MODE_FULL |
					SAU_OPT_AUDIO_STDOUT;
				SAU_stdout_busy = 1; /* required for audio */
			} else {
				goto USAGE;
			}
			break;
#if SAU_ADD_TESTOPT
		case '?':
			if (!get_iarg(opt.arg, &i)) goto USAGE;
			SAU_testopt = i;
			continue;
#endif
		case 'a':
			if (*flags & (SAU_OPT_SYSAU_DISABLE |
			              SAU_OPT_MODE_CHECK))
				goto USAGE;
			*flags |= SAU_OPT_MODE_FULL |
				SAU_OPT_SYSAU_ENABLE;
			break;
		case 'c':
			if (*flags & SAU_OPT_MODE_FULL)
				goto USAGE;
			*flags |= SAU_OPT_MODE_CHECK;
			break;
		case 'e':
			*flags |= SAU_OPT_EVAL_STRING;
			break;
		case 'h':
			h_arg = true;
			h_type = opt.arg; /* optional argument for -h */
			goto USAGE;
		case 'm':
			if (*flags & (SAU_OPT_SYSAU_ENABLE |
			              SAU_OPT_MODE_CHECK))
				goto USAGE;
			*flags |= SAU_OPT_MODE_FULL |
				SAU_OPT_SYSAU_DISABLE;
			break;
		case 'o':
			if (*flags & SAU_OPT_MODE_CHECK)
				goto USAGE;
			if (!strcmp(opt.arg, "-")) {
				if (*flags & SAU_OPT_AUDIO_STDOUT)
					goto USAGE;
				*flags |= SAU_OPT_AUFILE_STDOUT;
				SAU_stdout_busy = 1; /* required for AU file */
			}
			*flags |= SAU_OPT_MODE_FULL;
			*wav_path = opt.arg;
			continue;
		case 'p':
			*flags |= SAU_OPT_PRINT_INFO;
			break;
		case 'r':
			if (*flags & SAU_OPT_MODE_CHECK)
				goto USAGE;
			*flags |= SAU_OPT_MODE_FULL;
			if (!get_iarg(opt.arg, &i) || (i <= 0)) goto USAGE;
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
		SAU_PtrArr_add(script_args, (void*) arg);
		++opt.ind;
		c = 0; /* only goto REPARSE after advancing, to prevent hang */
	}
	return true;
USAGE:
	print_usage(h_arg, h_type);
ABORT:
	SAU_PtrArr_clear(script_args);
	return false;
}

/*
 * Discard the scripts in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SAU_discard(SAU_PtrArr *restrict script_objs) {
	SAU_Script **scripts = (SAU_Script**) SAU_PtrArr_ITEMS(script_objs);
	for (size_t i = 0; i < script_objs->count; ++i) {
		SAU_discard_Script(scripts[i]);
	}
	SAU_PtrArr_clear(script_objs);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SAU_PtrArr script_args = (SAU_PtrArr){0};
	SAU_PtrArr script_objs = (SAU_PtrArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = 0;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate))
		return 0;
	bool error = !SAU_load(&script_args, options, &script_objs);
	SAU_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (script_objs.count > 0) {
		error = !SAU_play(&script_objs, srate, options, wav_path);
		SAU_discard(&script_objs);
		if (error)
			return 1;
	}
	return 0;
}
