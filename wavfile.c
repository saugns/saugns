/* Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "wavfile.h"
#include <stdio.h>
#include <stdlib.h>

static void fputw(short i16, FILE *stream) {
	uchar c;
	c = i16 & 0xff;
	putc(c, stream);
	c = (i16 >>= 8) & 0xff;
	putc(c, stream);
}

static void fputl(int i32, FILE *stream) {
	uchar c;
	c = i32 & 0xff;
	putc(c, stream);
	c = (i32 >>= 8) & 0xff;
	putc(c, stream);
	c = (i32 >>= 8) & 0xff;
	putc(c, stream);
	c = (i32 >>= 8) & 0xff;
	putc(c, stream);
}

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

struct SGSWAVFile {
	FILE *f;
	ushort channels;
	uint samples;
};

/*
 * Create 16-bit WAV file for audio output. Sound data may thereafter be
 * written any number of times using SGS_wav_file_write().
 *
 * Returns NULL if fopen fails.
 */
SGSWAVFile *SGS_begin_wav_file(const char *fpath, ushort channels,
		uint srate) {
	SGSWAVFile *wf;
	FILE *f = fopen(fpath, "wb");
	if (!f) return NULL;
	wf = malloc(sizeof(SGSWAVFile));
	wf->f = f;
	wf->channels = channels;
	wf->samples = 0;

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

	return wf;
}

/*
 * Write the given number of samples from buf to the WAV file, the former
 * assumed to be in the format for which the WAV file was created. If
 * created for multiple channels, buf is assumed to be interleaved and of
 * channels * samples length.
 *
 * Returns zero upon suceessful write, otherwise non-zero.
 */
uchar SGS_wav_file_write(SGSWAVFile *wf, const short *buf, uint samples) {
	size_t length = wf->channels * samples, written;
	written = fwrite(buf, SOUND_BYTES, length, wf->f);
	wf->samples += written;
	return (written != length);
}

/*
 * Properly update the WAV file header with the total length/size of audio
 * data written, and then close the file and destroy the SGSWAVFile structure.
 *
 * Checks ferror before closing the file and finally returns the value
 * returned.
 */
int SGS_end_wav_file(SGSWAVFile *wf) {
	int err;
	FILE *f = wf->f;
	uint channels = wf->channels;
	uint samples = wf->samples;

	fseek(f, 4 /* after "RIFF" */, SEEK_SET);
	fputl(36 + (channels * samples * SOUND_BYTES), f);

	fseek(f, 32 /* after "data" */, SEEK_CUR);
	fputl(channels * samples * SOUND_BYTES, f); /* fmt-chunk size */

	err = ferror(f);
	fclose(f);
	free(wf);
	return err;
}

/**/
