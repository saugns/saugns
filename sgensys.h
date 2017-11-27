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
uchar SGS_generator_run(SGSGenerator *o, short *buf, uint buf_len,
                        uint *gen_len);

/*
 * Debugging options
 */

#define USE_LEXER 0
#define HASHTAB_TEST 0
#define LEXER_TEST 0
