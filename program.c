#include "sgensys.h"
#include "program.h"
#include "parser.h"
#include "builder.h"
/**
 * a------
 */
SGSProgram *SGS_read_program(const char *filename) {
	struct SGSParser *parser = SGS_create_parser();
	struct SGSParseList *result = SGS_parser_parse(parser, filename);
	SGS_destroy_parser(parser);
	return SGS_build_program(result);
}
