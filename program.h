enum {
  MGS_TYPE_SETAMP = 1<<0,
  MGS_TYPE_SETTIME = 1<<1,
  MGS_TYPE_SETFREQ = 1<<2
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
  uchar type, wave, mode;
  float amp, delay, time, freq;
  uint id;
} MGSProgramNode;

struct MGSProgram {
  MGSProgramNode *steps;
  uint stepc;
};
