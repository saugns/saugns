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

#include "sndfile.h"
#include <stdio.h>
#include <stdlib.h>

const char *const MGS_SndFile_formats[MGS_SNDFILE_FORMATS] = {
	"raw",
	"AU",
	"WAV"
};

static void fputw(uint16_t i16, FILE *restrict stream) {
	putc(i16 & 0xff, stream);
	putc((i16 >>= 8) & 0xff, stream);
}

static void fputl(uint32_t i32, FILE *restrict stream) {
	putc(i32 & 0xff, stream);
	putc((i32 >>= 8) & 0xff, stream);
	putc((i32 >>= 8) & 0xff, stream);
	putc((i32 >>= 8) & 0xff, stream);
}

//static void fputw_be(uint16_t i16, FILE *restrict stream) {
//	putc((i16 >> 8) & 0xff, stream);
//	putc(i16 & 0xff, stream);
//}

static void fputl_be(uint32_t i32, FILE *restrict stream) {
	putc((i32 >> 24) & 0xff, stream);
	putc((i32 >> 16) & 0xff, stream);
	putc((i32 >> 8) & 0xff, stream);
	putc(i32 & 0xff, stream);
}

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

struct MGS_SndFile {
	FILE *f;
	unsigned format;
	uint16_t channels;
	uint64_t samples;
	bool is_subfile, needs_conv;
};

static void write_au_header(MGS_SndFile *restrict o, uint32_t srate) {
	FILE *f = o->f;
	fputs(".snd", f);
	fputl_be(28, f);
	fputl_be(0xffffffff /* size: unspecified */, f);
	fputl_be(3 /* format: 16-bit integer */, f);
	fputl_be(srate /* sample rate */, f);
	fputl_be(o->channels /* channel count */, f);
	fputl_be(0, f);
}

static void update_au_header(MGS_SndFile *restrict o) {
	FILE *f = o->f;
	if (o->samples >= UINT32_MAX)
		return;
	fseek(f, 8 /* size */, SEEK_SET);
	fputl_be(o->samples, f);
}

static void write_wav_header(MGS_SndFile *restrict o, uint32_t srate) {
	FILE *f = o->f;
	fputs("RIFF", f);
	fputl(36 /* update adding audio data size later */, f);
	fputs("WAVE", f);

	fputs("fmt ", f);
	fputl(16, f); /* fmt-chunk size */
	fputw(1, f); /* format */
	fputw(o->channels, f);
	fputl(srate, f); /* sample rate */
	fputl(o->channels * srate * SOUND_BYTES, f); /* byte rate */
	fputw(o->channels * SOUND_BYTES, f); /* block align */
	fputw(SOUND_BITS, f); /* bits per sample */

	fputs("data", f);
	fputl(0 /* updated with data size later */, f); /* fmt-chunk size */
}

static void update_wav_header(MGS_SndFile *restrict o) {
	FILE *f = o->f;
	uint32_t bytes = o->channels * o->samples * SOUND_BYTES;
	fseek(f, 4 /* after "RIFF" */, SEEK_SET);
	fputl(36 + bytes, f);

	fseek(f, 32 /* after "data" */, SEEK_CUR);
	fputl(bytes, f); /* fmt-chunk size */
}

static void cleanup(MGS_SndFile *restrict o) {
	if (!o)
		return;
	if (!o->is_subfile)
		fclose(o->f);
	free(o);
}

/**
 * Create 16-bit sound file for output. Sound data may thereafter be
 * written any number of times using MGS_SndFile_write().
 *
 * \return instance or NULL on error
 */
MGS_SndFile *MGS_create_SndFile(const char *restrict fpath, unsigned format,
		uint16_t channels, uint32_t srate) {
	bool is_subfile = !fpath;
	FILE *f = stdout;
	if (!is_subfile && !(f = fopen(fpath, "wb"))) {
		MGS_error(NULL, "couldn't open %s file \"%s\" for writing",
			MGS_SndFile_formats[format], fpath);
		return NULL;
	}
	MGS_SndFile *o = malloc(sizeof(*o));
	if (!o) goto ERROR;
	o->f = f;
	o->format = format;
	o->channels = channels;
	o->samples = 0;
	o->is_subfile = is_subfile;
	o->needs_conv = false;

	switch (format) {
	case MGS_SNDFILE_RAW:
		break;
	case MGS_SNDFILE_AU:
		o->needs_conv = true;
		write_au_header(o, srate);
		break;
	case MGS_SNDFILE_WAV:
		write_wav_header(o, srate);
		break;
	}
	return o;
ERROR:
	if (!is_subfile) fclose(f);
	return NULL;
}

static void convert_endian(MGS_SndFile *restrict o,
		int16_t *restrict buf, uint32_t samples) {
	uint32_t i, maxlen = o->channels * samples;
	for (i = 0; i < maxlen; ++i) {
		uint16_t s = buf[i];
		s = (s >> 8) | (s << 8);
		buf[i] = s;
	}
}

/**
 * Write \p samples from \p buf to sound file. Channels are assumed
 * to be interleaved in the buffer, and the buffer of length
 * (channels * samples).
 *
 * Warning: Will perform endian conversion in place in \p buf if needed!
 *
 * \return true if write successful
 */
bool MGS_SndFile_write(MGS_SndFile *restrict o,
		int16_t *restrict buf, uint32_t samples) {
	uint32_t written;
	if (o->needs_conv)
		convert_endian(o, buf, samples);
	written = fwrite(buf, o->channels * SOUND_BYTES, samples, o->f);
	o->samples += written;
	return (written == samples);
}

/**
 * Close sound file and destroy instance,
 * or simply clean up if it's a stream subfile.
 *
 * Updates the file header with the total length/size of
 * audio data written, if to be done for the file type.
 *
 * \return value of ferror, checked before closing file
 */
int MGS_close_SndFile(MGS_SndFile *restrict o) {
	int err;

	if (!o->is_subfile) switch (o->format) {
	case MGS_SNDFILE_RAW:
		break;
	case MGS_SNDFILE_AU:
		update_au_header(o);
		break;
	case MGS_SNDFILE_WAV:
		update_wav_header(o);
		break;
	}

	err = ferror(o->f);
	cleanup(o);
	return err;
}
