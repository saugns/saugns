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

enum {
  MGS_TYPE_TOP = 0,
  MGS_TYPE_NESTED,
  MGS_TYPE_SETTOP,
  MGS_TYPE_SETNESTED,
  MGS_TYPE_ENV
};

enum {
  MGS_FLAG_EXEC = 1<<0,
  MGS_FLAG_ENTERED = 1<<1
};

enum {
  MGS_ATTR_FREQRATIO = 1<<0,
  MGS_ATTR_DYNFREQRATIO = 1<<1
};

enum {
  MGS_MODE_CENTER = 0,
  MGS_MODE_LEFT   = 1,
  MGS_MODE_RIGHT  = 2
};

enum {
  MGS_TIME = 1<<0,
  MGS_WAVE = 1<<1,
  MGS_FREQ = 1<<2,
  MGS_DYNFREQ = 1<<3,
  MGS_PHASE = 1<<4,
  MGS_AMP = 1<<5,
  MGS_DYNAMP = 1<<6,
  MGS_ATTR = 1<<7
};

enum {
  MGS_PMODS = 1<<0,
  MGS_FMODS = 1<<1,
  MGS_AMODS = 1<<2
};

typedef struct MGS_ProgramNodeChain {
  uint32_t count;
  struct MGS_ProgramNode *chain;
} MGS_ProgramNodeChain;

typedef struct MGS_ProgramNode {
  struct MGS_ProgramNode *next;
  uint8_t type, flag, attr, wave, mode;
  float time, delay, freq, dynfreq, phase, amp, dynamp;
  uint32_t id;
  MGS_ProgramNodeChain pmod, fmod, amod;
  union { /* type-specific data */
    struct {
      struct MGS_ProgramNode *link;
    } nested;
    struct {
      uint8_t values;
      uint8_t mods;
      struct MGS_ProgramNode *ref;
    } set;
  } spec;
} MGS_ProgramNode;

struct MGS_SymTab;

struct MGS_Program {
  MGS_ProgramNode *nodelist;
  uint32_t nodec;
  uint32_t topc; /* nodes >= topc are nested ones, ids starting over from 0 */
  struct MGS_SymTab *symtab;
  const char *name;
};
