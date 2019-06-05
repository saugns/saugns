/* sgensys: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2018 Joel K. Pettersson
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
#include "common.h"

struct SGS_AudioDev;
typedef struct SGS_AudioDev SGS_AudioDev;

SGS_AudioDev *SGS_open_AudioDev(uint16_t channels, uint32_t *srate);
void SGS_close_AudioDev(SGS_AudioDev *ad);

uint32_t SGS_AudioDev_get_srate(const SGS_AudioDev *ad);
bool SGS_AudioDev_write(SGS_AudioDev *ad, const int16_t *buf, uint32_t samples);
