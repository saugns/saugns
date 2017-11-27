/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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
#include "ptrlist.h"

struct SGSOperatorNode;

/*
 * Parsing nodes.
 */

enum {
  /* parse flags */
  ON_OPERATOR_LATER_USED = 1<<0,
  ON_MULTIPLE_OPERATORS = 1<<1,
  ON_OPERATOR_NESTED = 1<<2,
  ON_LABEL_ALLOC = 1<<3,
  ON_TIME_DEFAULT = 1<<4,
  ON_SILENCE_ADDED = 1<<5,
};

typedef struct SGSOperatorNode {
  struct SGSEventNode *event;
  SGSPtrList on_next; /* all immediate forward references for operator(s) */
  struct SGSOperatorNode *on_prev; /* preceding node(s) for same operator(s) */
  struct SGSOperatorNode *next_bound;
  uint32_t on_flags;
  const char *label;
  /* operator parameters */
  uint32_t operator_id; /* not filled in by parser; for later use (program.c) */
  uint32_t operator_params;
  uint8_t attr;
  uint8_t wave;
  int32_t time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
  /* node adjacents in operator linkage graph */
  SGSPtrList fmods, pmods, amods;
} SGSOperatorNode;

enum {
  /* parse flags */
  EN_VOICE_LATER_USED = 1<<0,
  EN_ADD_WAIT_DURATION = 1<<1,
};

typedef struct SGSEventNode {
  struct SGSEventNode *next;
  struct SGSEventNode *groupfrom;
  struct SGSEventNode *composite;
  int32_t wait_ms;
  SGSPtrList operators; /* operators included in event */
  uint32_t en_flags;
  /* voice parameters */
  uint32_t voice_id; /* not filled in by parser; for later use (program.c) */
  uint32_t voice_params;
  struct SGSEventNode *voice_prev; /* preceding event for same voice */
  uint8_t voice_attr;
  float panning;
  SGSProgramValit valitpanning;
  SGSPtrList graph;
} SGSEventNode;

typedef struct SGSParserResult {
  SGSEventNode *events;
} SGSParserResult;

SGSParserResult *SGSParser_parse(const char *filename);
void SGSParser_destroy_result(SGSParserResult *pr);
