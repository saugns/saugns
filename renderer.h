/* sgensys audio renderer
 * Copyright (c) 2011-2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

/*
 * SGSRenderer
 */

struct SGSRenderer;
typedef struct SGSRenderer SGSRenderer;

SGSRenderer* SGS_create_renderer(uint srate, SGSResult_t res);
void SGS_destroy_renderer(SGSRenderer *o);

uchar SGS_renderer_run(SGSRenderer *o, short *buf, uint buf_len,
                        uint *gen_len);
