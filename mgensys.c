/* mgensys: Main module / Command-line interface
 * Copyright (c) 2011, 2017-2018, 2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "mgensys.h"
#include <stdio.h>
#define DEFAULT_SRATE 96000

static bool run_program(struct MGS_Program *prg,
    bool use_audiodev, uint32_t srate) {
  MGS_PtrArr ptr_list = {0};
  MGS_PtrArr_add(&ptr_list, prg);
  bool status = true;
  status = MGS_render(&ptr_list, srate, use_audiodev, NULL);
  return status;
}

int main(int argc, char **argv) {
  MGS_Program *prg;
  uint32_t srate = DEFAULT_SRATE;
  if (argc < 2) {
    puts("usage: mgensys scriptfile");
    return 0;
  }
  if (!(prg = MGS_create_Program(argv[1]))) {
    printf("error: couldn't open script file \"%s\"\n", argv[1]);
    return 1;
  }
  bool ok = run_program(prg, true, srate);
  MGS_destroy_Program(prg);
  return !ok;
}
