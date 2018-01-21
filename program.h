enum {
  SGS_TYPE_TOP = 0,
  SGS_TYPE_NESTED,
  SGS_TYPE_SETTOP,
  SGS_TYPE_SETNESTED,
  SGS_TYPE_ENV
};

enum {
  SGS_FLAG_EXEC = 1<<0,
  SGS_FLAG_ENTERED = 1<<1
};

enum {
  SGS_ATTR_FREQRATIO = 1<<0,
  SGS_ATTR_DYNFREQRATIO = 1<<1
};

enum {
  SGS_WAVE_SIN = 0,
  SGS_WAVE_SQR,
  SGS_WAVE_TRI,
  SGS_WAVE_SAW
};

enum {
  SGS_MODE_CENTER = 0,
  SGS_MODE_LEFT   = 1,
  SGS_MODE_RIGHT  = 2
};

enum {
  SGS_TIME = 1<<0,
  SGS_FREQ = 1<<1,
  SGS_DYNFREQ = 1<<2,
  SGS_PHASE = 1<<3,
  SGS_AMP = 1<<4,
  SGS_DYNAMP = 1<<5,
  SGS_ATTR = 1<<6
};

enum {
  SGS_PMODS = 1<<0,
  SGS_FMODS = 1<<1,
  SGS_AMODS = 1<<2
};

typedef struct SGSProgramNodeChain {
  uint count;
  struct SGSProgramNode *chain;
} SGSProgramNodeChain;

typedef struct SGSProgramNode {
  struct SGSProgramNode *next;
  uchar type, flag, attr, wave, mode;
  float time, delay, freq, dynfreq, phase, amp, dynamp;
  uint id;
  SGSProgramNodeChain pmod, fmod, amod;
  union { /* type-specific data */
    struct {
      struct SGSProgramNode *link;
    } nested;
    struct {
      uchar values;
      uchar mods;
      struct SGSProgramNode *ref;
    } set;
  } spec;
} SGSProgramNode;

struct SGSProgram {
  SGSProgramNode *nodelist;
  uint nodec;
  uint topc; /* nodes >= topc are nested ones, ids starting over from 0 */
};
