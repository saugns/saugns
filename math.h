#include <math.h>
#define PI 3.141592653589f
#define DC_OFFSET 1.0E-25

typedef long long int llong;

#define SET_I2F(i, f) \
  asm( \
"fistpl %0\n\t" \
: "=m"(i) \
: "t"(f) \
: "st" \
  )

typedef int i16_16; /* fixed-point 16.16 */
typedef unsigned int ui16_16; /* unsigned fixed-point 16.16 */
#define SET_I16_162F(i16_16, f) SET_I2F(i16_16, (f) * 65536)
#define SET_F2I16_16(f, i16_16) ((void)((f) = (i16_16) * (1/65536.0)))

