/* ssndgen: System audio output support module.
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
#include "common.h"

struct SSG_AudioDev;
typedef struct SSG_AudioDev SSG_AudioDev;

SSG_AudioDev *SSG_open_AudioDev(uint16_t channels, uint32_t *srate);
void SSG_close_AudioDev(SSG_AudioDev *ad);

uint32_t SSG_AudioDev_get_srate(const SSG_AudioDev *ad);
bool SSG_AudioDev_write(SSG_AudioDev *ad, const int16_t *buf, uint32_t samples);
