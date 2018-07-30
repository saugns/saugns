/* sgensys: Audio renderer module
 * Copyright (c) 2011-2014, 2017-2018 Joel K. Pettersson
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

#pragma once
#include "result.h"

/*
 * SGSRenderer
 */

struct SGS_Renderer;
typedef struct SGS_Renderer SGS_Renderer;

SGS_Renderer *SGS_create_Renderer(const SGS_Result *res, uint32_t srate);
void SGS_destroy_Renderer(SGS_Renderer *o);

bool SGS_Renderer_run(SGS_Renderer *o, int16_t *buf, size_t buf_len,
		size_t *out_len);
