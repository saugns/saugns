/* saugns: Common definitions.
 * Copyright (c) 2011-2012, 2018-2019 Joel K. Pettersson
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

/**
 * Allocates memory of \p size and copies that many bytes
 * from \p src to it.  If \p src is NULL, a zero'd memory
 * block will instead be returned.
 *
 * \return new allocation or NULL on failure
 */
void *SAU_memdup(const void *restrict src, size_t size) {
	if (!size) {
		return NULL;
	}
	if (!src) {
		return calloc(1, size);
	}
	void *dst = malloc(size);
	if (!dst) {
		return NULL;
	}
	memcpy(dst, src, size);
	return dst;
}
