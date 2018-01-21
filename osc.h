#include "math.h"

extern void SGSOsc_init(void);

#define SGSOsc_TABLEN 1024
#define SGSOsc_TABINDEXBITS 10
#define SGSOsc_TABINDEXMASK ((1<<(32-SGSOsc_TABINDEXBITS))-1)

/* for different waveforms (use short* as pointer type) */
typedef short SGSOscTab[SGSOsc_TABLEN+1]; /* one extra for no-check lerp */
extern SGSOscTab SGSOsc_sin,
                 SGSOsc_sqr,
                 SGSOsc_tri,
                 SGSOsc_saw;

typedef struct SGSOsc {
  uint phase;
} SGSOsc;

#define SGSOsc_COEFF(sr) \
  (4294967296.0/(sr))

#define SGSOsc_PHASE(p) \
  ((uint)((p) * 4294967296.0))

#define SGSOsc_GET_PHASE(o) \
  ((uint)((o)->phase))

#define SGSOsc_SET_PHASE(o, p) \
  ((void)((o)->phase = (p)))

#define SGSOsc_RUN(o, osctab, coeff, freq, amp, out) do{ \
  int SGSOsc__s; \
  uint SGSOsc__i; \
  SET_I2F(SGSOsc__i, (coeff)*(freq)); \
  (o)->phase += SGSOsc__i; \
  SGSOsc__i = (o)->phase >> (32-SGSOsc_TABINDEXBITS); \
  SGSOsc__s = (osctab)[SGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)SGSOsc__s) + \
     ((float)(((osctab)[SGSOsc__i + 1] - SGSOsc__s))) * \
     ((float)((o)->phase & SGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-SGSOsc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

#define SGSOsc_RUN_FM(o, osctab, coeff, freq, fm, amp, out) do{ \
  int SGSOsc__s; \
  uint SGSOsc__i; \
  SET_I2F(SGSOsc__i, (coeff)*(freq)); \
  (o)->phase += SGSOsc__i + ((fm) * ((SGSOsc__i >> 11) - (SGSOsc__i >> 14) + (SGSOsc__i >> 18))); \
  SGSOsc__i = (o)->phase >> (32-SGSOsc_TABINDEXBITS); \
  SGSOsc__s = (osctab)[SGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)SGSOsc__s) + \
     ((float)(((osctab)[SGSOsc__i + 1] - SGSOsc__s))) * \
     ((float)((o)->phase & SGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-SGSOsc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

#define SGSOsc_RUN_PM(o, osctab, coeff, freq, pm, amp, out) do{ \
  int SGSOsc__s; \
  uint SGSOsc__i, SGSOsc__p; \
  SET_I2F(SGSOsc__i, (coeff)*(freq)); \
  (o)->phase += SGSOsc__i; \
  SGSOsc__p = (o)->phase + ((pm) << 16); \
  SGSOsc__i = SGSOsc__p >> (32-SGSOsc_TABINDEXBITS); \
  SGSOsc__s = (osctab)[SGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  SET_I2F((out), \
    (((float)SGSOsc__s) + \
     ((float)(((osctab)[SGSOsc__i + 1] - SGSOsc__s))) * \
     ((float)(SGSOsc__p & SGSOsc_TABINDEXMASK)) * \
     (1.f / (1 << (32-SGSOsc_TABINDEXBITS)))) * \
    (amp) \
  ); \
}while(0)

/* Gives output in 0.0 to 1.0 range for AM use as envelope */
#define SGSOsc_RUN_PM_ENVO(o, osctab, coeff, freq, pm, out) do{ \
  int SGSOsc__s; \
  uint SGSOsc__i, SGSOsc__p; \
  SET_I2F(SGSOsc__i, (coeff)*(freq)); \
  (o)->phase += SGSOsc__i; \
  SGSOsc__p = (o)->phase + ((pm) << 16); \
  SGSOsc__i = SGSOsc__p >> (32-SGSOsc_TABINDEXBITS); \
  SGSOsc__s = (osctab)[SGSOsc__i]; \
  /* write lerp'd & scaled result */ \
  (out) = (((float)SGSOsc__s) + \
           ((float)(((osctab)[SGSOsc__i + 1] - SGSOsc__s))) * \
           ((float)(SGSOsc__p & SGSOsc_TABINDEXMASK)) * \
           (1.f / (1 << (32-SGSOsc_TABINDEXBITS)))) * \
          (1.f / ((1 << 16) - 2)) + \
          .5f; \
}while(0)

#define SGSOsc_WAVE_OFFS(o, coeff, freq, timepos, out) do{ \
  uint SGSOsc__i; \
  SET_I2F(SGSOsc__i, (coeff)*(freq)); \
  uint SGSOsc__p = SGSOsc__i * (uint)(timepos); \
  int SGSOsc__o = SGSOsc__p - (SGSOsc_TABINDEXMASK+1); \
  (out) = (SGSOsc__o / SGSOsc__i); \
}while(0)

/**/
