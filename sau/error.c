/* SAU library: Common definitions.
 * Copyright (c) 2011-2012, 2018-2022 Joel K. Pettersson
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

#include <sau/common.h>
#include <stdio.h>
#include <stdarg.h>

int SAU_stdout_busy = 0; /* enable if stdout is given other uses! */

/**
 * Wrapper for vfprintf(), which prints to either stdout or stderr
 * depending on \ref SAU_print_stream().
 */
int SAU_printf(const char *restrict fmt, ...) {
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vfprintf(SAU_print_stream(), fmt, ap);
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
void SAU_warning(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("warning", label, fmt, ap);
	va_end(ap);
}

/**
 * Print error message. If \p label is not NULL, it will be
 * added after "error" within square brackets.
 */
void SAU_error(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("error", label, fmt, ap);
	va_end(ap);
}
