#include "sgensys.h"
#include "osc.h"

#define SGSOsc_TABSCALE ((float)((1<<15) - 1))
#define HALFLEN (SGSOsc_TABLEN>>1)

SGSOscTab SGSOsc_sin,
          SGSOsc_sqr,
          SGSOsc_tri,
          SGSOsc_saw;

void SGSOsc_init(void) {
  int i;
  static uchar done = 0;
  if (done) return;
  done = 1;

  /* first half */
  for (i = 0; i < HALFLEN; ++i) {
    SGSOsc_sin[i] = SGSOsc_TABSCALE * sin(PI * i/HALFLEN);
    SGSOsc_sqr[i] = SGSOsc_TABSCALE;
    if (i < (HALFLEN>>1))
      SGSOsc_tri[i] = SGSOsc_TABSCALE * (2.f * i/HALFLEN);
    else
      SGSOsc_tri[i] = SGSOsc_TABSCALE * (2.f * (HALFLEN-i)/HALFLEN);
    SGSOsc_saw[i] = SGSOsc_TABSCALE * (1.f * (HALFLEN-i)/HALFLEN);
  }
  /* second half */
  for (; i < SGSOsc_TABLEN; ++i) {
    SGSOsc_sin[i] = -SGSOsc_sin[i - HALFLEN];
    SGSOsc_sqr[i] = -SGSOsc_sqr[i - HALFLEN];
    SGSOsc_tri[i] = -SGSOsc_tri[i - HALFLEN];
    SGSOsc_saw[i] = -SGSOsc_saw[SGSOsc_TABLEN - i];
  }
  /* wrap value */
  SGSOsc_sin[SGSOsc_TABLEN] = SGSOsc_sin[0];
  SGSOsc_sqr[SGSOsc_TABLEN] = SGSOsc_sqr[0];
  SGSOsc_tri[SGSOsc_TABLEN] = SGSOsc_tri[0];
  SGSOsc_saw[SGSOsc_TABLEN] = SGSOsc_saw[0];
}
