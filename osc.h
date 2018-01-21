#include "math.h"

extern void MGSOsc_init(void);

#define MGSOsc_TABLEN 1024
#define MGSOsc_TABINDEXBITS 10
#define MGSOsc_TABINDEXMASK ((1<<(32-MGSOsc_TABINDEXBITS))-1)

/* for different waveforms (use short* as pointer type) */
typedef short MGSOscTab[MGSOsc_TABLEN+1]; /* one extra for no-check lerp */
extern MGSOscTab MGSOsc_sin,
                 MGSOsc_sqr,
                 MGSOsc_tri,
                 MGSOsc_saw;

typedef struct MGSOsc {
  uint inc, phase;
  ui16_16 amp;
} MGSOsc;

#define MGSOsc_COEFF(fq, sr) \
  ((4294967296.0 * (fq))/(sr))

#define MGSOsc_SET_PHASE(o, p) \
  ((void)((o)->phase = (p)))

#define MGSOsc_SET_COEFF(o, fq, sr) \
  ((void)((o)->inc = MGSOsc_COEFF(fq, sr)))

#define MGSOsc_SET_RANGE(o, r) \
  ((void)((o)->amp = (r)))

#define MGSOsc_RUN(o, osctab, out) do{ \
  int MGSOsc__s, MGSOsc__d; \
  uint MGSOsc__i; \
  (o)->phase += (o)->inc; \
  MGSOsc__i = (o)->phase >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  SET_I2F(MGSOsc__d, \
    ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s)) * \
             ((o)->phase & MGSOsc_TABINDEXMASK) * \
             (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) \
  ); \
/*printf("%d + %d = %d\n", MGSOsc__s, MGSOsc__d, MGSOsc__s + MGSOsc__d);*/ \
  MGSOsc__s += MGSOsc__d; \
  MGSOsc__s *= (o)->amp; \
  MGSOsc__s >>= 16; \
  (out) = MGSOsc__s; \
}while(0)

#define MGSOsc_RUN_FM(o, osctab, fm, out) do{ \
  int MGSOsc__s, MGSOsc__d; \
  uint MGSOsc__i; \
  (o)->phase += (o)->inc + (fm); \
  MGSOsc__i = (o)->phase >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  SET_I2F(MGSOsc__d, \
    ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s)) * \
             ((o)->phase & MGSOsc_TABINDEXMASK) * \
             (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) \
  ); \
/*printf("%d + %d = %d\n", MGSOsc__s, MGSOsc__d, MGSOsc__s + MGSOsc__d);*/ \
  MGSOsc__s += MGSOsc__d; \
  MGSOsc__s *= (o)->amp; \
  MGSOsc__s >>= 16; \
  (out) = MGSOsc__s; \
}while(0)

#define MGSOsc_RUN_PM(o, osctab, pm, out) do{ \
  int MGSOsc__s, MGSOsc__d; \
  uint MGSOsc__i; \
  (o)->phase += (o)->inc; \
  MGSOsc__i = ((o)->phase + (pm)) >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  SET_I2F(MGSOsc__d, \
    ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s)) * \
             ((o)->phase & MGSOsc_TABINDEXMASK) * \
             (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) \
  ); \
/*printf("%d + %d = %d\n", MGSOsc__s, MGSOsc__d, MGSOsc__s + MGSOsc__d);*/ \
  MGSOsc__s += MGSOsc__d; \
  MGSOsc__s *= (o)->amp; \
  MGSOsc__s >>= 16; \
  (out) = MGSOsc__s; \
}while(0)

#define MGSOsc_WAVE_OFFS(o, timepos, out) do{ \
  uint MGSOsc__p = (o)->inc * (uint)(timepos); \
  int MGSOsc__o = MGSOsc__p - (MGSOsc_TABINDEXMASK+1); \
  (out) = (MGSOsc__o / (o)->inc); \
}while(0)

/**/
