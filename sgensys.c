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
#include "arrtype.h"
#include "file.h"
#include "generator.h"
#include "player/audiodev.h"
#include "player/wavfile.h"
#include "math.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

struct SGS_ScriptArg {
	const char *str;
};
sgsArrType(SGS_ScriptArgArr, struct SGS_ScriptArg, )
sgsArrType(SGS_ProgramArr, SGS_Program*, )

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: sgensys [-a|-m] [-r <srate>] [-p] [-o <wavfile>] [-e] <script>...\n"
"       sgensys [-c] [-p] [-e] <script>...\n"
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
		uint32_t *restrict flags,
		SGS_ScriptArgArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	int i;
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
			struct SGS_ScriptArg entry = {arg};
			SGS_ScriptArgArr_add(script_args, &entry);
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
	return (script_args->count != 0);
INVALID:
	print_usage(false);
CLEAR:
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
size_t SGS_read(const SGS_ScriptArgArr *restrict script_args, bool are_paths,
		SGS_ProgramArr *restrict prg_objs) {
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
static void discard_programs(SGS_ProgramArr *restrict prg_objs) {
	for (size_t i = 0; i < prg_objs->count; ++i) {
		SGS_discard_Program(prg_objs->a[i]);
	}
	SGS_ProgramArr_clear(prg_objs);
}

/*
 * Process the listed scripts.
 *
 * \return true if at least one script succesfully built
 */
static bool read(const SGS_ScriptArgArr *restrict script_args,
		SGS_ProgramArr *restrict prg_objs,
		uint32_t options) {
	bool are_paths = !(options & ARG_EVAL_STRING);
	if (!SGS_read(script_args, are_paths, prg_objs))
		return false;
	if ((options & ARG_PRINT_INFO) != 0) {
		for (size_t i = 0; i < prg_objs->count; ++i) {
			const SGS_Program *prg = prg_objs->a[i];
			if (prg != NULL) SGS_Program_print_info(prg);
		}
	}
	if ((options & ARG_ONLY_COMPILE) != 0) {
		discard_programs(prg_objs);
	}
	return true;
}

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1
#define NUM_CHANNELS 2

typedef struct SGS_Output {
	SGS_AudioDev *ad;
	SGS_WAVFile *wf;
	uint32_t ad_srate;
	int16_t *buf;
	size_t buf_len;
	size_t ch_len;
} SGS_Output;

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SGS_init_Output(SGS_Output *restrict o, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path) {
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (SGS_Output){0};
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
	bool run;
	use_audiodev = use_audiodev && (o->ad != NULL);
	use_wavfile = use_wavfile && (o->wf != NULL);
	do {
		run = SGS_Generator_run(gen, o->buf, o->ch_len, &len);
		if (use_audiodev && !SGS_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			SGS_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !SGS_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			SGS_error(NULL, "WAV file write failed");
		}
	} while (run);
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
		bool use_audiodev, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	SGS_Output out;
	bool status = true;
	if (!SGS_init_Output(&out, srate, use_audiodev, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	if (out.ad != NULL && out.wf != NULL && (out.ad_srate != srate)) {
		SGS_warning(NULL,
"generating audio twice, using different sample rates");
		for (size_t i = 0; i < prg_objs->count; ++i) {
			const SGS_Program *prg = prg_objs->a[i];
			if (!prg) continue;
			if (!SGS_Output_run(&out, prg, out.ad_srate,
						true, false))
				status = false;
			if (!SGS_Output_run(&out, prg, srate,
						false, true))
				status = false;
		}
	} else {
		if (out.ad != NULL) srate = out.ad_srate;
		for (size_t i = 0; i < prg_objs->count; ++i) {
			const SGS_Program *prg = prg_objs->a[i];
			if (!prg) continue;
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

/*
 * Produce results from the list of programs, ignoring NULL entries.
 *
 * \return true unless error occurred
 */
static bool play(const SGS_ProgramArr *restrict prg_objs,
		uint32_t srate, uint32_t options,
		const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & ARG_ENABLE_AUDIO_DEV) != 0) :
		((options & ARG_DISABLE_AUDIO_DEV) == 0);
	return SGS_play(prg_objs, srate, use_audiodev, wav_path);
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SGS_ScriptArgArr script_args = (SGS_ScriptArgArr){0};
	SGS_ProgramArr prg_objs = (SGS_ProgramArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = SGS_DEFAULT_SRATE;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate))
		return 0;
	bool error = !read(&script_args, &prg_objs, options);
	SGS_ScriptArgArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		error = !play(&prg_objs, srate, options, wav_path);
		discard_programs(&prg_objs);
		if (error)
			return 1;
	}

	return 0;
}
