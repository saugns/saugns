/* mgensys: Oscillator module.
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

#define MGS_Osc_TABSCALE ((float)((1<<15) - 1))
#define HALFLEN (MGS_Osc_TABLEN>>1)

MGS_OscTab MGS_Osc_sin,
          MGS_Osc_sqr,
          MGS_Osc_tri,
          MGS_Osc_saw;

void MGS_Osc_init(void) {
  int i;
  static bool done = 0;
  if (done) return;
  done = 1;

  /* first half */
  for (i = 0; i < HALFLEN; ++i) {
    MGS_Osc_sin[i] = MGS_Osc_TABSCALE * sin(PI * i/HALFLEN);
    MGS_Osc_sqr[i] = MGS_Osc_TABSCALE;
    if (i < (HALFLEN>>1))
      MGS_Osc_tri[i] = MGS_Osc_TABSCALE * (2.f * i/HALFLEN);
    else
      MGS_Osc_tri[i] = MGS_Osc_TABSCALE * (2.f * (HALFLEN-i)/HALFLEN);
    MGS_Osc_saw[i] = MGS_Osc_TABSCALE * (1.f * (HALFLEN-i)/HALFLEN);
  }
  /* second half */
  for (; i < MGS_Osc_TABLEN; ++i) {
    MGS_Osc_sin[i] = -MGS_Osc_sin[i - HALFLEN];
    MGS_Osc_sqr[i] = -MGS_Osc_sqr[i - HALFLEN];
    MGS_Osc_tri[i] = -MGS_Osc_tri[i - HALFLEN];
    MGS_Osc_saw[i] = -MGS_Osc_saw[MGS_Osc_TABLEN - i];
  }
  /* wrap value */
  MGS_Osc_sin[MGS_Osc_TABLEN] = MGS_Osc_sin[0];
  MGS_Osc_sqr[MGS_Osc_TABLEN] = MGS_Osc_sqr[0];
  MGS_Osc_tri[MGS_Osc_TABLEN] = MGS_Osc_tri[0];
  MGS_Osc_saw[MGS_Osc_TABLEN] = MGS_Osc_saw[0];
}
