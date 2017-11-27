/* sgensys script parser module.
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

#include <stdio.h>

struct SGSOperatorNode;

/*
 * SGSNodeList
 */

typedef struct SGSNodeList {
  ushort count,
         inactive_count; /* used when nodes are inherited from another list */
  void *data;
} SGSNodeList;

#define SGS_NODE_LIST_GET(nl) \
 ((struct SGSOperatorNode**)(((nl)->count > 1) ? (nl)->data : &(nl)->data))

void SGS_node_list_add(SGSNodeList *list, struct SGSOperatorNode *n);
void SGS_node_list_clear(SGSNodeList *list);
void SGS_node_list_safe_copy(SGSNodeList *dst, const SGSNodeList *src);
int SGS_node_list_rforeach(SGSNodeList *list,
                           int (*callback)(struct SGSOperatorNode *op,
                                           void *arg),
                           void *arg);

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
  SGSNodeList on_next; /* all immediate forward references for operator(s) */
  struct SGSOperatorNode *on_prev; /* preceding node(s) for same operator(s) */
  struct SGSOperatorNode *next_bound;
  uint on_flags;
  const char *label;
  /* operator parameters */
  uint operator_id; /* not filled in by parser; for later use (program.c) */
  uint operator_params;
  uchar attr;
  uchar wave;
  int time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
  /* node adjacents in operator linkage graph */
  SGSNodeList fmods, pmods, amods;
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
  int wait_ms;
  SGSNodeList operators; /* operators included in event */
  uint en_flags;
  /* voice parameters */
  uint voice_id; /* not filled in by parser; for later use (program.c) */
  uint voice_params;
  struct SGSEventNode *voice_prev; /* preceding event for same voice */
  uchar voice_attr;
  float panning;
  SGSProgramValit valitpanning;
  SGSNodeList graph;
} SGSEventNode;

void SGS_event_node_destroy(SGSEventNode *e);

typedef struct SGSParser {
  FILE *f;
  const char *fn;
  struct SGSSymtab *st;
  uint line;
  uint calllevel;
  uint scopeid;
  char c, nextc;
  /* node state */
  SGSEventNode *events;
  SGSEventNode *last_event;
  /* settings/ops */
  float ampmult;
  int def_time_ms;
  float def_freq, def_A4tuning, def_ratio;
} SGSParser;

void SGS_parse(SGSParser *o, FILE *f, const char *fn);
