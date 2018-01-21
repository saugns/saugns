#include "math.h"

/*
 * MGSSinOsc - thanks to anonymous musicdsp archive contributor for algorithm
 */

typedef struct MGSSinOsc {
  float coeff;
  float sin, cos;
} MGSSinOsc;

#define MGSSinOsc_SET_COEFF(o, fq, sr) \
  ((void)((o)->coeff = (2.f * sin(PI * (fq)/(sr)))))

#define MGSSinOsc_SET_RANGE(o, r) \
  ((void)(((o)->sin = (r)), (o)->cos = 0.f))

#define MGSSinOsc_RUN(o) do{ \
  (o)->sin -= (o)->coeff * (o)->cos; \
  (o)->cos += (o)->coeff * (o)->sin; \
}while(0)

/*
 * MGSSqrOsc
 */

typedef struct MGSSqrOsc {
  uint len, pos;
  float sqr;
} MGSSqrOsc;

#define MGSSqrOsc_SET_COEFF(o, fq, sr) \
  ((void)((o)->len = (sr)/((fq)*2.f)))

#define MGSSqrOsc_SET_RANGE(o, r) \
  ((void)(((o)->pos = 0), (o)->sqr = (r)))

#define MGSSqrOsc_RUN(o) do{ \
  if ((o)->pos++ >= (o)->len) { \
    (o)->pos = 0; \
    (o)->sqr = -(o)->sqr; \
  } \
}while(0)

/*
 * MGSSawOsc
 */

typedef struct MGSSawOsc {
  float size, step;
  float saw;
} MGSSawOsc;

#define MGSSawOsc_SET_COEFF(o, fq, sr) \
  ((void)((o)->step = ((fq)*2.f)/(sr)))

#define MGSSawOsc_SET_RANGE(o, r) \
  ((void)(((o)->size = (r)), (o)->saw = 0.f))

#define MGSSawOsc_RUN_UP(o) do{ \
  (o)->saw += (o)->step * (o)->size; \
  if ((o)->saw >= (o)->size - DC_OFFSET) (o)->saw = - (o)->size; \
}while(0)

#define MGSSawOsc_RUN_DOWN(o) do{ \
  (o)->saw -= (o)->step * (o)->size; \
  if ((o)->saw <= DC_OFFSET - (o)->size) (o)->saw = (o)->size; \
}while(0)

/* Default - more or less arbitrary. */
#define MGSSawOsc_RUN(o) MGSSawOsc_RUN_UP(o)

/*
 * MGSTriOsc
 */

typedef struct MGSTriOsc {
  float size, step;
  float tri;
} MGSTriOsc;

#define MGSTriOsc_SET_COEFF(o, fq, sr) \
  ((void)((o)->step = ((fq)*4.f)/(sr)))

#define MGSTriOsc_SET_RANGE(o, r) \
  ((void)(((o)->size = (r)), (o)->tri = 0.f))

#define MGSTriOsc_RUN(o) do{ \
  (o)->tri += (o)->step * (o)->size; \
  if (fabs((o)->tri) >= (o)->size - DC_OFFSET) (o)->step = - (o)->step; \
}while(0)
