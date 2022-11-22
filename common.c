/* mgensys: Common definitions.
 * Copyright (c) 2011-2012, 2019-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int MGS_stdout_busy = 0; /* enable if stdout is given other uses! */

/**
 * Wrapper for vfprintf(), which prints to either stdout or stderr
 * depending on \ref MGS_print_stream().
 */
int MGS_printf(const char *restrict fmt, ...) {
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vfprintf(MGS_print_stream(), fmt, ap);
	va_end(ap);
	return ret;
}

/*
 * Print to stderr. message, optionally including a descriptive label.
 *  - \p msg_type may be e.g. "warning", "error"
 *  - \p msg_label may be NULL or a label to add within square brackets
 */
static void print_stderr(const char *restrict msg_type,
		const char *restrict msg_label,
		const char *restrict fmt, va_list ap) {
	if (msg_label) {
		fprintf(stderr, "%s [%s]: ", msg_type, msg_label);
	} else {
		fprintf(stderr, "%s: ", msg_type);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message. If \p label is not NULL, it will be
 * added after "warning" within square brackets.
 */
void MGS_warning(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("warning", label, fmt, ap);
	va_end(ap);
}

/**
 * Print error message. If \p label is not NULL, it will be
 * added after "error" within square brackets.
 */
void MGS_error(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("error", label, fmt, ap);
	va_end(ap);
}

/**
 * Allocates memory of \p size and copies that many bytes
 * from \p src to it.  If \p src is NULL, a zero'd memory
 * block will instead be returned.
 *
 * \return new allocation or NULL on failure
 */
void *MGS_memdup(const void *restrict src, size_t size) {
	if (!size)
		return NULL;
	if (!src)
		return calloc(1, size);
	void *dst = malloc(size);
	if (!dst)
		return NULL;
	memcpy(dst, src, size);
	return dst;
}

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

/**
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
int MGS_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct MGS_opt *restrict opt) {
	(void)argc;
	if (opt->ind == 0) {
		opt->ind = 1;
		opt->pos = 1;
	}
	const char *arg = argv[opt->ind], *subs;
	if (!arg || arg[0] != '-' || !MGS_IS_ASCIIVISIBLE(arg[1]))
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
