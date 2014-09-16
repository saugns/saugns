/* Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

struct SGSWAVFile;
typedef struct SGSWAVFile SGSWAVFile;

SGSWAVFile *SGS_begin_wav_file(const char *fpath, ushort channels, uint srate);
int SGS_end_wav_file(SGSWAVFile *wf);
uchar SGS_wav_file_write(SGSWAVFile *wf, const short *buf, uint samples);
