typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

/*
 * SGSProgram
 */

struct SGSProgram;
typedef struct SGSProgram SGSProgram;

SGSProgram* SGSProgram_create(const char *filename);
void SGSProgram_destroy(SGSProgram *o);

/*
 * SGSGenerator
 */

struct SGSGenerator;
typedef struct SGSGenerator SGSGenerator;

SGSGenerator* SGSGenerator_create(uint srate, SGSProgram *prg);
void SGSGenerator_destroy(SGSGenerator *o);
uchar SGSGenerator_run(SGSGenerator *o, short *buf, uint len);
