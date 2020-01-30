/* mgensys: Audio program data.
 * Copyright (c) 2011, 2020 Joel K. Pettersson
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
#include "wave.h"

/* Node types. */
enum {
  MGS_TYPE_OP = 0,
  MGS_TYPE_ENV,
  MGS_NODE_TYPES
};

/* Operator attributes. */
enum {
  MGS_ATTR_FREQRATIO = 1<<0,
  MGS_ATTR_DYNFREQRATIO = 1<<1
};

/* Operator parameters. */
enum {
  MGS_AMODS = 1<<0,
  MGS_FMODS = 1<<1,
  MGS_PMODS = 1<<2,
  MGS_TIME = 1<<3,
  MGS_WAVE = 1<<4,
  MGS_FREQ = 1<<5,
  MGS_DYNFREQ = 1<<6,
  MGS_PHASE = 1<<7,
  MGS_AMP = 1<<8,
  MGS_DYNAMP = 1<<9,
  MGS_PAN = 1<<10,
  MGS_ATTR = 1<<11,
  MGS_MODS_MASK = (1<<3) - 1,
  MGS_PARAM_MASK = (1<<12) - 1
};

typedef struct MGS_ProgramNode MGS_ProgramNode;
typedef struct MGS_ProgramNodeChain MGS_ProgramNodeChain;
typedef struct MGS_ProgramDurScope MGS_ProgramDurScope;

struct MGS_ProgramDurScope {
  MGS_ProgramDurScope *next;
  MGS_ProgramNode *first_node;
  MGS_ProgramNode *last_node;
};

/* Time parameter flags. */
enum {
  MGS_TIME_SET = 1<<0,
};

typedef struct MGS_TimePar {
  float v;
  uint32_t flags;
} MGS_TimePar;

struct MGS_ProgramNodeChain {
  uint32_t count;
  MGS_ProgramNode *chain;
};

struct MGS_ProgramNode {
  MGS_ProgramNode *next;
  MGS_ProgramDurScope *dur;
  MGS_ProgramNode *ref_prev;
  uint8_t type, attr, wave;
  MGS_TimePar time;
  float delay, freq, dynfreq, phase, amp, dynamp, pan;
  uint32_t id;
  uint32_t first_id; // first id, not increasing for reference chains
  uint32_t root_id;  // first id of node, or of root node when nested
  uint32_t type_id;  // per-type id, unincreased for reference chains
  uint32_t params;
  MGS_ProgramNodeChain pmod, fmod, amod;
  MGS_ProgramNode *nested_next;
};

struct MGS_SymTab;

struct MGS_Program {
  MGS_ProgramNode *node_list;
  MGS_ProgramDurScope *dur_list;
  uint32_t node_count;
  uint32_t root_count;
  uint32_t type_counts[MGS_NODE_TYPES];
  struct MGS_SymTab *symtab;
  const char *name;
};
