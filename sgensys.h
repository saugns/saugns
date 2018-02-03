typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

/*
 * SGSProgram
 */

struct SGSProgram;
typedef struct SGSProgram SGSProgram;

SGSProgram* SGS_program_create(const char *filename);
void SGS_program_destroy(SGSProgram *o);

/*
 * SGSGenerator
 */

struct SGSGenerator;
typedef struct SGSGenerator SGSGenerator;

SGSGenerator* SGS_generator_create(uint srate, SGSProgram *prg);
void SGS_generator_destroy(SGSGenerator *o);
uchar SGS_generator_run(SGSGenerator *o, short *buf, uint len);
