/* saugns: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2019 Joel K. Pettersson
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

struct SAU_AudioDev;
typedef struct SAU_AudioDev SAU_AudioDev;

SAU_AudioDev *SAU_open_AudioDev(uint16_t channels, uint32_t *restrict srate)
		sauMalloclike;
void SAU_close_AudioDev(SAU_AudioDev *restrict o);

uint32_t SAU_AudioDev_get_srate(const SAU_AudioDev *restrict o);
bool SAU_AudioDev_write(SAU_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples);
