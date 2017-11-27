/* sgensys: system audio output support module.
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

struct SGSAudioDev;
typedef struct SGSAudioDev SGSAudioDev;

SGSAudioDev *SGS_open_audiodev(uint16_t channels, uint32_t *srate);
void SGS_close_audiodev(SGSAudioDev *ad);

uint32_t SGS_audiodev_get_srate(const SGSAudioDev *ad);
bool SGS_audiodev_write(SGSAudioDev *ad, const int16_t *buf, uint32_t samples);
