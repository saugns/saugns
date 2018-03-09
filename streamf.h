/* sgensys: Stream file module.
 * Copyright (c) 2014, 2017-2018 Joel K. Pettersson
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

#pragma once
#include "stream.h"

/*
 * Wrapper around stdio for the Stream type.
 *
 * Makes for faster handling of characters one-by-one than directly
 * getting/ungetting characters, and CBuf implements some functionality
 * convenient for scanning.
 *
 * When reading, the value 0 is used to mark the end of an opened file,
 * but may also be read for other reasons; the status field will make the
 * case clear.
 *
 * Currently only supports reading.
 */

bool SGS_Stream_fopenrb(SGS_Stream *o, const char *fname);
