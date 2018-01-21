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
  float freq;
  uint phase;
  float amp;
} MGSOsc;

#define MGSOsc_COEFF(sr) \
  (4294967296.0/(sr))

#define MGSOsc_SET_PHASE(o, p) \
  ((void)((o)->phase = (p)))

#define MGSOsc_SET_FREQ(o, fq) \
  ((void)((o)->freq = (fq)))

#define MGSOsc_SET_AMP(o, r) \
  ((void)((o)->amp = (r)))

#define MGSOsc_RUN(o, osctab, coeff, out) do{ \
  int MGSOsc__s; \
  uint MGSOsc__i; \
  SET_I2F(MGSOsc__i, (coeff)*(o)->freq); \
  (o)->phase += MGSOsc__i; \
  MGSOsc__i = (o)->phase >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)MGSOsc__s) + \
     ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s))) * \
     ((float)((o)->phase & MGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) * \
    (o)->amp \
  ); \
}while(0)

#define MGSOsc_RUN_FM(o, osctab, coeff, fm, out) do{ \
  int MGSOsc__s; \
  uint MGSOsc__i; \
  SET_I2F(MGSOsc__i, (coeff)*(o)->freq); \
  (o)->phase += MGSOsc__i + ((fm) * ((MGSOsc__i >> 11) - (MGSOsc__i >> 14) + (MGSOsc__i >> 18))); \
  MGSOsc__i = (o)->phase >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)MGSOsc__s) + \
     ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s))) * \
     ((float)((o)->phase & MGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) * \
    (o)->amp \
  ); \
}while(0)

#define MGSOsc_RUN_PM(o, osctab, coeff, pm, out) do{ \
  int MGSOsc__s; \
  uint MGSOsc__i, MGSOsc__p; \
  SET_I2F(MGSOsc__i, (coeff)*(o)->freq); \
  (o)->phase += MGSOsc__i; \
  MGSOsc__p = (o)->phase + ((pm) << 16); \
  MGSOsc__i = MGSOsc__p >> (32-MGSOsc_TABINDEXBITS); \
  MGSOsc__s = (osctab)[MGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)MGSOsc__s) + \
     ((float)(((osctab)[MGSOsc__i + 1] - MGSOsc__s))) * \
     ((float)(MGSOsc__p & MGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-MGSOsc_TABINDEXBITS)))) * \
    (o)->amp \
  ); \
}while(0)

#define MGSOsc_WAVE_OFFS(o, coeff, timepos, out) do{ \
  uint MGSOsc__i; \
  SET_I2F(MGSOsc__i, (coeff)*(o)->freq); \
  uint MGSOsc__p = MGSOsc__i * (uint)(timepos); \
  int MGSOsc__o = MGSOsc__p - (MGSOsc_TABINDEXMASK+1); \
  (out) = (MGSOsc__o / MGSOsc__i); \
}while(0)

/**/
