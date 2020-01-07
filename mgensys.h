/* mgensys: Common header.
 * Copyright (c) 2011, 2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

bool MGS_render(const MGS_PtrArr *restrict prg_objs, uint32_t srate,
		bool use_audiodev, const char *restrict wav_path);

/*
 * MGS_Program
 */

struct MGS_Program;
typedef struct MGS_Program MGS_Program;

MGS_Program* MGS_create_Program(const char *filename);
void MGS_destroy_Program(MGS_Program *o);

/*
 * MGS_Generator
 */

struct MGS_Generator;
typedef struct MGS_Generator MGS_Generator;

MGS_Generator* MGS_create_Generator(const MGS_Program *prg, uint32_t srate);
void MGS_destroy_Generator(MGS_Generator *o);
bool MGS_Generator_run(MGS_Generator *o, int16_t *buf, uint32_t len,
		uint32_t *gen_len);
