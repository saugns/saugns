//#include "osc.h"

enum {
  MGS_TYPE_WAIT = 0,
  MGS_TYPE_SIN,
  MGS_TYPE_SQR,
  MGS_TYPE_TRI,
  MGS_TYPE_SAW
};

enum {
  MGS_MODE_CENTER = 1|2,
  MGS_MODE_LEFT   = 1,
  MGS_MODE_RIGHT  = 2
};

typedef struct MGSProgramNode {
  struct MGSProgramNode *pnext, *snext, *pfirst;
  uchar type, mode;
  union MGSProgramComponent *component;
  float amp, time, freq;
  uint len, pos;
} MGSProgramNode;

typedef union MGSProgramComponent {
  MGSSinOsc sinosc;
  MGSSqrOsc sqrosc;
  MGSSawOsc sawosc;
  MGSTriOsc triosc;
} MGSProgramComponent;

struct MGSProgram {
  MGSProgramNode *steps, *last;
  MGSProgramComponent *components;
  uint componentc;
};
