/* saugns: Main module / Command-line interface.
 * Copyright (c) 2011-2013, 2017-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "saugns.h"
#include <sau/script.h>
#include <sau/scanner.h> // character tests
#include <sau/arrtype.h>
#include <sau/generator.h>
#include <sau/math.h>
#include <sau/help.h>
#include "player/audiodev.h"
#include "player/sndfile.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define NAME CLINAME_STR
#if SGS_ADD_TESTOPT
# define TESTOPT "?:"
int SGS_testopt = 0;
#else
# define TESTOPT
#endif

/*
 * Command line options flags.
 */
enum {
	OPT_MODE_FULL     = 1<<0,
	OPT_SYSAU_ENABLE  = 1<<1,
	OPT_SYSAU_DISABLE = 1<<2,
	OPT_AUDIO_MONO    = 1<<3,
	OPT_AUDIO_STDOUT  = 1<<4,
	OPT_AUFILE_STDOUT = 1<<5,
	OPT_MODE_CHECK    = 1<<6,
	OPT_PRINT_INFO    = 1<<7,
	OPT_EVAL_STRING   = 1<<8,
	OPT_DETERMINISTIC = 1<<9,
	OPT_PRINT_VERBOSE = 1<<10,
};

/*
 * Print help list for \p topic,
 * with an optional \p description in parentheses.
 */
static void print_help(const char *restrict topic,
		const char *restrict description) {
	const char *const *contents = sau_find_help(topic);
	if (!contents || /* silence warning */ !topic) {
		topic = sauHelp_names[SAU_HELP_N_help];
		contents = sauHelp_names;
	}
	fprintf(stdout, "\nList of '%s' names", topic);
	if (description != NULL)
		fprintf(stdout, " (%s)", description);
	fputs(":\n", stdout);
	sau_print_names(contents, "\t", stdout);
}

sauArrType(sauScriptPredefArr, sauScriptPredef, )
sauArrType(sauScriptArgArr, sauScriptArg, )
sauArrType(sauProgramArr, sauProgram*, )

/*
 * Print command line usage instructions.
 */
static void print_usage(bool h_arg, const char *restrict h_type) {
	fputs(
"Usage: "NAME" [-a | -m] [-r <srate>] [--mono] [-o <file>] [--stdout]\n"
"              [-d] [-p] [variable=value] [-e] <script>...\n"
"       "NAME" -c [-d] [-p] [variable=value] [-e] <script>...\n",
		h_arg ? stdout : stderr);
	if (!h_type)
		fputs(
"\n"
"Audio output options (by default, system audio output is enabled):\n"
"  -a \tAudible; always enable system audio output.\n"
"  -m \tMuted; always disable system audio output.\n"
"  -r \tSample rate in Hz (default "SAU_STREXP(DEFAULT_SRATE)");\n"
"     \tif unsupported for system audio, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested.\n"
"     \tOr for AU over stdout, \"-\". Disables system audio output by default.\n"
"  --mono \tDownmix and output audio as mono; this applies to all outputs.\n"
"  --stdout \tSend a raw 16-bit output to stdout, -r or default sample rate.\n"
"\n"
"Other options:\n"
"  -c \tCheck scripts only; parse, handle -p, but don't interpret unlike -m.\n"
"  -d \tDeterministic mode; ensures unvarying script output from same input.\n"
"  -p \tPrint info for scripts read.\n"
"  -e \tEvaluate strings instead of files. Applies to scripts after.\n"
"  -h \tPrint this and list help topics, or print help for '-h <topic>'.\n"
"  -v \tBe verbose.\n"
"  -V \tPrint version.\n"
"  variable=value\tSet variable, passed on to scripts as \"$variable\".\n",
			h_arg ? stdout : stderr);
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
	fputs(NAME" "VERSION_STR"\n", stdout);
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
 * Read a predefine value argument from the given string.
 * Values are set to \p def, including reusing \p str.
 *
 * \return true, or false on error
 */
static bool get_defarg(sauScriptPredef *restrict def,
		const char *restrict str) {
	bool eq = false;
	uint32_t i, len = 0;
	double val;
	const char *valp;
	char *endp;
	int c;
	for (i = 0; str[i]; ++i) {
		c = str[i];
		if (c == '=') {
			eq = true;
			break;
		}
		else if (!sau_is_symchar(c))
			return false;
	}
	if (!eq)
		return false;
	len = i;
	valp = &str[len + 1];
	val = strtod(valp, &endp);
	if (endp == valp || *endp != '\0')
		return false;
	*def = (sauScriptPredef){
		.key = str, .len = len, .val = val
	};
	return true;
}

/* getopt() data. Initialize to zero, except \a err for error messages. */
struct Opt {
	int ind; /* set to zero to start over next getopt() call */
	int err;
	int pos;
	int opt;
	const char *arg;
};

/*
 * Compare to name substring, which may be terminated either
 * with a NULL byte, or with a '-' character (which precedes
 * a next substring).
 */
static bool streq_longname(const char *restrict arg,
		const char *restrict name) {
	size_t i;
	for (i = 0; arg[i] != '\0' && arg[i] == name[i]; ++i) ;
	return arg[i] == '\0' &&
		(name[i] == '\0' || name[i] == '-');
}

/*
 * Command-line argument parser similar to POSIX getopt(),
 * but replacing opt* global variables with \p opt fields.
 *
 * For unrecognized options, will return 1 instead of '?',
 * freeing up '?' for possible use as another option name.
 * Allows only a limited form of "--long" option use, with
 * the '-' regarded as the option and "long" its argument.
 * A '-' in \p optstring must be after short options, each
 * '-' followed by a string to recognize as the long name.
 *
 * The \a arg field is always set for each valid option, so as to be
 * available for reading as an unspecified optional option argument.
 *
 * In large part based on the public domain
 * getopt() version by Christopher Wellons.
 * Not the nonstandard extensions, however.
 */
static int getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct Opt *restrict opt) {
	(void)argc;
	if (opt->ind == 0) {
		opt->ind = 1;
		opt->pos = 1;
	}
	const char *arg = argv[opt->ind], *subs;
	if (!arg || arg[0] != '-' || !SAU_IS_ASCIIVISIBLE(arg[1]))
		return -1;
	const char *shortend = strchr(optstring, '-');
	if (arg[1] == '-') {
		if (arg[2] == '\0') {
			++opt->ind;
			return -1;
		}
		subs = shortend;
		while (subs) {
			if (streq_longname(arg + 2, subs + 1)) {
				opt->opt = '-';
				opt->arg = arg + 2;
				++opt->ind;
				opt->pos = 1;
				return opt->opt;
			}
			subs = strchr(subs + 1, '-');
		}
		if (opt->err)
			fprintf(stderr, "%s: invalid option \"%s\"\n",
					argv[0], arg);
		return 1;
	}
	opt->opt = arg[opt->pos];
	subs = strchr(optstring, opt->opt);
	if (opt->opt == ':' || !subs || (shortend && subs >= shortend)) {
		if (opt->err && *optstring != ':')
			fprintf(stderr, "%s: invalid option '%c'\n",
					argv[0], opt->opt);
		return 1;
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
		if (opt->err && *optstring != ':')
			fprintf(stderr,
"%s: option '%c' requires an argument\n",
					argv[0], opt->opt);
		return (*optstring == ':') ? ':' : 1;
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
		sauScriptArgArr *restrict script_args,
		sauScriptPredefArr *restrict predef_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	struct Opt opt = {0};
	sauScriptPredef predef = {0};
	int c;
	int32_t i;
	bool dashdash = false;
	bool h_arg = false;
	const char *h_type = NULL;
	*srate = DEFAULT_SRATE;
	opt.err = 1;
REPARSE:
	while ((c = getopt(argc, argv,
	                       "Vamr:o:ecdphv"TESTOPT
			       "-mono-stdout", &opt)) != -1) {
		switch (c) {
		case '-':
			if (!strcmp(opt.arg, "mono")) {
				if (*flags & OPT_MODE_CHECK)
					goto USAGE;
				*flags |= OPT_MODE_FULL |
					OPT_AUDIO_MONO;
			} else if (!strcmp(opt.arg, "stdout")) {
				if (*flags & (OPT_MODE_CHECK |
				              OPT_AUFILE_STDOUT))
					goto USAGE;
				*flags |= OPT_MODE_FULL |
					OPT_AUDIO_STDOUT;
				sau_stdout_busy = 1; /* required for audio */
			} else {
				goto USAGE;
			}
			break;
#if SGS_ADD_TESTOPT
		case '?':
			if (!get_iarg(opt.arg, &i)) goto USAGE;
			SGS_testopt = i;
			continue;
#endif
		case 'V':
			print_version();
			goto ABORT;
		case 'a':
			if (*flags & (OPT_SYSAU_DISABLE |
			              OPT_MODE_CHECK))
				goto USAGE;
			*flags |= OPT_MODE_FULL |
				OPT_SYSAU_ENABLE;
			break;
		case 'c':
			if (*flags & OPT_MODE_FULL)
				goto USAGE;
			*flags |= OPT_MODE_CHECK;
			break;
		case 'd':
			*flags |= OPT_DETERMINISTIC;
			break;
		case 'e':
			*flags |= OPT_EVAL_STRING;
			break;
		case 'h':
			h_arg = true;
			h_type = opt.arg; /* optional argument for -h */
			goto USAGE;
		case 'm':
			if (*flags & (OPT_SYSAU_ENABLE |
			              OPT_MODE_CHECK))
				goto USAGE;
			*flags |= OPT_MODE_FULL |
				OPT_SYSAU_DISABLE;
			break;
		case 'o':
			if (*flags & OPT_MODE_CHECK)
				goto USAGE;
			if (!strcmp(opt.arg, "-")) {
				if (*flags & OPT_AUDIO_STDOUT)
					goto USAGE;
				*flags |= OPT_AUFILE_STDOUT;
				sau_stdout_busy = 1; /* required for AU file */
			}
			*flags |= OPT_MODE_FULL;
			*wav_path = opt.arg;
			continue;
		case 'p':
			*flags |= OPT_PRINT_INFO;
			break;
		case 'r':
			if (*flags & OPT_MODE_CHECK)
				goto USAGE;
			*flags |= OPT_MODE_FULL;
			if (!get_iarg(opt.arg, &i) || (i <= 0)) goto USAGE;
			*srate = i;
			continue;
		case 'v':
			*flags |= OPT_PRINT_VERBOSE;
			break;
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
		const char *str = argv[opt.ind];
		if (!dashdash) {
			if (c != -1 && str[0] == '-') goto REPARSE;
			/*
			 * Check for and read "variable=number" string.
			 */
			if (!(*flags & OPT_EVAL_STRING) && strchr(str, '=')) {
				if (get_defarg(&predef, str)) {
					if (!sauScriptPredefArr_push(
							predef_args, &predef))
						goto ABORT;
				} else {
					fprintf(stderr,
				"%s: malformed \"variable=number\" string\n",
							argv[0]);
				}
				goto NEXT_POSTOPT;
			}
		}
		sauScriptArg *arg = sauScriptArgArr_add(script_args);
		if (!arg) goto ABORT;
		arg->str = str;
		arg->is_path = !(*flags & OPT_EVAL_STRING);
	NEXT_POSTOPT:
		++opt.ind;
		c = 0; /* only goto REPARSE after advancing, to prevent hang */
	}
	for (size_t i = 0; i < script_args->count; ++i) {
		sauScriptArg *arg = &script_args->a[i];
		arg->no_time = *flags & OPT_DETERMINISTIC;
		arg->predef = predef_args->a;
		arg->predef_count = predef_args->count;
	}
	return true;
USAGE:
	print_usage(h_arg, h_type);
ABORT:
	sauScriptPredefArr_clear(predef_args);
	sauScriptArgArr_clear(script_args);
	return false;
}

/*
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
static size_t read_scripts(const sauScriptArgArr *restrict script_args,
		sauProgramArr *restrict prg_objs) {
	size_t built = 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		const sauProgram *prg = sau_build_Program(&script_args->a[i]);
		if (prg != NULL) ++built;
		sauProgramArr_push(prg_objs, &prg);
	}
	return built;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
static void discard(sauProgramArr *restrict prg_objs) {
	for (size_t i = 0; i < prg_objs->count; ++i) {
		sau_discard_Program(prg_objs->a[i]);
	}
	sauProgramArr_clear(prg_objs);
}

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1

struct Player {
	SGS_AudioDev *ad;
	SGS_SndFile *sf;
	int16_t *buf, *ad_buf;
	uint32_t srate, ad_srate;
	uint32_t options;
	uint32_t ch_count;
	uint32_t ch_len, ad_ch_len;
};

/*
 * Set up use of system audio device, raw audio to stdout,
 * and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool init_Player(struct Player *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool split_gen = false;
	bool use_audiodev = (wav_path) ?
		((options & OPT_SYSAU_ENABLE) != 0) :
		((options & OPT_SYSAU_DISABLE) == 0);
	bool use_stdout = (options & OPT_AUDIO_STDOUT);
	uint32_t ad_srate = srate;
	*o = (struct Player){0};
	o->options = options;
	o->ch_count = (options & OPT_AUDIO_MONO) ? 1 : 2;
	if ((options & OPT_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SGS_open_AudioDev(o->ch_count, &ad_srate);
		if (!o->ad)
			return false;
	}
	if (wav_path) {
		if (options & OPT_AUFILE_STDOUT)
			o->sf = SGS_create_SndFile(NULL, SGS_SNDFILE_AU,
					o->ch_count, srate);
		else
			o->sf = SGS_create_SndFile(wav_path, SGS_SNDFILE_WAV,
					o->ch_count, srate);
		if (!o->sf)
			return false;
	}
	if (ad_srate != srate) {
		if (use_stdout || o->sf)
			split_gen = true;
		else
			srate = ad_srate;
	}

	o->srate = srate;
	o->ch_len = sau_ms_in_samples(BUF_TIME_MS, srate, NULL);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf = calloc(o->ch_len * o->ch_count, sizeof(int16_t));
	if (!o->buf)
		return false;
	if (split_gen) {
		/*
		 * For alternating buffered generation with non-ad_* version.
		 */
		o->ad_srate = ad_srate;
		o->ad_ch_len = sau_ms_in_samples(BUF_TIME_MS, ad_srate, NULL);
		if (o->ad_ch_len < CH_MIN_LEN) o->ad_ch_len = CH_MIN_LEN;
		o->ad_buf = calloc(o->ad_ch_len * o->ch_count, sizeof(int16_t));
		if (!o->ad_buf)
			return false;
	}
	return true;
}

/*
 * \return true unless error occurred
 */
static bool fini_Player(struct Player *restrict o) {
	free(o->buf);
	free(o->ad_buf);
	if (o->ad != NULL) SGS_close_AudioDev(o->ad);
	if (o->sf != NULL)
		return (SGS_close_SndFile(o->sf) == 0);
	return true;
}

/*
 * Write \p samples from \p buf to raw file. Channels are assumed
 * to be interleaved in the buffer, and the buffer of length
 * (channels * samples).
 *
 * \return true if write successful
 */
static bool raw_audio_write(FILE *restrict f, uint32_t channels,
		const int16_t *restrict buf, uint32_t samples) {
	return samples == fwrite(buf, channels * sizeof(int16_t), samples, f);
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool Player_run(struct Player *restrict o,
		const sauProgram *restrict prg) {
	bool use_stereo = !(o->options & OPT_AUDIO_MONO);
	bool use_stdout = (o->options & OPT_AUDIO_STDOUT);
	bool split_gen = o->ad_buf;
	bool run = !(o->options & OPT_MODE_CHECK);
	bool error = false;
	sauGenerator *gen = NULL, *ad_gen = NULL;
	if (!(gen = sau_create_Generator(prg, o->srate)))
		return false;
	if (split_gen && !(ad_gen = sau_create_Generator(prg, o->ad_srate))) {
		error = true;
		goto ERROR;
	}
	while (run) {
		int16_t *buf = o->buf, *ad_buf = NULL;
		size_t len, ad_len;
		run = sauGenerator_run(gen, buf, o->ch_len, use_stereo, &len);
		if (split_gen) {
			ad_buf = o->ad_buf;
			run |= sauGenerator_run(ad_gen, ad_buf, o->ad_ch_len,
					use_stereo, &ad_len);
		} else {
			ad_buf = o->buf;
			ad_len = len;
		}
		if (o->ad && !SGS_AudioDev_write(o->ad, ad_buf, ad_len)) {
			sau_error(NULL, "system audio write failed");
			error = true;
		}
		if (use_stdout && !raw_audio_write(stdout,
					o->ch_count, buf, len)) {
			sau_error(NULL, "raw audio stdout write failed");
			error = true;
		}
		if (o->sf && !SGS_SndFile_write(o->sf, buf, len)) {
			sau_error(NULL, "%s file write failed",
					SGS_SndFile_formats[
					(o->options & OPT_AUFILE_STDOUT) ?
					SGS_SNDFILE_AU :
					SGS_SNDFILE_WAV]);
			error = true;
		}
	}
ERROR:
	sau_destroy_Generator(gen);
	sau_destroy_Generator(ad_gen);
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
static bool play(const sauProgramArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	struct Player out;
	bool status = true;
	if (!init_Player(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen = out.ad_buf;
	if (split_gen) sau_warning(NULL,
			"generating audio twice, using different sample rates");
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const sauProgram *prg = prg_objs->a[i];
		if (!prg) continue;
		if ((options & OPT_PRINT_INFO) != 0)
			sauProgram_print_info(prg);
		if ((options & OPT_PRINT_VERBOSE) != 0)
			sau_printf((options & OPT_MODE_CHECK) != 0 ?
					"Checked \"%s\".\n" :
					"Playing \"%s\".\n", prg->name);
		if (!Player_run(&out, prg))
			status = false;
	}

CLEANUP:
	if (!fini_Player(&out))
		status = false;
	return status;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	sauScriptPredefArr predef_args = {0};
	sauScriptArgArr script_args = {0};
	sauProgramArr prg_objs = {0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = 0;
	if (!parse_args(argc, argv, &options, &script_args, &predef_args,
				&wav_path, &srate))
		return 0;
	bool error = !read_scripts(&script_args, &prg_objs);
	sauScriptPredefArr_clear(&predef_args);
	sauScriptArgArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		error = !play(&prg_objs, srate, options, wav_path);
		discard(&prg_objs);
		if (error)
			return 1;
	}
	return 0;
}
