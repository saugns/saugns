/* operator parameters */
enum {
  /* voice values */
  SGS_GRAPH = 1<<0,
  SGS_PANNING = 1<<1,
  SGS_VALITPANNING = 1<<2,
  SGS_VOATTR = 1<<3,
  /* operator values */
  SGS_ADJC = 1<<4,
  SGS_WAVE = 1<<5,
  SGS_TIME = 1<<6,
  SGS_SILENCE = 1<<7,
  SGS_FREQ = 1<<8,
  SGS_VALITFREQ = 1<<9,
  SGS_DYNFREQ = 1<<10,
  SGS_PHASE = 1<<11,
  SGS_AMP = 1<<12,
  SGS_VALITAMP = 1<<13,
  SGS_DYNAMP = 1<<14,
  SGS_OPATTR = 1<<15
};

/* operator wave types */
enum {
  SGS_WAVE_SIN = 0,
  SGS_WAVE_SRS,
  SGS_WAVE_TRI,
  SGS_WAVE_SQR,
  SGS_WAVE_SAW
};

/* operator atttributes */
enum {
  SGS_ATTR_FREQRATIO = 1<<1,
  SGS_ATTR_DYNFREQRATIO = 1<<2,
  SGS_ATTR_VALITFREQ = 1<<3,
  SGS_ATTR_VALITFREQRATIO = 1<<4,
  SGS_ATTR_VALITAMP = 1<<5,
  SGS_ATTR_VALITPANNING = 1<<6
};

/* value iteration types */
enum {
  SGS_VALIT_NONE = 0, /* when none given */
  SGS_VALIT_LIN,
  SGS_VALIT_EXP,
  SGS_VALIT_LOG
};

typedef struct SGSProgramGraph {
  uchar opc;
  int ops[1]; /* sized to opc */
} SGSProgramGraph;

typedef struct SGSProgramGraphAdjcs {
  uchar pmodc;
  uchar fmodc;
  uchar amodc;
  uchar level;  /* index for buffer used to store result to use if node
                   revisited when traversing the graph. */
  int adjcs[1]; /* sized to total number */
} SGSProgramGraphAdjcs;

typedef struct SGSProgramValit {
  int time_ms, pos_ms;
  float goal;
  uchar type;
} SGSProgramValit;

typedef struct SGSProgramVoiceData {
  const SGSProgramGraph *graph;
  uchar attr;
  float panning;
  SGSProgramValit valitpanning;
} SGSProgramVoiceData;

typedef struct SGSProgramOperatorData {
  const SGSProgramGraphAdjcs *adjcs;
  uint operatorid;
  uchar attr, wave;
  int time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
} SGSProgramOperatorData;

typedef struct SGSProgramEvent {
  int wait_ms;
  uint params;
  uint voiceid; /* needed for both voice and operator data */
  const SGSProgramVoiceData *voice;
  const SGSProgramOperatorData *operator;
} SGSProgramEvent;

struct SGSProgram {
  const SGSProgramEvent *events;
  uint eventc;
  uint operatorc,
       voicec;
};
