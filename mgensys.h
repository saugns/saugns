typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
#include "osc.h"

/*
 * MGSProgram
 */

struct MGSProgram;
typedef struct MGSProgram MGSProgram;

MGSProgram* MGSProgram_create(const char *filename);
void MGSProgram_destroy(MGSProgram *o);

/*
 * MGSGenerator
 */

struct MGSGenerator;
typedef struct MGSGenerator MGSGenerator;

MGSGenerator* MGSGenerator_create(uint srate, MGSProgram *prg);
void MGSGenerator_destroy(MGSGenerator *o);
uchar MGSGenerator_run(MGSGenerator *o, short *buf, uint len);
