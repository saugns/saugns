/* mgensys: Common header / Command-line interface.
 * Copyright (c) 2011, 2020-2022 Joel K. Pettersson
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
#include "ptrarr.h"

/*
 * Configuration options.
 */

#define MGS_CLINAME_STR "mgensys"
#define MGS_VERSION_STR "v0.1-dev"

#define MGS_DEFAULT_SRATE 96000

/**
 * Command line options flags.
 */
enum {
	MGS_OPT_MODE_FULL     = 1<<0,
	MGS_OPT_SYMGS_ENABLE  = 1<<1,
	MGS_OPT_SYMGS_DISABLE = 1<<2,
	MGS_OPT_AUDIO_MONO    = 1<<3,
	MGS_OPT_AUDIO_STDOUT  = 1<<4,
	MGS_OPT_AUFILE_STDOUT = 1<<5,
	MGS_OPT_MODE_CHECK    = 1<<6,
	MGS_OPT_PRINT_INFO    = 1<<7,
	MGS_OPT_EVAL_STRING   = 1<<8,
	MGS_OPT_PRINT_VERBOSE = 1<<9,
};

#if MGS_ADD_TESTOPT
extern int mgs_testopt; /* defaults to 0, set using debug option "-?" */
#endif

/*
 * Command-line interface functions.
 */

size_t mgs_build(const mgsPtrArr *restrict script_args, uint32_t options,
		mgsPtrArr *restrict prg_objs);

bool mgs_play(const mgsPtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path);

void mgs_discard(mgsPtrArr *restrict prg_objs);

/*
 * mgsProgram
 */

struct mgsProgram;
typedef struct mgsProgram mgsProgram;

mgsProgram* mgs_create_Program(const char *file, bool is_path);
void mgs_destroy_Program(mgsProgram *o);

/*
 * mgsGenerator
 */

struct mgsGenerator;
typedef struct mgsGenerator mgsGenerator;

mgsGenerator* mgs_create_Generator(const mgsProgram *prg, uint32_t srate);
void mgs_destroy_Generator(mgsGenerator *o);
bool mgsGenerator_run(mgsGenerator *o, int16_t *buf, uint32_t len,
		bool stereo, uint32_t *gen_len);
