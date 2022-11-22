/* mgensys: Sound file writer module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#pragma once
#include "../common.h"

enum {
	MGS_SNDFILE_RAW = 0,
	MGS_SNDFILE_AU,
	MGS_SNDFILE_WAV,
	MGS_SNDFILE_FORMATS
};

struct MGS_SndFile;
typedef struct MGS_SndFile MGS_SndFile;

MGS_SndFile *MGS_create_SndFile(const char *restrict fpath, unsigned format,
		uint16_t channels, uint32_t srate) mgsMalloclike;
int MGS_close_SndFile(MGS_SndFile *restrict o);

bool MGS_SndFile_write(MGS_SndFile *restrict o,
		int16_t *restrict buf, uint32_t samples);

extern const char *const MGS_SndFile_formats[MGS_SNDFILE_FORMATS];
