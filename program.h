/* sgensys: Sound program definitions.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
#include "osc.h"

/*
 * Program types and definitions.
 */

struct SGSProgram;
typedef struct SGSProgram SGSProgram;

enum {
	/* voice parameters */
	SGS_P_GRAPH = 1<<0,
	SGS_P_PANNING = 1<<1,
	SGS_P_VALITPANNING = 1<<2,
	SGS_P_VOATTR = 1<<3,
	/* operator parameters */
	SGS_P_ADJCS = 1<<4,
	SGS_P_WAVE = 1<<5,
	SGS_P_TIME = 1<<6,
	SGS_P_SILENCE = 1<<7,
	SGS_P_FREQ = 1<<8,
	SGS_P_VALITFREQ = 1<<9,
	SGS_P_DYNFREQ = 1<<10,
	SGS_P_PHASE = 1<<11,
	SGS_P_AMP = 1<<12,
	SGS_P_VALITAMP = 1<<13,
	SGS_P_DYNAMP = 1<<14,
	SGS_P_OPATTR = 1<<15
};

#define SGS_P_VOICE(flags) \
	((flags) & (SGS_GRAPH|SGS_PANNING|SGS_VALITPANNING|SGS_VOATTR))

#define SGS_P_OPERATOR(flags) \
	((flags) & (SGS_ADJCS|SGS_WAVE|SGS_SILENCE|SGS_FREQ|SGS_VALITFREQ| \
	            SGS_DYNFREQ|SGS_PHASE|SGS_AMP|SGS_VALITAMP|SGS_DYNAMP| \
	            SGS_OPATTR))

/* special operator timing values */
enum {
	SGS_TIME_INF = -1 /* used for nested operators */
};

/* operator atttributes */
enum {
	SGS_ATTR_WAVEENV = 1<<0, // should be moved, set by interpreter
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

struct SGSProgramGraph {
  uint32_t opc;
  int32_t ops[1]; /* sized to opc */
};
typedef const struct SGSProgramGraph *SGSProgramGraph_t;

struct SGSProgramGraphAdjcs {
  uint32_t fmodc;
  uint32_t pmodc;
  uint32_t amodc;
  uint32_t level;  /* index for buffer used to store result to use if node
                    revisited when traversing the graph. */
  int32_t adjcs[1]; /* sized to total number */
};
typedef const struct SGSProgramGraphAdjcs *SGSProgramGraphAdjcs_t;

struct SGSProgramValit {
  int32_t time_ms, pos_ms;
  float goal;
  uint8_t type;
};
typedef struct SGSProgramValit SGSProgramValit_t;

struct SGSProgramVoiceData {
  SGSProgramGraph_t graph;
  uint8_t attr;
  float panning;
  SGSProgramValit_t valitpanning;
};
typedef const struct SGSProgramVoiceData *SGSProgramVoiceData_t;

struct SGSProgramOperatorData {
  SGSProgramGraphAdjcs_t adjcs;
  uint32_t operator_id;
  uint8_t attr, wave;
  int32_t time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit_t valitfreq, valitamp;
};
typedef const struct SGSProgramOperatorData *SGSProgramOperatorData_t;

struct SGSProgramEvent {
  int32_t wait_ms;
  uint32_t params;
  uint32_t voice_id; /* needed for both voice and operator data */
  SGSProgramVoiceData_t voice;
  SGSProgramOperatorData_t operator;
};
typedef const struct SGSProgramEvent *SGSProgramEvent_t;

struct SGSProgram {
  SGSProgramEvent_t events;
  size_t eventc;
  uint32_t operatorc;
  uint32_t voicec;
};
typedef const struct SGSProgram *SGSProgram_t;
