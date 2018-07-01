/* sgensys: Audio program interpreter module.
 * Copyright (c) 2013-2014, 2018 Joel K. Pettersson
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

#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
};

typedef struct ONState {
	uint32_t flags;
	const SGS_ProgramOperatorData *in_odata;
	SGS_ResultOperatorData *out_odata;
} ONState;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
	VN_EXEC = 1<<1
};

typedef struct VNState {
	uint32_t flags;
	const SGS_ProgramVoiceData *in_vdata;
	SGS_ResultVoiceData *out_vdata;
} VNState;

struct SGS_Interpreter {
	/* current program */
	SGS_Program *program;
	SGS_Result *result;
	ONState *ops;
	VNState *vcs;
	uint32_t time_ms;
	size_t odata_id;
	size_t vdata_id;
	/* global state */
	SGS_PList results;
};

static void end_program(SGS_Interpreter *o);
static void handle_event(SGS_Interpreter *o, size_t i);

static SGS_Result *run_program(SGS_Interpreter *o, SGS_Program *program) {
	size_t i;
	SGS_Result *res = NULL;

	/* init */
	o->program = program;
	i = program->event_count;
	res = calloc(1, sizeof(SGS_Result));
	if (!res) {
		end_program(o);
		return NULL;
	}
	o->result = res;
	if (i > 0) {
		res->events = calloc(i, sizeof(SGS_ResultEvent));
		if (!res->events) {
			end_program(o);
			return NULL;
		}
	}
	res->event_count = i;
	i = program->operator_count;
	if (i > 0) {
		o->ops = calloc(i, sizeof(ONState));
		if (!o->ops) {
			end_program(o);
			return NULL;
		}
	}
	res->operator_count = i;
	i = program->voice_count;
	if (i > 0) {
		o->vcs = calloc(i, sizeof(VNState));
		if (!o->vcs) {
			end_program(o);
			return NULL;
		}
	}
	res->voice_count = i;
	i = program->odata_count;
	if (i > 0) {
		res->odata_nodes = calloc(i, sizeof(SGS_ResultOperatorData));
		if (!res->odata_nodes) {
			end_program(o);
			return NULL;
		}
	}
	i = program->vdata_count;
	if (i > 0) {
		res->vdata_nodes = calloc(i, sizeof(SGS_ResultVoiceData));
		if (!res->vdata_nodes) {
			end_program(o);
			return NULL;
		}
	}
	res->flags = program->flags;
	res->name = program->name;
	o->time_ms = 0;
	o->odata_id = 0;
	o->vdata_id = 0;

	/* run */
	for (i = 0; i < program->event_count; ++i) {
		handle_event(o, i);


	}

	/* cleanup */
	end_program(o);
	return res;
}

static void end_program(SGS_Interpreter *o) {
	o->program = NULL;
	if (o->ops) {
		free(o->ops);
		o->ops = NULL;
	}
	if (o->vcs) {
		free(o->vcs);
		o->vcs = NULL;
	}
}

static void handle_event(SGS_Interpreter *o, size_t i) {
	const SGS_ProgramEvent *pe = o->program->events[i];
	SGS_ResultEvent *re = &o->result->events[i];
	o->time_ms += pe->wait_ms;
	re->wait_ms = pe->wait_ms;
	re->params = pe->params;

	const SGS_ProgramOperatorData *pod = pe->operator;
	const SGS_ProgramVoiceData *pvd = pe->voice;
	uint32_t voice_id = pe->voice_id;
	if (pod) {
		uint32_t operator_id = pod->operator_id;
		SGS_ResultOperatorData *rod;
		ONState *ostate = &o->ops[operator_id];
		ostate->in_odata = pod;
		rod = &o->result->odata_nodes[o->odata_id++];
		rod->operator_id = operator_id;
		re->operator_data = rod;
		ostate->out_odata = rod;
	}
	if (pvd) {
		SGS_ResultVoiceData *rvd;
		VNState *vstate = &o->vcs[voice_id];
		vstate->in_vdata = pvd;
		rvd = &o->result->vdata_nodes[o->vdata_id++];
		rvd->voice_id = voice_id;
		re->voice_data = rvd;
		vstate->out_vdata = rvd;
	}
}

SGS_Interpreter *SGS_create_Interpreter(void) {
	SGS_Interpreter *o = calloc(1, sizeof(SGS_Interpreter));
	return o;
}

void SGS_destroy_Interpreter(SGS_Interpreter *o) {
	SGS_Interpreter_clear(o);
	free(o);
}

SGS_Result *SGS_Interpreter_run(SGS_Interpreter *o, SGS_Program *program) {
	SGS_Result *result = run_program(o, program);
	if (result == NULL) {
		return NULL;
	}

	SGS_PList_add(&o->results, result);
	return result;
}

/**
 * Get result list, setting it to \p dst.
 *
 * The list assigned to \p dst will be freed when the interpreter
 * instance is destroyed or SGS_Interpreter_clear() is called,
 * unless dst is added to.
 */
void SGS_Interpreter_get_results(SGS_Interpreter *o, SGS_PList *dst) {
	if (!dst) return;
	SGS_PList_copy(dst, &o->results);
}

void SGS_Interpreter_clear(SGS_Interpreter *o) {
	SGS_PList_clear(&o->results);
}

#if 0

typedef uint32_t SGS_blid_t;

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
};

typedef struct OperatorNode {
	SGS_Osc osc;
	int32_t time;
	uint32_t silence;
	uint8_t flags;
	uint8_t attr;
	SGS_wave_t wave;
	const SGS_ProgramGraphAdjcs *adjcs;
	float amp, dynamp;
	float freq, dynfreq;
	ParameterValit valitamp, valitfreq;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
	VN_EXEC = 1<<1
};

typedef struct VoiceNode {
	int32_t pos; /* negative for wait time */
	uint8_t flags;
	uint8_t attr;
	const SGS_ProgramGraph *graph;
	float panning;
	ParameterValit valitpanning;
} VoiceNode;

typedef union EventValue {
	int32_t i;
	float f;
	void *v;
} EventValue;

typedef struct EventNode {
	EventValue *data;
	uint32_t waittime;
	uint32_t params;
	int32_t voice_id;
	int32_t operator_id;
} EventNode;

static uint32_t count_flags(uint32_t flags) {
	uint32_t i, count = 0;
	for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
		count += flags & 1;
		flags >>= 1;
	}
	return count;
}

struct SGS_Generator {
	double osc_coeff;
	uint32_t srate;
	uint32_t buf_count;
	Buf *bufs;
	size_t event, event_count;
	EventNode *events;
	uint32_t event_pos;
	uint16_t voice, voice_count;
	VoiceNode *voices;
	float amp_scale;
	uint32_t operator_count;
	OperatorNode *operators;
	EventValue *event_values;
};

/*
 * Count buffers needed for operator, including linked operators.
 * TODO: Redesign, do graph traversal before generator.
 */
static uint32_t calc_bufs(SGS_Generator *o, int32_t op_id) {
	uint32_t count = 0, i, res;
	OperatorNode *n = &o->operators[op_id];
	if ((n->flags & ON_VISITED) != 0) {
		fprintf(stderr,
"skipping operator %d; generator does not support circular references\n",
			op_id);
		return 0;
	}
	if (n->adjcs) {
		const int32_t *mods = n->adjcs->adjcs;
		const uint32_t modc = n->adjcs->fmodc +
			n->adjcs->pmodc +
			n->adjcs->amodc;
		n->flags |= ON_VISITED;
		for (i = 0; i < modc; ++i) {
			int32_t next_id = mods[i];
//			printf("visit node %d\n", next_id);
			res = calc_bufs(o, next_id);
			if (res > count) count = res;
		}
		n->flags &= ~ON_VISITED;
	}
	return count + 5;
}

/*
 * Check operators for voice and increase the buffer allocation if needed.
 * TODO: Redesign, do graph traversal before generator.
 */
static void upsize_bufs(SGS_Generator *o, VoiceNode *vn) {
	uint32_t count = 0, i, res;
	if (!vn->graph) return;
	for (i = 0; i < vn->graph->opc; ++i) {
//		printf("visit node %d\n", vn->graph->ops[i]);
		res = calc_bufs(o, vn->graph->ops[i]);
		if (res > count) count = res;
	}
//	printf("need %d buffers (have %d)\n", count, o->buf_count);
	if (count > o->buf_count) {
//		printf("new alloc size 0x%lu\n", sizeof(Buf) * count);
		o->bufs = realloc(o->bufs, sizeof(Buf) * count);
		o->buf_count = count;
	}
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(SGS_Program *prg, uint32_t srate) {
	SGS_Generator *o = calloc(1, sizeof(SGS_Generator));
	if (!o) return NULL;
	o->osc_coeff = SGS_Osc_SRATE_COEFF(srate);
	o->srate = srate;
	o->amp_scale = 1.f;
	o->event_count = prg->event_count;
	if (o->event_count > 0) {
		o->events = calloc(o->event_count, sizeof(EventNode));
		if (!o->events) {
			SGS_destroy_Generator(o);
			return NULL;
		}
	}
	o->voice_count = prg->voice_count;
	if (o->voice_count > 0) {
		o->voices = calloc(o->voice_count, sizeof(VoiceNode));
		if (!o->voices) {
			SGS_destroy_Generator(o);
			return NULL;
		}
		if ((prg->flags & SGS_PROG_AMP_DIV_VOICES) != 0)
			o->amp_scale /= o->voice_count;
	}
	o->operator_count = prg->operator_count;
	if (o->operator_count > 0) {
		o->operators = calloc(o->operator_count, sizeof(OperatorNode));
		if (!o->operators) {
			SGS_destroy_Generator(o);
			return NULL;
		}
	}
	size_t event_value_count = 0;
	for (size_t i = 0; i < o->event_count; ++i) {
		const SGS_ProgramEvent *ev = prg->events[i];
		event_value_count += count_flags(ev->params) +
			count_flags(ev->params &
				(SGS_P_VALITFREQ |
				SGS_P_VALITAMP |
				SGS_P_VALITPANNING)) * 2;
	}
	if (event_value_count > 0) {
		o->event_values = calloc(event_value_count, sizeof(EventValue));
		if (!o->event_values) {
			SGS_destroy_Generator(o);
			return NULL;
		}
	}
	SGS_global_init_Wave();
	/*
	 * Fill in event data according to the SGS_Program, i.e.
	 * copy timed state changes for voices and operators.
	 */
	EventValue *event_values = o->event_values;
	uint32_t indexwaittime = 0;
	for (size_t i = 0; i < o->event_count; ++i) {
		const SGS_ProgramEvent *prg_e = prg->events[i];
		EventNode *e = &o->events[i];
		EventValue *val = event_values;
		e->data = val;
		e->waittime = MS_TO_SRT(prg_e->wait_ms, srate);
		indexwaittime += e->waittime;
		e->voice_id = -1;
		e->operator_id = -1;
		e->params = prg_e->params;
		if (prg_e->operator) {
			const SGS_ProgramOperatorData *pod = prg_e->operator;
			e->voice_id = prg_e->voice_id;
			e->operator_id = pod->operator_id;
			if (e->params & SGS_P_ADJCS)
				(*val++).v = (void*)pod->adjcs;
			if (e->params & SGS_P_OPATTR)
				(*val++).i = pod->attr;
			if (e->params & SGS_P_WAVE)
				(*val++).i = pod->wave;
			if (e->params & SGS_P_TIME) {
				(*val++).i = (pod->time_ms == SGS_TIME_INF) ?
					SGS_TIME_INF :
					MS_TO_SRT(pod->time_ms, srate);
			}
			if (e->params & SGS_P_SILENCE)
				(*val++).i = MS_TO_SRT(pod->silence_ms, srate);
			if (e->params & SGS_P_FREQ)
				(*val++).f = pod->freq;
			if (e->params & SGS_P_VALITFREQ) {
				(*val++).i = MS_TO_SRT(pod->valitfreq.time_ms, srate);
				(*val++).f = pod->valitfreq.goal;
				(*val++).i = pod->valitfreq.type;
			}
			if (e->params & SGS_P_DYNFREQ)
				(*val++).f = pod->dynfreq;
			if (e->params & SGS_P_PHASE)
				(*val++).i = SGS_Osc_PHASE(pod->phase);
			if (e->params & SGS_P_AMP)
				(*val++).f = pod->amp;
			if (e->params & SGS_P_VALITAMP) {
				(*val++).i = MS_TO_SRT(pod->valitamp.time_ms, srate);
				(*val++).f = pod->valitamp.goal;
				(*val++).i = pod->valitamp.type;
			}
			if (e->params & SGS_P_DYNAMP)
				(*val++).f = pod->dynamp;
		}
		if (prg_e->voice) {
			const SGS_ProgramVoiceData *pvd = prg_e->voice;
			e->voice_id = prg_e->voice_id;
			if (e->params & SGS_P_GRAPH)
				(*val++).v = (void*)pvd->graph;
			if (e->params & SGS_P_VOATTR)
				(*val++).i = pvd->attr;
			if (e->params & SGS_P_PANNING)
				(*val++).f = pvd->panning;
			if (e->params & SGS_P_VALITPANNING) {
				(*val++).i = MS_TO_SRT(pvd->valitpanning.time_ms, srate);
				(*val++).f = pvd->valitpanning.goal;
				(*val++).i = pvd->valitpanning.type;
			}
			o->voices[e->voice_id].pos = -indexwaittime;
			indexwaittime = 0;
		}
		event_values = val;
	}
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Generator(SGS_Generator *o) {
	if (o->bufs) free(o->bufs);
	if (o->events) free(o->events);
	if (o->voices) free(o->voices);
	if (o->operators) free(o->operators);
	if (o->event_values) free(o->event_values);
	free(o);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *o, EventNode *e) {
  if (1) /* more types to be added in the future */ {
    const EventValue *val = e->data;
    /*
     * Set state of operator and/or voice. Voice updates must be done last,
     * as operator updates may change node adjacents and buffer recalculation
     * is currently done during voice updates.
     */
    if (e->operator_id >= 0) {
      OperatorNode *on = &o->operators[e->operator_id];
      if (e->params & SGS_P_ADJCS)
        on->adjcs = (*val++).v;
      if (e->params & SGS_P_OPATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        if (!(e->params & SGS_P_FREQ)) {
          /* May change during processing; preserve state of FREQRATIO flag */
          attr &= ~SGS_ATTR_FREQRATIO;
          attr |= on->attr & SGS_ATTR_FREQRATIO;
        }
        on->attr = attr;
      }
      if (e->params & SGS_P_WAVE)
        on->wave = (*val++).i;
      if (e->params & SGS_P_TIME)
        on->time = (*val++).i;
      if (e->params & SGS_P_SILENCE)
        on->silence = (*val++).i;
      if (e->params & SGS_P_FREQ)
        on->freq = (*val++).f;
      if (e->params & SGS_P_VALITFREQ) {
        on->valitfreq.time = (*val++).i;
        on->valitfreq.pos = 0;
        on->valitfreq.goal = (*val++).f;
        on->valitfreq.type = (*val++).i;
      }
      if (e->params & SGS_P_DYNFREQ)
        on->dynfreq = (*val++).f;
      if (e->params & SGS_P_PHASE)
        SGS_Osc_SET_PHASE(&on->osc, (uint32_t)(*val++).i);
      if (e->params & SGS_P_AMP)
        on->amp = (*val++).f;
      if (e->params & SGS_P_VALITAMP) {
        on->valitamp.time = (*val++).i;
        on->valitamp.pos = 0;
        on->valitamp.goal = (*val++).f;
        on->valitamp.type = (*val++).i;
      }
      if (e->params & SGS_P_DYNAMP)
        on->dynamp = (*val++).f;
    }
    if (e->voice_id >= 0) {
      VoiceNode *vn = &o->voices[e->voice_id];
      if (e->params & SGS_P_GRAPH)
        vn->graph = (*val++).v;
      if (e->params & SGS_P_VOATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        vn->attr = attr;
      }
      if (e->params & SGS_P_PANNING)
        vn->panning = (*val++).f;
      if (e->params & SGS_P_VALITPANNING) {
        vn->valitpanning.time = (*val++).i;
        vn->valitpanning.pos = 0;
        vn->valitpanning.goal = (*val++).f;
        vn->valitpanning.type = (*val++).i;
      }
      upsize_bufs(o, vn);
      vn->flags |= VN_INIT | VN_EXEC;
      vn->pos = 0;
      if ((int32_t)o->voice > e->voice_id) /* go back to re-activated node */
        o->voice = e->voice_id;
    }
  }
}

/*
 * Fill buffer with buf_len float values for a parameter; these may
 * either simply be a copy of the supplied state, or modified.
 *
 * If a parameter valit (VALue ITeration) is supplied, the values
 * are shaped according to its timing, target value and curve
 * selection. Once elapsed, the state will also be set to its final
 * value.
 *
 * Passing a modifier buffer will accordingly multiply each output
 * value, done to get absolute values from ratios.
 */
static bool run_param(BufValue *buf, uint32_t buf_len, ParameterValit *vi,
                       float *state, const BufValue *modbuf) {
  uint32_t i, end, len, filllen;
  double coeff;
  float s0 = *state;
  if (!vi) {
    filllen = buf_len;
    goto FILL;
  }
  coeff = 1.f / vi->time;
  len = vi->time - vi->pos;
  if (len > buf_len) {
    len = buf_len;
    filllen = 0;
  } else {
    filllen = buf_len - len;
  }
  switch (vi->type) {
  case SGS_VALIT_LIN:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      (*buf++).f = s0 + (vi->goal - s0) * (i * coeff);
    }
    break;
  case SGS_VALIT_EXP:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = 1.f - i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = vi->goal + (s0 - vi->goal) * mod;
    }
    break;
  case SGS_VALIT_LOG:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = s0 + (vi->goal - s0) * mod;
    }
    break;
  }
  if (modbuf) {
    buf -= len;
    for (i = 0; i < len; ++i) {
      (*buf++).f *= (*modbuf++).f;
    }
  }
  vi->pos += len;
  if (vi->time == vi->pos) {
    s0 = *state = vi->goal; /* when reached, valit target becomes new state */
  FILL:
    /*
     * Set the remaining values, if any, using the state.
     */
    if (modbuf) {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0 * modbuf[i].f;
    } else {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0;
    }
    return (vi != 0);
  }
  return 0;
}

/*
 * Generate up to buf_len samples for an operator node, the remainder (if any)
 * zero-filled if acc_ind is zero.
 *
 * Recursively visits the subnodes of the operator node in the process, if
 * any.
 *
 * Returns number of samples generated for the node.
 */
static uint32_t run_block(SGS_Generator *o, Buf *bufs, uint32_t buf_len,
                      OperatorNode *n, BufValue *parent_freq,
                      bool wave_env, uint32_t acc_ind) {
  uint32_t i, len;
  BufValue *sbuf, *freq, *freqmod, *pm, *amp;
  Buf *nextbuf = bufs + 1;
  ParameterValit *vi;
  uint32_t fmodc = 0, pmodc = 0, amodc = 0;
  if (n->adjcs) {
    fmodc = n->adjcs->fmodc;
    pmodc = n->adjcs->pmodc;
    amodc = n->adjcs->amodc;
  }
  sbuf = *bufs;
  len = buf_len;
  /*
   * If silence, zero-fill and delay processing for duration.
   */
  uint32_t zero_len = 0;
  if (n->silence) {
    zero_len = n->silence;
    if (zero_len > len)
      zero_len = len;
    if (!acc_ind) for (i = 0; i < zero_len; ++i)
      sbuf[i].i = 0;
    len -= zero_len;
    if (n->time != SGS_TIME_INF) n->time -= zero_len;
    n->silence -= zero_len;
    if (!len) return zero_len;
    sbuf += zero_len;
  }
  /*
   * Guard against circular references.
   */
  if ((n->flags & ON_VISITED) != 0) {
      for (i = 0; i < len; ++i)
        sbuf[i].i = 0;
      return zero_len + len;
  }
  n->flags |= ON_VISITED;
  /*
   * Limit length to time duration of operator.
   */
  uint32_t skip_len = 0;
  if (n->time < (int32_t)len && n->time != SGS_TIME_INF) {
    skip_len = len - n->time;
    len = n->time;
  }
  /*
   * Handle frequency (alternatively ratio) parameter, including frequency
   * modulation if modulators linked.
   */
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_VALITFREQ) {
    vi = &n->valitfreq;
    if (n->attr & SGS_ATTR_VALITFREQRATIO) {
      freqmod = parent_freq;
      if (!(n->attr & SGS_ATTR_FREQRATIO)) {
        n->attr |= SGS_ATTR_FREQRATIO;
        n->freq /= parent_freq[0].f;
      }
    } else {
      freqmod = 0;
      if (n->attr & SGS_ATTR_FREQRATIO) {
        n->attr &= ~SGS_ATTR_FREQRATIO;
        n->freq *= parent_freq[0].f;
      }
    }
  } else {
    vi = 0;
    freqmod = (n->attr & SGS_ATTR_FREQRATIO) ? parent_freq : 0;
  }
  if (run_param(freq, len, vi, &n->freq, freqmod))
    n->attr &= ~(SGS_ATTR_VALITFREQ|SGS_ATTR_VALITFREQRATIO);
  if (fmodc) {
    const int32_t *fmods = n->adjcs->adjcs;
    BufValue *fmbuf;
    for (i = 0; i < fmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, true, i);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parent_freq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  /*
   * If phase modulators linked, get phase offsets for modulation.
   */
  pm = 0;
  if (pmodc) {
    const int32_t *pmods = &n->adjcs->adjcs[fmodc];
    for (i = 0; i < pmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, false, i);
    pm = *(nextbuf++);
  }
  if (!wave_env) {
    /*
     * Handle amplitude parameter, including amplitude modulation if
     * modulators linked.
     */
    if (amodc) {
      const int32_t *amods = &n->adjcs->adjcs[fmodc+pmodc];
      float dynampdiff = n->dynamp - n->amp;
      for (i = 0; i < amodc; ++i)
        run_block(o, nextbuf, len, &o->operators[amods[i]], freq, true, i);
      amp = *(nextbuf++);
      for (i = 0; i < len; ++i)
        amp[i].f = n->amp + amp[i].f * dynampdiff;
    } else {
      amp = *(nextbuf++);
      vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
      if (run_param(amp, len, vi, &n->amp, 0))
        n->attr &= ~SGS_ATTR_VALITAMP;
    }
    /*
     * Generate integer output - either for voice output or phase modulation
     * input.
     */
    const int16_t *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      int32_t s, spm = 0;
      float sfreq = freq[i].f, samp = amp[i].f;
      if (pm) spm = pm[i].i;
      SGS_Osc_RUN_S16(&n->osc, lut, o->osc_coeff, sfreq, spm, samp, s);
      if (acc_ind) s += sbuf[i].i;
      sbuf[i].i = s;
    }
  } else {
    /*
     * Generate float output - used as waveform envelopes for modulating
     * frequency or amplitude.
     */
    const int16_t *lut = SGS_Wave_luts[n->wave];
    for (i = 0; i < len; ++i) {
      float s, sfreq = freq[i].f;
      int32_t spm = 0;
      if (pm) spm = pm[i].i;
      SGS_Osc_RUN_SF(&n->osc, lut, o->osc_coeff, sfreq, spm, s);
      if (acc_ind) s *= sbuf[i].f;
      sbuf[i].f = s;
    }
  }
  /*
   * Update time duration left, zero rest of buffer if unfilled.
   */
  if (n->time != SGS_TIME_INF) {
    if (!acc_ind && skip_len > 0) {
      sbuf += len;
      for (i = 0; i < skip_len; ++i)
        sbuf[i].i = 0;
    }
    n->time -= len;
  }
  n->flags &= ~ON_VISITED;
  return zero_len + len;
}

/*
 * Mix output for voice node \p vn into the 16-bit stereo (interleaved)
 * \p st_out buffer from the first generator buffer. Advances the st_out
 * pointer.
 *
 * The second generator buffer is used for panning if dynamic panning
 * is used.
 */
static void mix_output(SGS_Generator *o, VoiceNode *vn, int16_t **st_out,
		uint32_t len) {
	BufValue *s_buf = o->bufs[0];
	float scale = o->amp_scale;
	if (vn->attr & SGS_ATTR_VALITPANNING) {
		BufValue *pan_buf = o->bufs[1];
		if (run_param(pan_buf, len, &vn->valitpanning,
				&vn->panning, 0)) {
			vn->attr &= ~SGS_ATTR_VALITPANNING;
		}
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i].i * scale;
			float p = s * pan_buf[i].f;
			*(*st_out)++ += lrintf(s - p);
			*(*st_out)++ += lrintf(p);
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i].i * scale;
			float p = s * vn->panning;
			*(*st_out)++ += lrintf(s - p);
			*(*st_out)++ += lrintf(p);
		}
	}
}

/*
 * Generate up to buf_len samples for a voice, these mixed into the
 * interleaved output stereo buffer by simple addition.
 *
 * Returns number of samples generated for the voice.
 */
static uint32_t run_voice(SGS_Generator *o, VoiceNode *vn,
		int16_t *out, uint32_t buf_len) {
  uint32_t out_len = 0;
  bool finished = true;
  if (!vn->graph) goto RETURN;
  uint32_t opc = vn->graph->opc;
  const int32_t *ops = vn->graph->ops;
  int32_t time = 0;
  uint32_t i;
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    if (n->time == 0) continue;
    if (n->time > time && n->time != SGS_TIME_INF) time = n->time;
  }
  if (time > (int32_t)buf_len) time = buf_len;
  /*
   * Repeatedly generate up to BUF_LEN samples until done.
   */
  int16_t *sp = out;
  while (time) {
    uint32_t acc_ind = 0;
    uint32_t gen_len = 0;
    uint32_t len = (time < BUF_LEN) ? time : BUF_LEN;
    time -= len;
    for (i = 0; i < opc; ++i) {
      uint32_t last_len;
      OperatorNode *n = &o->operators[ops[i]];
      if (n->time == 0) continue;
      last_len = run_block(o, o->bufs, len, n, 0, false, acc_ind++);
      if (last_len > gen_len) gen_len = last_len;
    }
    if (!gen_len) goto RETURN;
    mix_output(o, vn, &sp, gen_len);
    out_len += gen_len;
  }
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    if (n->time != 0) {
      finished = false;
      break;
    }
  }
RETURN:
  vn->pos += out_len;
  if (finished)
    vn->flags &= ~VN_EXEC;
  return out_len;
}

/**
 * Main sound generation/processing function. Call repeatedly to write
 * buf_len new samples into the interleaved stereo buffer buf until
 * the signal ends; any values after the end will be zero'd.
 *
 * If supplied, out_len will be set to the precise length generated.
 * for the call, which is buf_len unless the signal ended earlier.
 *
 * Return true as long as there are more samples to generate, false
 * when the end of the signal has been reached.
 */
bool SGS_Generator_run(SGS_Generator *o, int16_t *buf, size_t buf_len,
		size_t *out_len) {
  int16_t *sp = buf;
  uint32_t i, len = buf_len;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
  uint32_t skip_len, gen_len = 0;
PROCESS:
  skip_len = 0;
  while (o->event < o->event_count) {
    EventNode *e = &o->events[o->event];
    if (o->event_pos < e->waittime) {
      uint32_t waittime = e->waittime - o->event_pos;
      if (waittime < len) {
        /*
         * Limit len to waittime, further splitting processing into two
         * blocks; otherwise, voice processing could get ahead of event
         * handling in some cases - which would give undefined results!
         */
        skip_len = len - waittime;
        len = waittime;
      }
      o->event_pos += len;
      break;
    }
    handle_event(o, e);
    ++o->event;
    o->event_pos = 0;
  }
  uint32_t last_len = 0;
  for (i = o->voice; i < o->voice_count; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint32_t waittime = -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->flags & VN_EXEC) {
      uint32_t voice_len = run_voice(o, vn, buf, len);
      if (voice_len > last_len) last_len = voice_len; 
    }
  }
  gen_len += last_len;
  if (skip_len) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skip_len;
    goto PROCESS;
  }
  /*
   * Advance starting voice and check for end of signal.
   */
  for(;;) {
    VoiceNode *vn;
    if (o->voice == o->voice_count) {
      if (o->event != o->event_count) break;
      /*
       * The end.
       */
      if (out_len) *out_len = gen_len;
      return false;
    }
    vn = &o->voices[o->voice];
    if (!(vn->flags & VN_INIT) || vn->flags & VN_EXEC) break;
    ++o->voice;
  }
  /*
   * Further calls needed to complete signal.
   */
  if (out_len) *out_len = buf_len;
  return true;
}

#endif

#if 0 // PROGRAM

#include "sgensys.h"
#include "program.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>

static void print_linked(const char *header, const char *footer, uint count,
		const int *nodes) {
	uint i;
	if (!count) return;
	printf("%s%d", header, nodes[0]);
	for (i = 0; ++i < count; )
		printf(", %d", nodes[i]);
	printf("%s", footer);
}

#if 0 /* old program.c code */

static void build_graph(SGS_ProgramEvent *root, const SGSEventNode *voice_in) {
	SGSOperatorNode **nl;
	SGSImGraph *graph, **graph_out;
	uint i;
	uint size;
	if (!voice_in->voice_params & SGS_GRAPH)
		return;
	size = voice_in->graph.count;
	graph_out = (SGSImGraph**)&root->voice->graph;
	if (!size) {
		*graph_out = graph;
		return;
	}
	nl = SGS_NODE_LIST_GET(&voice_in->graph);
	graph = malloc(sizeof(SGSImGraph) + sizeof(int) * (size - 1));
	graph->opc = size;
	for (i = 0; i < size; ++i)
		graph->ops[i] = nl[i]->operator_id;
	*graph_out = graph;
}

static void build_adjcs(SGS_ProgramEvent *root,
		const SGSOperatorNode *operator_in) {
	SGSOperatorNode **nl;
	SGSImGraphNodeAdjcs *adjcs, **adjcs_out;
	int *data;
	uint i;
	uint size;
	if (!operator_in || !(operator_in->operator_params & SGS_ADJCS))
		return;
	size = operator_in->fmods.count +
				 operator_in->pmods.count +
				 operator_in->amods.count;
	adjcs_out = (SGSImGraphNodeAdjcs**)&root->operator->adjcs;
	if (!size) {
		*adjcs_out = 0;
		return;
	}
	adjcs = malloc(sizeof(SGSImGraphNodeAdjcs) + sizeof(int) * (size - 1));
	adjcs->fmodc = operator_in->fmods.count;
	adjcs->pmodc = operator_in->pmods.count;
	adjcs->amodc = operator_in->amods.count;
	data = adjcs->adjcs;
	nl = SGS_NODE_LIST_GET(&operator_in->fmods);
	for (i = 0; i < adjcs->fmodc; ++i)
		*data++ = nl[i]->operator_id;
	nl = SGS_NODE_LIST_GET(&operator_in->pmods);
	for (i = 0; i < adjcs->pmodc; ++i)
		*data++ = nl[i]->operator_id;
	nl = SGS_NODE_LIST_GET(&operator_in->amods);
	for (i = 0; i < adjcs->amodc; ++i)
		*data++ = nl[i]->operator_id;
	*adjcs_out = adjcs;
}

#endif



/*
static void build_voicedata(SGS_ProgramEvent *root,
		const SGSEventNode *voice_in) {
	SGSOperatorNode **nl;
	SGS_ProgramVoiceData *graph, **graph_out;
	uint i;
	uint size;
	if (!voice_in->voice_params & SGS_GRAPH)
		return;
	size = voice_in->graph.count;
	graph_out = (SGSImGraph**)&root->voice->graph;
	if (!size) {
		*graph_out = graph;
		return;
	}
	nl = SGS_NODE_LIST_GET(&voice_in->graph);
	graph = malloc(sizeof(SGSImGraph) + sizeof(int) * (size - 1));
	graph->opc = size;
	for (i = 0; i < size; ++i)
		graph->ops[i] = nl[i]->operator_id;
	*graph_out = graph;
}
*/


/*
 * Program (event, voice, operator) allocation
 */

typedef struct VoiceAllocData {
	SGSEventNode *last;
	uint duration_ms;
} VoiceAllocData;

typedef struct VoiceAlloc {
	VoiceAllocData *data;
	uint voicec;
	uint alloc;
} VoiceAlloc;

#define VOICE_ALLOC_INIT(va) do{ \
	(va)->data = calloc(1, sizeof(VoiceAllocData)); \
	(va)->voicec = 0; \
	(va)->alloc = 1; \
}while(0)

#define VOICE_ALLOC_FINI(va, prg) do{ \
	(prg)->voicec = (va)->voicec; \
	free((va)->data); \
}while(0)

/*
 * Returns the longest operator duration among top-level operators for
 * the graph of the voice event.
 */
static uint voice_duration(SGSEventNode *ve) {
	SGSOperatorNode **nl = SGS_NODE_LIST_GET(&ve->operators);
	uint i, duration_ms = 0;
	/* FIXME: node list type? */
	for (i = 0; i < ve->operators.count; ++i) {
		SGSOperatorNode *op = nl[i];
		if (op->time_ms > (int)duration_ms)
			duration_ms = op->time_ms;
	}
	return duration_ms;
}

/*
 * Incremental voice allocation - allocate voice for event,
 * returning voice id.
 */
static uint voice_alloc_inc(VoiceAlloc *va, SGSEventNode *e) {
	uint voice;
	for (voice = 0; voice < va->voicec; ++voice) {
		if ((int)va->data[voice].duration_ms < e->wait_ms)
			va->data[voice].duration_ms = 0;
		else
			va->data[voice].duration_ms -= e->wait_ms;
	}
	if (e->voice_prev) {
		SGSEventNode *prev = e->voice_prev;
		voice = prev->voice_id;
	} else {
		for (voice = 0; voice < va->voicec; ++voice)
			if (!(va->data[voice].last->en_flags & EN_VOICE_LATER_USED) &&
					va->data[voice].duration_ms == 0) break;
		/*
		 * If no unused voice found, allocate new one.
		 */
		if (voice == va->voicec) {
			++va->voicec;
			if (va->voicec > va->alloc) {
				uint i = va->alloc;
				va->alloc <<= 1;
				va->data = realloc(va->data, va->alloc * sizeof(VoiceAllocData));
				while (i < va->alloc) {
					va->data[i].last = 0;
					va->data[i].duration_ms = 0;
					++i;
				}
			}
		}
	}
	e->voice_id = voice;
	va->data[voice].last = e;
//	if (e->voice_params & SGS_GRAPH)
		va->data[voice].duration_ms = voice_duration(e);
	return voice;
}

typedef struct OperatorAllocData {
	SGSOperatorNode *last;
	SGS_ProgramEvent *out;
	uint duration_ms;
	uchar visited; /* used in graph traversal */
} OperatorAllocData;

typedef struct OperatorAlloc {
	OperatorAllocData *data;
	uint operatorc;
	uint alloc;
} OperatorAlloc;

#define OPERATOR_ALLOC_INIT(oa) do{ \
	(oa)->data = calloc(1, sizeof(OperatorAllocData)); \
	(oa)->operatorc = 0; \
	(oa)->alloc = 1; \
}while(0)

#define OPERATOR_ALLOC_FINI(oa, prg) do{ \
	(prg)->operatorc = (oa)->operatorc; \
	free((oa)->data); \
}while(0)

/*
 * Incremental operator allocation - allocate operator for event,
 * returning operator id.
 *
 * Only valid to call for single-operator nodes.
 */
static uint operator_alloc_inc(OperatorAlloc *oa, SGSOperatorNode *op) {
	SGSEventNode *e = op->event;
	uint operator;
	for (operator = 0; operator < oa->operatorc; ++operator) {
		if ((int)oa->data[operator].duration_ms < e->wait_ms)
			oa->data[operator].duration_ms = 0;
		else
			oa->data[operator].duration_ms -= e->wait_ms;
	}
	if (op->on_prev) {
		SGSOperatorNode *pop = op->on_prev;
		operator = pop->operator_id;
	} else {
//		for (operator = 0; operator < oa->operatorc; ++operator)
//			if (!(oa->data[operator].last->on_flags & ON_OPERATOR_LATER_USED) &&
//					oa->data[operator].duration_ms == 0) break;
		/*
		 * If no unused operator found, allocate new one.
		 */
		if (operator == oa->operatorc) {
			++oa->operatorc;
			if (oa->operatorc > oa->alloc) {
				uint i = oa->alloc;
				oa->alloc <<= 1;
				oa->data = realloc(oa->data, oa->alloc * sizeof(OperatorAllocData));
				while (i < oa->alloc) {
					oa->data[i].last = 0;
					oa->data[i].duration_ms = 0;
					++i;
				}
			}
		}
	}
	op->operator_id = operator;
	oa->data[operator].last = op;
//	oa->data[operator].duration_ms = op->time_ms;
	return operator;
}

typedef struct ProgramAlloc {
	SGS_ProgramEvent *oe, **oevents;
	uint eventc;
	uint alloc;
	OperatorAlloc oa;
	VoiceAlloc va;
} ProgramAlloc;

#define PROGRAM_ALLOC_INIT(pa) do{ \
	VOICE_ALLOC_INIT(&(pa)->va); \
	OPERATOR_ALLOC_INIT(&(pa)->oa); \
	(pa)->oe = 0; \
	(pa)->oevents = 0; \
	(pa)->eventc = 0; \
	(pa)->alloc = 0; \
}while(0)

#define PROGRAM_ALLOC_FINI(pa, prg) do{ \
	uint i; \
	/* copy output events to program & cleanup */ \
	*(SGS_ProgramEvent**)&(prg)->events = malloc(sizeof(SGS_ProgramEvent) * \
		(pa)->eventc); \
	for (i = 0; i < (pa)->eventc; ++i) { \
		*(SGS_ProgramEvent*)&(prg)->events[i] = *(pa)->oevents[i]; \
		free((pa)->oevents[i]); \
	} \
	free((pa)->oevents); \
	(prg)->eventc = (pa)->eventc; \
	OPERATOR_ALLOC_FINI(&(pa)->oa, (prg)); \
	VOICE_ALLOC_FINI(&(pa)->va, (prg)); \
}while(0)

static SGS_ProgramEvent *program_alloc_oevent(ProgramAlloc *pa) {
	++pa->eventc;
	if (pa->eventc > pa->alloc) {
		pa->alloc = (pa->alloc > 0) ? pa->alloc << 1 : 1;
		pa->oevents = realloc(pa->oevents, sizeof(SGS_ProgramEvent*) * pa->alloc);
	}
	pa->oevents[pa->eventc - 1] = calloc(1, sizeof(SGS_ProgramEvent));
	pa->oe = pa->oevents[pa->eventc - 1];
	return pa->oe;
}

/*
 * Graph creation and traversal
 */

#if 0
typedef struct SGSImGraph {
  uchar opc;
  struct SGSImGraphNodeAdjcs *ops[1]; /* sized to opc */
} SGSImGraph;

typedef struct SGSImGraphNodeAdjcs {
  uchar pmodc;
  uchar fmodc;
  uchar amodc;
  uchar level;  /* index for buffer used to store result to use if node
                   revisited when traversing the graph. */
  struct SGSImGraphNodeAdjcs *adjcs[1]; /* sized to total number */
} SGSImGraphNodeAdjcs;

static SGSImGraphNodeAdjcs *create_adjcs(ProgramAlloc *pa,
		const SGSOperatorNode *operator_in) {
	SGSOperatorNode **nl;
	SGSImGraphNodeAdjcs *adjcs;
	SGSImGraphNodeAdjcs **data;
	uint i;
	uint size;
	if (!operator_in || !(operator_in->event->voice_params & SGS_OPLIST))
		return NULL;
	size = operator_in->fmods.count +
		operator_in->pmods.count +
		operator_in->amods.count;
	if (size == 0) {
		return NULL;
	}
	adjcs = malloc(sizeof(SGSImGraphNodeAdjcs) +
		sizeof(SGSImGraphNodeAdjcs*) * (size - 1));
	adjcs->fmodc = operator_in->fmods.count;
	adjcs->pmodc = operator_in->pmods.count;
	adjcs->amodc = operator_in->amods.count;
	data = adjcs->adjcs;
	nl = SGS_NODE_LIST_GET(&operator_in->fmods);
	for (i = 0; i < adjcs->fmodc; ++i) {
		int op_id = nl[i]->operator_id;
		OperatorAllocData *op_data = &pa->oa.data[op_id];
		SGSOperatorNode *last = op_data->last;
		*data++ = create_adjcs(pa, last);
	}
	nl = SGS_NODE_LIST_GET(&operator_in->pmods);
	for (i = 0; i < adjcs->pmodc; ++i) {
		int op_id = nl[i]->operator_id;
		OperatorAllocData *op_data = &pa->oa.data[op_id];
		SGSOperatorNode *last = op_data->last;
		*data++ = create_adjcs(pa, last);
	}
	nl = SGS_NODE_LIST_GET(&operator_in->amods);
	for (i = 0; i < adjcs->amodc; ++i) {
		int op_id = nl[i]->operator_id;
		OperatorAllocData *op_data = &pa->oa.data[op_id];
		SGSOperatorNode *last = op_data->last;
		*data++ = create_adjcs(pa, last);
	}
	return adjcs;
}

static void destroy_adjcs(SGSImGraphNodeAdjcs *o) {
	uint i, total;
	if (o == NULL) return;
	total = o->fmodc +
		o->pmodc +
		o->amodc;
	for (i = 0; i < total; ++i) {
		destroy_adjcs(o->adjcs[i]);
	}
	free(o);
}

static SGSImGraph *create_graph(ProgramAlloc *pa,
		const SGSEventNode *voice_in) {
	SGSOperatorNode **nl;
	SGSImGraph *graph;
	uint i;
	uint size;
	if (!voice_in->voice_params & SGS_OPLIST)
		return NULL;
	size = voice_in->graph.count;
	if (!size) {
		return NULL;
	}
	graph = malloc(sizeof(SGSImGraph) +
		sizeof(SGSImGraphNodeAdjcs*) * (size - 1));
	graph->opc = size;
	nl = SGS_NODE_LIST_GET(&voice_in->graph);
	for (i = 0; i < size; ++i) {
		int op_id = nl[i]->operator_id;
		OperatorAllocData *op_data = &pa->oa.data[op_id];
		SGSOperatorNode *last = op_data->last;
		graph->ops[i] = create_adjcs(pa, last);
	}
	return graph;
}

static void destroy_graph(SGSImGraph *o) {
	uint i;
	for (i = 0; i < o->opc; ++i) {
		destroy_adjcs(o->ops[i]);
	}
	free(o);
}
#endif

#if 0 /* generator.c */
static uint run_block(SGSGenerator *o, Buf *blocks, uint buf_len,
		OperatorNode *n, BufData *parent_freq, uchar wave_env,
		uint acc_ind) {
	uint i, len, zero_len, skip_len;
	BufData *freq, *freqmod, *pm, *amp;
	BufData *sbuf = *blocks;
	Buf *nextbuf = blocks + 1;
	uchar fmodc, pmodc, amodc;
	/*
	 * Handle frequency (alternatively ratio) parameter, including frequency
	 * modulation if modulators linked.
	 */
	freq = *(nextbuf++);
	if (n->attr & SGS_ATTR_VALITFREQ) {
		if (n->attr & SGS_ATTR_VALITFREQRATIO) {
			freqmod = parent_freq;
			if (!(n->attr & SGS_ATTR_FREQRATIO)) {
				n->attr |= SGS_ATTR_FREQRATIO;
				n->freq /= parent_freq[0].f;
			}
		} else {
			freqmod = 0;
			if (n->attr & SGS_ATTR_FREQRATIO) {
				n->attr &= ~SGS_ATTR_FREQRATIO;
				n->freq *= parent_freq[0].f;
			}
		}
	} else {
		freqmod = (n->attr & SGS_ATTR_FREQRATIO) ? parent_freq : 0;
	}
	if (run_param(freq, len, vi, &n->freq, freqmod))
		n->attr &= ~(SGS_ATTR_VALITFREQ|SGS_ATTR_VALITFREQRATIO);
	if (fmodc) {
		const int *fmods = n->adjcs->adjcs;
		BufData *fmbuf;
		for (i = 0; i < fmodc; ++i)
			run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, 1, i);
		fmbuf = *nextbuf;
		if (n->attr & SGS_ATTR_FREQRATIO) {
			for (i = 0; i < len; ++i)
				freq[i].f += (n->dynfreq * parent_freq[i].f - freq[i].f) * fmbuf[i].f;
		} else {
			for (i = 0; i < len; ++i)
				freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
		}
	}
	/*
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	pm = 0;
	if (pmodc) {
		const int *pmods = &n->adjcs->adjcs[fmodc];
		for (i = 0; i < pmodc; ++i)
			run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, 0, i);
		pm = *(nextbuf++);
	}
	if (!wave_env) {
		/*
		 * Handle amplitude parameter, including amplitude modulation if
		 * modulators linked.
		 */
		if (amodc) {
			const int *amods = &n->adjcs->adjcs[fmodc+pmodc];
			float dynampdiff = n->dynamp - n->amp;
			for (i = 0; i < amodc; ++i)
				run_block(o, nextbuf, len, &o->operators[amods[i]], freq, 1, i);
			amp = *(nextbuf++);
			for (i = 0; i < len; ++i)
				amp[i].f = n->amp + amp[i].f * dynampdiff;
		} else {
			amp = *(nextbuf++);
			vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
			if (run_param(amp, len, vi, &n->amp, 0))
				n->attr &= ~SGS_ATTR_VALITAMP;
		}
		/*
		 * Generate integer output - either for voice output or phase modulation
		 * input.
		 */
		for (i = 0; i < len; ++i) {
			int s, spm = 0;
			float sfreq = freq[i].f, samp = amp[i].f;
			if (pm) spm = pm[i].i;
			SGSOsc_RUN_PM(&n->osc, n->osctype, o->osc_coeff, sfreq, spm, samp, s);
			if (acc_ind) s += sbuf[i].i;
			sbuf[i].i = s;
		}
	} else {
		/*
		 * Generate float output - used as waveform envelopes for modulating
		 * frequency or amplitude.
		 */
		for (i = 0; i < len; ++i) {
			float s, sfreq = freq[i].f;
			int spm = 0;
			if (pm) spm = pm[i].i;
			SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, o->osc_coeff, sfreq, spm, s);
			if (acc_ind) s *= sbuf[i].f;
			sbuf[i].f = s;
		}
	}
}
#endif

static const int *traverse_graph(SGSImGraph *operator_graph) {
	const int *op_list;
	
	return op_list;
}

static void reset_graph(ProgramAlloc *pa) {
	OperatorAlloc *oa = &pa->oa;
	uint i;
	for (i = 0; i < oa->operatorc; ++i) {
		OperatorAllocData *entry = &oa->data[i];
		entry->visited = 0;
	}
}

/*
 * Overwrite parameters in dst that have values in src.
 */
static void copy_params(SGSOperatorNode *dst, const SGSOperatorNode *src) {
	if (src->operator_params & SGS_AMP) dst->amp = src->amp;
}

static void expand_operator(SGSOperatorNode *op) {
	SGSOperatorNode *pop;
	if (!(op->on_flags & ON_MULTIPLE_OPERATORS)) return;
	pop = op->on_prev;
	do {
		copy_params(pop, op);
		expand_operator(pop);
	} while ((pop = pop->next_bound));
	SGS_node_list_clear(&op->fmods);
	SGS_node_list_clear(&op->pmods);
	SGS_node_list_clear(&op->amods);
	op->operator_params = 0;
}

/*
 * Convert data for an operator node to program operator data, setting it for
 * the program event given.
 */
static void program_convert_onode(ProgramAlloc *pa, SGSOperatorNode *op,
		uint operator_id) {
	SGS_ProgramEvent *oe = pa->oa.data[operator_id].out;
	SGS_ProgramOperatorData *ood = calloc(1, sizeof(SGS_ProgramOperatorData));
	oe->operator = ood;
	oe->params |= op->operator_params;
	//printf("operator_id == %d | address == %x\n", op->operator_id, op);
	ood->operator_id = operator_id;
//	ood->output_block_id = 
	ood->freq_block_id = -1;
	ood->freq_mod_block_id = -1;
	ood->phase_mod_block_id = -1;
	ood->amp_block_id = -1;
	ood->amp_mod_block_id = -1;
	ood->attr = op->attr;
	ood->wave = op->wave;
	ood->time_ms = op->time_ms;
	ood->silence_ms = op->silence_ms;
	ood->freq = op->freq;
	ood->dynfreq = op->dynfreq;
	ood->phase = op->phase;
	ood->amp = op->amp;
	ood->dynamp = op->dynamp;
	ood->valitfreq = op->valitfreq;
	ood->valitamp = op->valitamp;
}

/*
 * Visit each operator node in the node list and recurse through each node's
 * sublists in turn, following and converting operator data and allocating
 * new output events as needed.
 */
static void program_follow_onodes(ProgramAlloc *pa, SGSNodeList *nl) {
	uint i;
	SGSOperatorNode **list = SGS_NODE_LIST_GET(nl);
	for (i = nl->inactive_count; i < nl->count; ++i) {
		SGSOperatorNode *op = list[i];
		OperatorAllocData *ad;
		uint operator_id;
		if (op->on_flags & ON_MULTIPLE_OPERATORS) continue;
		operator_id = operator_alloc_inc(&pa->oa, op);
		program_follow_onodes(pa, &op->fmods);
		program_follow_onodes(pa, &op->pmods);
		program_follow_onodes(pa, &op->amods);
		ad = &pa->oa.data[operator_id];
		if (pa->oe->operator) {
			/*
			 * Need a new output event for this operator;
			 * the current one is already used.
			 */
			program_alloc_oevent(pa);
		}
		ad->out = pa->oe;
		program_convert_onode(pa, op, operator_id);
	}
}

/*
 * Convert voice and operator data for an event node into a (series of) output
 * events.
 *
 * This is the "main" parser data conversion function, to be called for every
 * event.
 */
static void program_convert_enode(ProgramAlloc *pa, SGSEventNode *e) {
	SGS_ProgramEvent *oe;
	SGS_ProgramVoiceData *ovd;
	uint voice_id;
	/* Add to final output list */
	voice_id = voice_alloc_inc(&pa->va, e);
	oe = program_alloc_oevent(pa);
	oe->wait_ms = e->wait_ms;
	if (e->voice_params & SGS_OPLIST) {
	}
	program_follow_onodes(pa, &e->operators);
	oe = pa->oe; /* oe may have re(al)located */
	if (e->voice_params & SGS_OPLIST) {
		SGSImGraph *graph;
		ovd = calloc(1, sizeof(SGS_ProgramVoiceData));
		oe->voice = ovd;
		oe->params |= e->voice_params;
		ovd->voice_id = voice_id;
		ovd->attr = e->voice_attr;
		ovd->panning = e->panning;
		ovd->valitpanning = e->valitpanning;
		graph = create_graph(pa, e);
		destroy_graph(graph);
	}
}

static SGS_Program* build(SGSParser *o) {
	//puts("build():");
	ProgramAlloc pa;
	SGS_Program *prg = calloc(1, sizeof(SGS_Program));
	SGSEventNode *e;
	uint id;
	/*
	 * Pass #1 - Output event allocation, voice allocation,
	 *					 parameter data copying.
	 */
	PROGRAM_ALLOC_INIT(&pa);
	for (e = o->events; e; e = e->next) {
		program_convert_enode(&pa, e);
	}
	PROGRAM_ALLOC_FINI(&pa, prg);
	/*
	 * Pass #2 - Cleanup of parsing data.
	 */
	for (e = o->events; e; ) {
		SGSEventNode *e_next = e->next;
		SGS_event_node_destroy(e);
		e = e_next;
	}
	//puts("/build()");
#if 1
	/*
	 * Debug printing.
	 */
	putchar('\n');
	printf("events: %d\tvoices: %d\toperators: %d\n", prg->eventc, prg->voicec, prg->operatorc);
	for (id = 0; id < prg->eventc; ++id) {
		const SGS_ProgramEvent *oe;
		const SGS_ProgramVoiceData *ovo;
		const SGS_ProgramOperatorData *oop;
		oe = &prg->events[id];
		ovo = oe->voice;
		oop = oe->operator;
		printf("\\%d \tEV %d", oe->wait_ms, id);
		if (ovo) {
//			const SGSImGraph *g = ovo->graph;
			printf("\n\tvo %d", ovo->voice_id);
//			if (g)
//				print_linked("\n\t		{", "}", g->opc, g->ops);
		}
		if (oop) {
//			const SGSImGraphNodeAdjcs *ga = oop->adjcs;
			if (oop->time_ms == SGS_TIME_INF)
				printf("\n\top %d \tt=INF \tf=%.f", oop->operator_id, oop->freq);
			else
				printf("\n\top %d \tt=%d \tf=%.f", oop->operator_id, oop->time_ms, oop->freq);
//			if (ga) {
//				print_linked("\n\t		f!<", ">", ga->fmodc, ga->adjcs);
//				print_linked("\n\t		p!<", ">", ga->pmodc, &ga->adjcs[ga->fmodc]);
//				print_linked("\n\t		a!<", ">", ga->amodc, &ga->adjcs[ga->fmodc +
//					ga->pmodc]);
//			}
		}
		putchar('\n');
	}
#endif
	return prg;
}

SGS_Program* SGS_program_create(const char *filename) {
	SGSParser p;
	FILE *f = fopen(filename, "r");
	if (!f) return 0;

	SGS_parse(&p, f, filename);
	fclose(f);
	return build(&p);
}

void SGS_program_destroy(SGS_Program *o) {
	uint i;
	for (i = 0; i < o->eventc; ++i) {
		SGS_ProgramEvent *e = (void*)&o->events[i];
		if (e->voice) {
			free((void*)e->voice->operator_list);
			free((void*)e->voice);
		}
		if (e->operator) {
			free((void*)e->operator);
		}
	}
	free((void*)o->events);
}

#endif
