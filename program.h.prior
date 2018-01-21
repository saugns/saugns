enum {
  MGS_TYPE_WAVE = 0,
  MGS_TYPE_ENV = 1
};

enum {
  MGS_FLAG_PLAY = 1<<0,
  MGS_FLAG_REFAMP = 1<<1,
  MGS_FLAG_REFTIME = 1<<2,
  MGS_FLAG_REFFREQ = 1<<3,
  MGS_FLAG_FREQRATIO = 1<<4,
  MGS_FLAG_ENTERED = 1<<5
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

typedef struct MGSProgramNode {
  struct MGSProgramNode *next, *ref;
  uchar type, flag, wave, mode;
  float amp, delay, time, freq;
  uint id;
  uchar modc, free_mods;
  struct MGSProgramNode **mods;
} MGSProgramNode;

struct MGSProgram {
  MGSProgramNode *steps;
  uint stepc;
};
