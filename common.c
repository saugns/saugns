/* ssndgen: Common definitions.
 * Copyright (c) 2011-2012, 2019-2021 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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
void SSG_warning(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("warning", label, fmt, ap);
	va_end(ap);
}

/**
 * Print error message. If \p label is not NULL, it will be
 * added after "error" within square brackets.
 */
void SSG_error(const char *restrict label, const char *restrict fmt, ...) {
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
void *SSG_memdup(const void *restrict src, size_t size) {
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

/**
 * Command-line argument parser similar to POSIX getopt(),
 * but replacing opt* global variables with \p opt fields.
 *
 * The \a arg field is always set for each valid option, so as to be
 * available for reading as an unspecified optional option argument.
 *
 * In large part based on the public domain
 * getopt() version by Christopher Wellons.
 */
int SSG_getopt(int argc, char *const*restrict argv,
		const char *restrict optstring, struct SSG_opt *restrict opt) {
	(void)argc;
	if (opt->ind == 0) {
		opt->ind = 1;
		opt->pos = 1;
	}
	const char *arg = argv[opt->ind];
	if (!arg || arg[0] != '-' || !SSG_IS_ASCIIVISIBLE(arg[1]))
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
