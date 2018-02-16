#include "sgensys.h"
#include "program.h"
#include "builder.h"
/**
 * a------
 */
SGSProgram *SGS_read_program(const char *filename) {
	return SGS_build_program(filename);
}
