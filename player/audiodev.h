/* mgensys: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2021 Joel K. Pettersson
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
#include "../common.h"

struct MGS_AudioDev;
typedef struct MGS_AudioDev MGS_AudioDev;

MGS_AudioDev *MGS_open_AudioDev(uint16_t channels, uint32_t *restrict srate)
	mgsMalloclike;
void MGS_close_AudioDev(MGS_AudioDev *restrict o);

uint32_t MGS_AudioDev_get_srate(const MGS_AudioDev *restrict o);
bool MGS_AudioDev_write(MGS_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples);
