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
  MGS_WAVE_SIN = 0,
  MGS_WAVE_SQR,
  MGS_WAVE_TRI,
  MGS_WAVE_SAW
};

enum {
  MGS_MODE_CENTER = 0,
  MGS_MODE_LEFT   = 1,
  MGS_MODE_RIGHT  = 2
};

enum {
  MGS_TIME = 1<<0,
  MGS_FREQ = 1<<1,
  MGS_DYNFREQ = 1<<2,
  MGS_PHASE = 1<<3,
  MGS_AMP = 1<<4,
  MGS_DYNAMP = 1<<5,
  MGS_ATTR = 1<<6
};

enum {
  MGS_PMODS = 1<<0,
  MGS_FMODS = 1<<1,
  MGS_AMODS = 1<<2
};

typedef struct MGSProgramNodeChain {
  uint count;
  struct MGSProgramNode *chain;
} MGSProgramNodeChain;

typedef struct MGSProgramNode {
  struct MGSProgramNode *next;
  uchar type, flag, attr, wave, mode;
  float time, delay, freq, dynfreq, phase, amp, dynamp;
  uint id;
  MGSProgramNodeChain pmod, fmod, amod;
  union { /* type-specific data */
    struct {
      struct MGSProgramNode *link;
    } nested;
    struct {
      uchar values;
      uchar mods;
      struct MGSProgramNode *ref;
    } set;
  } spec;
} MGSProgramNode;

struct MGSProgram {
  MGSProgramNode *nodelist;
  uint nodec;
  uint topc; /* nodes >= topc are nested ones, ids starting over from 0 */
};
