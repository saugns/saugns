/* Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "osc.h"

#define SGSOsc_TABSCALE ((float)((1<<15) - 1))
#define HALFLEN (SGSOsc_TABLEN>>1)

SGSOscLut SGSOsc_sin,
          SGSOsc_srs,
          SGSOsc_tri,
          SGSOsc_sqr,
          SGSOsc_saw;

void SGSOsc_init(void) {
  int i;
  static uchar done = 0;
  if (done) return;
  done = 1;

  /* first half */
  for (i = 0; i < HALFLEN; ++i) {
    double sinval = sin(PI * i/HALFLEN);
    SGSOsc_sin[i] = SGSOsc_TABSCALE * sinval;
    SGSOsc_srs[i] = SGSOsc_TABSCALE * sqrtf(sinval);
    if (i < (HALFLEN>>1))
      SGSOsc_tri[i] = SGSOsc_TABSCALE * (2.f * i/HALFLEN);
    else
      SGSOsc_tri[i] = SGSOsc_TABSCALE * (2.f * (HALFLEN-i)/HALFLEN);
    SGSOsc_sqr[i] = SGSOsc_TABSCALE;
    SGSOsc_saw[i] = SGSOsc_TABSCALE * (1.f * (HALFLEN-i)/HALFLEN);
  }
  /* second half */
  for (; i < SGSOsc_TABLEN; ++i) {
    SGSOsc_sin[i] = -SGSOsc_sin[i - HALFLEN];
    SGSOsc_srs[i] = -SGSOsc_srs[i - HALFLEN];
    SGSOsc_tri[i] = -SGSOsc_tri[i - HALFLEN];
    SGSOsc_sqr[i] = -SGSOsc_sqr[i - HALFLEN];
    SGSOsc_saw[i] = -SGSOsc_saw[SGSOsc_TABLEN - i];
  }
  /* wrap value */
  SGSOsc_sin[SGSOsc_TABLEN] = SGSOsc_sin[0];
  SGSOsc_srs[SGSOsc_TABLEN] = SGSOsc_srs[0];
  SGSOsc_sqr[SGSOsc_TABLEN] = SGSOsc_sqr[0];
  SGSOsc_tri[SGSOsc_TABLEN] = SGSOsc_tri[0];
  SGSOsc_saw[SGSOsc_TABLEN] = SGSOsc_saw[0];
}
