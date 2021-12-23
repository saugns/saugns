/* sgensys: WAV file writer module.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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

struct SGS_WAVFile {
	FILE *f;
	uint16_t channels;
	uint32_t samples;
};

/**
 * Create 16-bit WAV file for audio output. Sound data may thereafter be
 * written any number of times using SGS_WAVFile_write().
 *
 * \return instance or NULL if fopen fails
 */
SGS_WAVFile *SGS_create_WAVFile(const char *fpath, uint16_t channels,
		uint32_t srate) {
	FILE *f = fopen(fpath, "wb");
	if (!f) {
		SGS_error(NULL, "couldn't open WAV file \"%s\" for writing",
			fpath);
		return NULL;
	}
	SGS_WAVFile *o = malloc(sizeof(SGS_WAVFile));
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
 * Write \p samples from \p buf to WAV file. Channels are assumed
 * to be interleaved in the buffer, and the buffer of length
 * (channels * samples).
 *
 * \return true if write successful
 */
bool SGS_WAVFile_write(SGS_WAVFile *o, const int16_t *buf, uint32_t samples) {
	uint32_t written;
	written = fwrite(buf, o->channels * SOUND_BYTES, samples, o->f);
	o->samples += written;
	return (written == samples);
}

/**
 * Close file and destroy instance.
 *
 * Updates the WAV file header with the total length/size of
 * audio data written.
 *
 * \return value of ferror, checked before closing file
 */
int SGS_close_WAVFile(SGS_WAVFile *o) {
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
