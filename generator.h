/* sgensys: sound generator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"

/*
 * SGSGenerator
 */

struct SGSGenerator;
typedef struct SGSGenerator *SGSGenerator_t;

SGSGenerator_t SGS_create_generator(SGSProgram_t prg, uint32_t srate);
void SGS_destroy_generator(SGSGenerator_t o);

bool SGS_generator_run(SGSGenerator_t o, int16_t *buf, size_t buf_len,
                        size_t *gen_len);
