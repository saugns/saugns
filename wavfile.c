/* sgensys: WAV file writer module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "wavfile.h"
#include <stdio.h>
#include <stdlib.h>

static void fputw(uint16_t i16, FILE *stream) {
	uint8_t b;
	b = i16 & 0xff;
	putc(b, stream);
	b = (i16 >>= 8) & 0xff;
	putc(b, stream);
}

static void fputl(uint32_t i32, FILE *stream) {
	uint8_t b;
	b = i32 & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
}

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

struct SGS_WavFile {
	FILE *f;
	uint16_t channels;
	uint32_t samples;
};

/**
 * Create 16-bit WAV file for audio output. Sound data may thereafter be
 * written any number of times using SGS_WavFile_write().
 *
 * Returns instance or NULL if fopen fails.
 */
SGS_WavFile *SGS_create_WavFile(const char *fpath, uint16_t channels,
		uint32_t srate) {
	SGS_WavFile *o;
	FILE *f = fopen(fpath, "wb");
	if (!f) {
		fprintf(stderr, "error: couldn't open WAV file \"%s\" for writing\n",
			fpath);
		return NULL;
	}
	o = malloc(sizeof(SGS_WavFile));
	o->f = f;
	o->channels = channels;
	o->samples = 0;

	fputs("RIFF", f);
	fputl(36 /* update adding audio data size later */, f);
	fputs("WAVE", f);

	fputs("fmt ", f);
	fputl(16, f); /* fmt-chunk size */
	fputw(1, f); /* format */
	fputw(channels, f);
	fputl(srate, f); /* sample rate */
	fputl(channels * srate * SOUND_BYTES, f); /* byte rate */
	fputw(channels * SOUND_BYTES, f); /* block align */
	fputw(SOUND_BITS, f); /* bits per sample */

	fputs("data", f);
	fputl(0 /* updated with data size later */, f); /* fmt-chunk size */

	return o;
}

/**
 * Write the given number of samples from buf to the WAV file, the former
 * assumed to be in the format for which the WAV file was created. If
 * created for multiple channels, buf is assumed to be interleaved and of
 * channels * samples length.
 *
 * Returns true upon successful write, otherwise false.
 */
bool SGS_WavFile_write(SGS_WavFile *o, const int16_t *buf, uint32_t samples) {
	size_t length = o->channels * samples, written;
	written = fwrite(buf, SOUND_BYTES, length, o->f);
	o->samples += written;
	return (written == length);
}

/**
 * Ends the WAV file and destroys the instance. Updates the file
 * header with the length of audio data written.
 *
 * Returns the value of ferror, checked before closing the file.
 */
int SGS_close_WavFile(SGS_WavFile *o) {
	int err;
	FILE *f = o->f;
	uint32_t bytes = o->channels * o->samples * SOUND_BYTES;

	fseek(f, 4 /* after "RIFF" */, SEEK_SET);
	fputl(36 + bytes, f);

	fseek(f, 32 /* after "data" */, SEEK_CUR);
	fputl(bytes, f); /* fmt-chunk size */

	err = ferror(f);
	fclose(f);
	free(o);
	return err;
}
