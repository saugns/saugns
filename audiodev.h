/* mgensys: System audio output support module (individually licensed)
 * Copyright (c) 2011-2014, 2017-2018, 2020 Joel K. Pettersson
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

struct MGSAudioDev;
typedef struct MGSAudioDev MGSAudioDev;

MGSAudioDev *MGS_open_audiodev(uint16_t channels, uint32_t *srate);
void MGS_close_audiodev(MGSAudioDev *ad);

uint32_t MGS_audiodev_get_srate(const MGSAudioDev *ad);
bool MGS_audiodev_write(MGSAudioDev *ad, const int16_t *buf, uint32_t samples);
