/* mgensys: Oscillator module (individually licensed)
 * Copyright (c) 2011, 2020 Joel K. Pettersson
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

#include "mgensys.h"
#include "osc.h"

#define MGSOsc_TABSCALE ((float)((1<<15) - 1))
#define HALFLEN (MGSOsc_TABLEN>>1)

MGSOscTab MGSOsc_sin,
          MGSOsc_sqr,
          MGSOsc_tri,
          MGSOsc_saw;

void MGSOsc_init(void) {
  int i;
  static uchar done = 0;
  if (done) return;
  done = 1;

  /* first half */
  for (i = 0; i < HALFLEN; ++i) {
    MGSOsc_sin[i] = MGSOsc_TABSCALE * sin(PI * i/HALFLEN);
    MGSOsc_sqr[i] = MGSOsc_TABSCALE;
    if (i < (HALFLEN>>1))
      MGSOsc_tri[i] = MGSOsc_TABSCALE * (2.f * i/HALFLEN);
    else
      MGSOsc_tri[i] = MGSOsc_TABSCALE * (2.f * (HALFLEN-i)/HALFLEN);
    MGSOsc_saw[i] = MGSOsc_TABSCALE * (1.f * (HALFLEN-i)/HALFLEN);
  }
  /* second half */
  for (; i < MGSOsc_TABLEN; ++i) {
    MGSOsc_sin[i] = -MGSOsc_sin[i - HALFLEN];
    MGSOsc_sqr[i] = -MGSOsc_sqr[i - HALFLEN];
    MGSOsc_tri[i] = -MGSOsc_tri[i - HALFLEN];
    MGSOsc_saw[i] = -MGSOsc_saw[MGSOsc_TABLEN - i];
  }
  /* wrap value */
  MGSOsc_sin[MGSOsc_TABLEN] = MGSOsc_sin[0];
  MGSOsc_sqr[MGSOsc_TABLEN] = MGSOsc_sqr[0];
  MGSOsc_tri[MGSOsc_TABLEN] = MGSOsc_tri[0];
  MGSOsc_saw[MGSOsc_TABLEN] = MGSOsc_saw[0];
}
