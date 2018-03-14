/* sgensys: System audio output support module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
#include "sgensys.h"

struct SGS_AudioDev;
typedef struct SGS_AudioDev *SGS_AudioDev_t;

SGS_AudioDev_t SGS_open_audiodev(uint16_t channels, uint32_t *srate);
void SGS_close_audiodev(SGS_AudioDev_t o);

uint32_t SGS_audiodev_get_srate(SGS_AudioDev_t o);
bool SGS_audiodev_write(SGS_AudioDev_t o, const int16_t *buf, uint32_t samples);
