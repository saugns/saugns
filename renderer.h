/* sgensys: Audio renderer module
 * Copyright (c) 2011-2014, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "sgensys.h"

/*
 * SGSRenderer
 */

struct SGSRenderer;
typedef struct SGSRenderer SGSRenderer;

SGSRenderer* SGS_create_renderer(uint32_t srate, SGSResult_t res);
void SGS_destroy_renderer(SGSRenderer *o);

bool SGS_renderer_run(SGSRenderer *o, int16_t *buf, size_t buf_len,
		size_t *out_len);
