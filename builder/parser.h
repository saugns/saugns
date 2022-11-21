/* mgensys: Script parser.
 * Copyright (c) 2011, 2019-2020 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "../mgensys.h"
#include "../program.h"
#include "../math.h"
#include "../help.h"
#include "../reader/file.h"
#include "../mempool.h"
#include "../reader/symtab.h"

void mgs_adjust_node_list(mgsProgramNode *restrict list);
