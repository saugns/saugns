/* sgensys parsing data to program data translator module.
 * Copyright (c) 2011-2012 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

enum {
  /* voice parameters */
  SGS_GRAPH = 1<<0,
  SGS_PANNING = 1<<1,
  SGS_VALITPANNING = 1<<2,
  SGS_VOATTR = 1<<3,
  /* operator parameters */
  SGS_ADJCS = 1<<4,
  SGS_WAVE = 1<<5,
  SGS_TIME = 1<<6,
  SGS_SILENCE = 1<<7,
  SGS_FREQ = 1<<8,
  SGS_VALITFREQ = 1<<9,
  SGS_DYNFREQ = 1<<10,
  SGS_PHASE = 1<<11,
  SGS_AMP = 1<<12,
  SGS_VALITAMP = 1<<13,
  SGS_DYNAMP = 1<<14,
  SGS_OPATTR = 1<<15
};

#define SGS_VOICE_PARAMS(flags) \
  ((flags) & (SGS_GRAPH|SGS_PANNING|SGS_VALITPANNING|SGS_VOATTR))

#define SGS_OPERATOR_PARAMS(flags) \
  ((flags) & (SGS_ADJCS|SGS_WAVE|SGS_SILENCE|SGS_FREQ|SGS_VALITFREQ| \
              SGS_DYNFREQ|SGS_PHASE|SGS_AMP|SGS_VALITAMP|SGS_DYNAMP| \
              SGS_OPATTR))

/* operator wave types */
enum {
  SGS_WAVE_SIN = 0,
  SGS_WAVE_SRS,
  SGS_WAVE_TRI,
  SGS_WAVE_SQR,
  SGS_WAVE_SAW
};

/* special operator timing values */
enum {
  SGS_TIME_INF = -1 /* used for nested operators */
};

/* operator atttributes */
enum {
  SGS_ATTR_FREQRATIO = 1<<1,
  SGS_ATTR_DYNFREQRATIO = 1<<2,
  SGS_ATTR_VALITFREQ = 1<<3,
  SGS_ATTR_VALITFREQRATIO = 1<<4,
  SGS_ATTR_VALITAMP = 1<<5,
  SGS_ATTR_VALITPANNING = 1<<6
};

/* value iteration types */
enum {
  SGS_VALIT_NONE = 0, /* when none given */
  SGS_VALIT_LIN,
  SGS_VALIT_EXP,
  SGS_VALIT_LOG
};

typedef struct SGSProgramGraph {
  uchar opc;
  int ops[1]; /* sized to opc */
} SGSProgramGraph;

typedef struct SGSProgramGraphAdjcs {
  uchar fmodc;
  uchar pmodc;
  uchar amodc;
  uchar level;  /* index for buffer used to store result to use if node
                   revisited when traversing the graph. */
  int adjcs[1]; /* sized to total number */
} SGSProgramGraphAdjcs;

typedef struct SGSProgramValit {
  int time_ms, pos_ms;
  float goal;
  uchar type;
} SGSProgramValit;

typedef struct SGSProgramVoiceData {
  const SGSProgramGraph *graph;
  uchar attr;
  float panning;
  SGSProgramValit valitpanning;
} SGSProgramVoiceData;

typedef struct SGSProgramOperatorData {
  const SGSProgramGraphAdjcs *adjcs;
  uint operator_id;
  uchar attr, wave;
  int time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
} SGSProgramOperatorData;

typedef struct SGSProgramEvent {
  int wait_ms;
  uint params;
  uint voice_id; /* needed for both voice and operator data */
  const SGSProgramVoiceData *voice;
  const SGSProgramOperatorData *operator;
} SGSProgramEvent;

struct SGSProgram {
  const SGSProgramEvent *events;
  uint eventc;
  uint operatorc,
       voicec;
};
