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

#pragma once
#include "math.h"

extern void MGS_Osc_init(void);

#define MGS_Osc_TABLEN 1024
#define MGS_Osc_TABINDEXBITS 10
#define MGS_Osc_TABINDEXMASK ((1<<(32-MGS_Osc_TABINDEXBITS))-1)

/* for different waveforms (use short* as pointer type) */
typedef short MGS_OscTab[MGS_Osc_TABLEN+1]; /* one extra for no-check lerp */
extern MGS_OscTab MGS_Osc_sin,
                 MGS_Osc_sqr,
                 MGS_Osc_tri,
                 MGS_Osc_saw;

typedef struct MGS_Osc {
  uint32_t phase;
} MGS_Osc;

#define MGS_Osc_COEFF(sr) \
  (4294967296.0/(sr))

#define MGS_Osc_PHASE(p) \
  ((uint32_t)((p) * 4294967296.0))

#define MGS_Osc_GET_PHASE(o) \
  ((uint32_t)((o)->phase))

#define MGS_Osc_SET_PHASE(o, p) \
  ((void)((o)->phase = (p)))

#define MGS_Osc_RUN(o, osctab, coeff, freq, amp, out) do{ \
  int MGS_Osc__s; \
  uint32_t MGS_Osc__i; \
  MGS_Osc__i = lrint((coeff)*(freq)); \
  (o)->phase += MGS_Osc__i; \
  MGS_Osc__i = (o)->phase >> (32-MGS_Osc_TABINDEXBITS); \
  MGS_Osc__s = (osctab)[MGS_Osc__i]; \
  /* write lerp'd & scaled result */ \
  (out) = lrint( \
    (((float)MGS_Osc__s) + \
     ((float)(((osctab)[MGS_Osc__i + 1] - MGS_Osc__s))) * \
     ((float)((o)->phase & MGS_Osc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGS_Osc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

#define MGS_Osc_RUN_FM(o, osctab, coeff, freq, fm, amp, out) do{ \
  int MGS_Osc__s; \
  uint32_t MGS_Osc__i; \
  MGS_Osc__i = lrint((coeff)*(freq)); \
  (o)->phase += MGS_Osc__i + ((fm) * ((MGS_Osc__i >> 11) - (MGS_Osc__i >> 14) + (MGS_Osc__i >> 18))); \
  MGS_Osc__i = (o)->phase >> (32-MGS_Osc_TABINDEXBITS); \
  MGS_Osc__s = (osctab)[MGS_Osc__i]; \
  /* write lerp'd & scaled result */ \
  (out) = lrint( \
    (((float)MGS_Osc__s) + \
     ((float)(((osctab)[MGS_Osc__i + 1] - MGS_Osc__s))) * \
     ((float)((o)->phase & MGS_Osc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGS_Osc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

#define MGS_Osc_RUN_PM(o, osctab, coeff, freq, pm, amp, out) do{ \
  int MGS_Osc__s; \
  uint32_t MGS_Osc__i, MGS_Osc__p; \
  MGS_Osc__i = lrint((coeff)*(freq)); \
  (o)->phase += MGS_Osc__i; \
  MGS_Osc__p = (o)->phase + ((pm) << 16); \
  MGS_Osc__i = MGS_Osc__p >> (32-MGS_Osc_TABINDEXBITS); \
  MGS_Osc__s = (osctab)[MGS_Osc__i]; \
  /* write lerp'd & scaled result */ \
  (out) = lrint( \
    (((float)MGS_Osc__s) + \
     ((float)(((osctab)[MGS_Osc__i + 1] - MGS_Osc__s))) * \
     ((float)(MGS_Osc__p & MGS_Osc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGS_Osc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

/* Gives output in 0.0 to 1.0 range for AM use as envelope */
#define MGS_Osc_RUN_PM_ENVO(o, osctab, coeff, freq, pm, out) do{ \
  int MGS_Osc__s; \
  uint32_t MGS_Osc__i, MGS_Osc__p; \
  MGS_Osc__i = lrint((coeff)*(freq)); \
  (o)->phase += MGS_Osc__i; \
  MGS_Osc__p = (o)->phase + ((pm) << 16); \
  MGS_Osc__i = MGS_Osc__p >> (32-MGS_Osc_TABINDEXBITS); \
  MGS_Osc__s = (osctab)[MGS_Osc__i]; \
  /* write lerp'd & scaled result */ \
  (out) = (((float)MGS_Osc__s) + \
           ((float)(((osctab)[MGS_Osc__i + 1] - MGS_Osc__s))) * \
           ((float)(MGS_Osc__p & MGS_Osc_TABINDEXMASK)) * \
           (1.f / (1 << (32-MGS_Osc_TABINDEXBITS)))) * \
          (1.f / ((1 << 16) - 2)) + \
          .5f; \
}while(0)

#define MGS_Osc_WAVE_OFFS(o, coeff, freq, timepos, out) do{ \
  uint32_t MGS_Osc__i; \
  MGS_Osc__i = lrint((coeff)*(freq)); \
  uint32_t MGS_Osc__p = MGS_Osc__i * (uint32_t)(timepos); \
  int MGS_Osc__o = MGS_Osc__p - (MGS_Osc_TABINDEXMASK+1); \
  (out) = (MGS_Osc__o / MGS_Osc__i); \
}while(0)

/**/
