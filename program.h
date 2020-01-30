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
  MGS_TYPE_TOP = 0,
  MGS_TYPE_NESTED,
  MGS_TYPE_ENV
};

/* Operator attributes. */
enum {
  MGS_ATTR_FREQRATIO = 1<<0,
  MGS_ATTR_DYNFREQRATIO = 1<<1
};

/* Audio panning modes. */
enum {
  MGS_MODE_CENTER = 0,
  MGS_MODE_LEFT   = 1,
  MGS_MODE_RIGHT  = 2
};

/* Operator parameters. */
enum {
  MGS_PMODS = 1<<0,
  MGS_FMODS = 1<<1,
  MGS_AMODS = 1<<2,
  MGS_TIME = 1<<3,
  MGS_WAVE = 1<<4,
  MGS_FREQ = 1<<5,
  MGS_DYNFREQ = 1<<6,
  MGS_PHASE = 1<<7,
  MGS_AMP = 1<<8,
  MGS_DYNAMP = 1<<9,
  MGS_ATTR = 1<<10,
  MGS_MODS_MASK = (1<<3) - 1,
  MGS_PARAM_MASK = (1<<11) - 1
};

typedef struct MGS_ProgramNodeChain {
  uint32_t count;
  struct MGS_ProgramNode *chain;
} MGS_ProgramNodeChain;

typedef struct MGS_ProgramNode {
  struct MGS_ProgramNode *next;
  struct MGS_ProgramNode *ref_prev;
  uint8_t type, attr, wave, mode;
  float time, delay, freq, dynfreq, phase, amp, dynamp;
  uint32_t id;
  uint32_t values;
  MGS_ProgramNodeChain pmod, fmod, amod;
  struct MGS_ProgramNode *nested_next;
} MGS_ProgramNode;

struct MGS_SymTab;

struct MGS_Program {
  MGS_ProgramNode *node_list;
  uint32_t nodec;
  uint32_t topc;
  struct MGS_SymTab *symtab;
};
