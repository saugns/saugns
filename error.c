/* sgensys: Common definitions.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
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
#include <stdio.h>
#include <stdarg.h>

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
void SGS_warning(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("warning", label, fmt, ap);
	va_end(ap);
}

/**
 * Print error message. If \p label is not NULL, it will be
 * added after "error" within square brackets.
 */
void SGS_error(const char *restrict label, const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr("error", label, fmt, ap);
	va_end(ap);
}
