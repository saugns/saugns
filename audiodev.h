/* ssndgen: System audio output support module.
 * Copyright (c) 2011-2014, 2017-2020 Joel K. Pettersson
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
#include "common.h"

struct SSG_AudioDev;
typedef struct SSG_AudioDev SSG_AudioDev;

SSG_AudioDev *SSG_open_AudioDev(uint16_t channels, uint32_t *restrict srate)
		SSG__malloclike;
void SSG_close_AudioDev(SSG_AudioDev *restrict o);

uint32_t SSG_AudioDev_get_srate(const SSG_AudioDev *restrict o);
bool SSG_AudioDev_write(SSG_AudioDev *restrict o,
		const int16_t *restrict buf, uint32_t samples);
