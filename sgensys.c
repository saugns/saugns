/* sgensys: Main module / Command-line interface.
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

#include "sgensys.h"
#include "script.h"
#include "arrtype.h"
#include "file.h"
#include "generator.h"
#include "player/audiodev.h"
#include "player/wavfile.h"
#include "math.h"
#include "help.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define NAME SGS_CLINAME_STR

/*
 * Command line options flags.
 */
enum {
	SGS_OPT_MODE_FULL     = 1<<0,
	SGS_OPT_AUDIO_ENABLE  = 1<<1,
	SGS_OPT_AUDIO_DISABLE = 1<<2,
	SGS_OPT_MODE_CHECK    = 1<<3,
	SGS_OPT_PRINT_INFO    = 1<<4,
	SGS_OPT_EVAL_STRING   = 1<<5,
};

/*
 * Print help list for \p topic,
 * with an optional \p description in parentheses.
 */
static void print_help(const char *restrict topic,
		const char *restrict description) {
	const char *const *contents = SGS_find_help(topic);
	if (!contents || /* silence warning */ !topic) {
		topic = SGS_Help_names[SGS_HELP_HELP];
		contents = SGS_Help_names;
	}
	fprintf(stderr, "\nList of %s types", topic);
	if (description != NULL)
		fprintf(stderr, " (%s)", description);
	fputs(":\n", stderr);
	SGS_print_names(contents, "\t", stderr);
}

struct SGS_ScriptArg {
	const char *str;
};
sgsArrType(SGS_ScriptArgArr, struct SGS_ScriptArg, )
sgsArrType(SGS_ProgramArr, SGS_Program*, )

/*
 * Print command line usage instructions.
 */
static void print_usage(bool h_arg, const char *restrict h_type) {
	fputs(
"Usage: "NAME" [-a|-m] [-r <srate>] [-p] [-o <wavfile>] [-e] <script>...\n"
"       "NAME" [-c] [-p] [-e] <script>...\n",
		stderr);
	if (!h_type)
		fputs(
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default "SGS_STREXP(SGS_DEFAULT_SRATE)");\n"
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
	puts(NAME" "SGS_VERSION_STR);
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

/* SGS_getopt() data. Initialize to zero, except \a err for error messages. */
struct SGS_opt {
	int ind; /* set to zero to start over next SGS_getopt() call */
	int err;
	int pos;
	int opt;
	const char *arg;
};

/*
 * Command-line argument parser similar to POSIX getopt(),
 * but replacing opt* global variables with \p opt fields.
 *
 * The \a arg field is always set for each valid option, so as to be
 * available for reading as an unspecified optional option argument.
 *
 * In large part based on the public domain
 * getopt() version by Christopher Wellons.
 */
static int SGS_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct SGS_opt *restrict opt) {
	(void)argc;
	if (opt->ind == 0) {
		opt->ind = 1;
		opt->pos = 1;
	}
	const char *arg = argv[opt->ind];
	if (!arg || arg[0] != '-' || !SGS_IS_ASCIIVISIBLE(arg[1]))
		return -1;
	if (!strcmp(arg, "--")) {
		++opt->ind;
		return -1;
	}
	opt->opt = arg[opt->pos];
	const char *subs = strchr(optstring, opt->opt);
	if (opt->opt == ':' || !subs) {
		if (opt->err != 0 && *optstring != ':')
			fprintf(stderr, "%s: invalid option '%c'\n",
					argv[0], opt->opt);
		return '?';
	}
	if (subs[1] == ':') {
		if (arg[opt->pos + 1] != '\0') {
			opt->arg = &arg[opt->pos + 1];
			++opt->ind;
			opt->pos = 1;
			return opt->opt;
		}
		if (argv[opt->ind + 1] != NULL) {
			opt->arg = argv[opt->ind + 1];
			opt->ind += 2;
			opt->pos = 1;
			return opt->opt;
		}
		if (opt->err != 0 && *optstring != ':')
			fprintf(stderr,
"%s: option '%c' requires an argument\n",
					argv[0], opt->opt);
		return (*optstring == ':') ? ':' : '?';
	}
	if (arg[++opt->pos] == '\0') {
		++opt->ind;
		opt->pos = 1;
		opt->arg = argv[opt->ind];
	} else {
		opt->arg = &arg[opt->pos];
	}
	return opt->opt;
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
		SGS_ScriptArgArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	struct SGS_opt opt = (struct SGS_opt){0};
	int c;
	int32_t i;
	bool dashdash = false;
	bool h_arg = false;
	const char *h_type = NULL;
	*srate = SGS_DEFAULT_SRATE;
	opt.err = 1;
REPARSE:
	while ((c = SGS_getopt(argc, argv, "amr:o:ecphv", &opt)) != -1) {
		switch (c) {
		case 'a':
			if ((*flags & (SGS_OPT_AUDIO_DISABLE |
					SGS_OPT_MODE_CHECK)) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_FULL |
				SGS_OPT_AUDIO_ENABLE;
			break;
		case 'c':
			if ((*flags & SGS_OPT_MODE_FULL) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_CHECK;
			break;
		case 'e':
			*flags |= SGS_OPT_EVAL_STRING;
			break;
		case 'h':
			h_arg = true;
			h_type = opt.arg; /* optional argument for -h */
			goto USAGE;
		case 'm':
			if ((*flags & (SGS_OPT_AUDIO_ENABLE |
					SGS_OPT_MODE_CHECK)) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_FULL |
				SGS_OPT_AUDIO_DISABLE;
			break;
		case 'o':
			if ((*flags & SGS_OPT_MODE_CHECK) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_FULL;
			*wav_path = opt.arg;
			continue;
		case 'p':
			*flags |= SGS_OPT_PRINT_INFO;
			break;
		case 'r':
			if ((*flags & SGS_OPT_MODE_CHECK) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_FULL;
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
		struct SGS_ScriptArg entry = {arg};
		SGS_ScriptArgArr_add(script_args, &entry);
		++opt.ind;
		c = 0; /* only goto REPARSE after advancing, to prevent hang */
	}
	return true;
USAGE:
	print_usage(h_arg, h_type);
ABORT:
	SGS_ScriptArgArr_clear(script_args);
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

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SGS_File *f = open_file(script_arg, is_path);
	if (!f) return NULL;

	SGS_Program *o = NULL;
	SGS_Script *sd = SGS_read_Script(f);
	if (!sd) goto CLOSE;
	o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
CLOSE:
	SGS_destroy_File(f);
	return o;
}

/*
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
size_t SGS_read(const SGS_ScriptArgArr *restrict script_args, uint32_t options,
		SGS_ProgramArr *restrict prg_objs) {
	bool are_paths = !(options & SGS_OPT_EVAL_STRING);
	size_t built = 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		const SGS_Program *prg = build_program(script_args->a[i].str,
				are_paths);
		if (prg != NULL) ++built;
		SGS_ProgramArr_add(prg_objs, &prg);
	}
	return built;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SGS_discard(SGS_ProgramArr *restrict prg_objs) {
	for (size_t i = 0; i < prg_objs->count; ++i) {
		SGS_discard_Program(prg_objs->a[i]);
	}
	SGS_ProgramArr_clear(prg_objs);
}

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1
#define NUM_CHANNELS 2

typedef struct SGS_Output {
	SGS_AudioDev *ad;
	SGS_WAVFile *wf;
	int16_t *buf;
	uint32_t ad_srate;
	uint32_t options;
	size_t buf_len;
	size_t ch_len;
} SGS_Output;

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SGS_init_Output(SGS_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & SGS_OPT_AUDIO_ENABLE) != 0) :
		((options & SGS_OPT_AUDIO_DISABLE) == 0);
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (SGS_Output){0};
	o->options = options;
	if ((options & SGS_OPT_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SGS_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!o->ad)
			return false;
		o->ad_srate = ad_srate;
	}
	if (wav_path != NULL) {
		o->wf = SGS_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!o->wf)
			return false;
	}
	if (ad_srate != srate) {
		if (!o->wf || ad_srate > srate)
			max_srate = ad_srate;
	}

	o->ch_len = SGS_ms_in_samples(BUF_TIME_MS, max_srate, NULL);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf_len = o->ch_len * NUM_CHANNELS;
	o->buf = calloc(o->buf_len, sizeof(int16_t));
	if (!o->buf)
		return false;
	return true;
}

/*
 * \return true unless error occurred
 */
static bool SGS_fini_Output(SGS_Output *restrict o) {
	free(o->buf);
	if (o->ad != NULL) SGS_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (SGS_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool SGS_Output_run(SGS_Output *restrict o,
		const SGS_Program *restrict prg, uint32_t srate,
		bool use_audiodev, bool use_wavfile) {
	SGS_Generator *gen = SGS_create_Generator(prg, srate);
	if (!gen)
		return false;
	size_t len;
	bool error = false;
	bool run = !(o->options & SGS_OPT_MODE_CHECK);
	use_audiodev = use_audiodev && (o->ad != NULL);
	use_wavfile = use_wavfile && (o->wf != NULL);
	while (run) {
		run = SGS_Generator_run(gen, o->buf, o->ch_len, &len);
		if (use_audiodev && !SGS_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			SGS_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !SGS_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			SGS_error(NULL, "WAV file write failed");
		}
	}
	SGS_destroy_Generator(gen);
	return !error;
}

/*
 * Run the listed programs through the audio generator until completion,
 * ignoring NULL entries.
 *
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SGS_play(const SGS_ProgramArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	SGS_Output out;
	bool status = true;
	if (!SGS_init_Output(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen;
	if (out.ad != NULL && out.wf != NULL && (out.ad_srate != srate)) {
		split_gen = true;
		SGS_warning(NULL,
"generating audio twice, using different sample rates");
	} else {
		split_gen = false;
		if (out.ad != NULL) srate = out.ad_srate;
	}
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const SGS_Program *prg = prg_objs->a[i];
		if (!prg) continue;
		if ((options & SGS_OPT_PRINT_INFO) != 0)
			SGS_Program_print_info(prg);
		if (split_gen) {
			if (!SGS_Output_run(&out, prg, out.ad_srate,
						true, false))
				status = false;
			if (!SGS_Output_run(&out, prg, srate,
						false, true))
				status = false;
		} else {
			if (!SGS_Output_run(&out, prg, srate,
						true, true))
				status = false;
		}
	}

CLEANUP:
	if (!SGS_fini_Output(&out))
		status = false;
	return status;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SGS_ScriptArgArr script_args = (SGS_ScriptArgArr){0};
	SGS_ProgramArr prg_objs = (SGS_ProgramArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = 0;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate))
		return 0;
	bool error = !SGS_read(&script_args, options, &prg_objs);
	SGS_ScriptArgArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		error = !SGS_play(&prg_objs, srate, options, wav_path);
		SGS_discard(&prg_objs);
		if (error)
			return 1;
	}
	return 0;
}
