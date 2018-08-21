/* sgensys: Sound generator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "generator.h"
#include "osc.h"
#include "garr.h"
#include <stdio.h>
#include <stdlib.h>

#define MS_TO_SRT(ms, srate) \
	lrintf(((ms) * .001f) * (srate))

typedef union BufValue {
	int32_t i;
	float f;
} BufValue;

#define BUF_LEN 256
typedef BufValue Buf[BUF_LEN];

// maximum number of buffers needed for op nesting depth
#define BUF_COUNT(op_nest_depth) (((op_nest_depth) + 1) * 5)

typedef struct ParameterValit {
	uint32_t time, pos;
	float goal;
	uint8_t type;
} ParameterValit;

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
};

typedef struct OperatorNode {
	SGS_Osc osc;
	uint32_t time;
	uint32_t silence;
	uint8_t flags;
	uint8_t attr;
	SGS_wave_t wave;
	const SGS_ProgramOpAdjcs *adjcs;
	float amp, dynamp;
	float freq, dynfreq;
	ParameterValit valitamp, valitfreq;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
};

typedef struct VoiceNode {
	int32_t pos; /* negative for wait time */
	uint32_t duration;
	uint8_t flags;
	uint8_t attr;
	const SGS_ProgramOpRef *op_list;
	uint32_t op_count;
	float panning;
	ParameterValit valitpanning;
} VoiceNode;

typedef union EventValue {
	int32_t i;
	float f;
} EventValue;

typedef struct EventOpData {
	uint32_t id;
	uint32_t params;
	const SGS_ProgramOpAdjcs *adjcs;
} EventOpData;

typedef struct EventVoData {
	uint16_t id;
	uint32_t params;
	const SGS_ProgramOpRef *op_list;
	uint32_t op_count;
} EventVoData;

typedef struct EventNode {
	EventVoData vd;
	EventOpData *od;
	EventValue *vals;
	uint32_t waittime;
	uint32_t od_count;
} EventNode;

struct SGS_Generator {
	double osc_coeff;
	uint32_t srate;
	uint32_t buf_count;
	Buf *bufs;
	size_t event, ev_count;
	EventNode *events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	float amp_scale;
	uint32_t op_count;
	OperatorNode *operators;
	EventValue *ev_values;
	EventOpData *ev_op_data;
};

/*
 * Program conversion
 */

static uint32_t count_flags(uint32_t flags) {
	uint32_t i, count = 0;
	for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
		count += flags & 1;
		flags >>= 1;
	}
	return count;
}

static size_t count_ev_values(const SGS_ProgramEvent *e) {
	size_t count = 0;
	uint32_t params;
	if (e->vo_data) {
		params = e->vo_data->params;
		params &= ~(SGS_VOP_GRAPH | SGS_VOP_OPLIST);
		count += count_flags(params);
		if ((params & SGS_VOP_VALITPANNING) != 0) count += 2;
	}
	for (size_t i = 0; i < e->op_data_count; ++i) {
		params = e->op_data[i].params;
		params &= ~(SGS_OPP_ADJCS);
		count += count_flags(params);
		if ((params & SGS_OPP_VALITFREQ) != 0) count += 2;
		if ((params & SGS_OPP_VALITAMP) != 0) count += 2;
	}
	return count;
}

typedef struct OpConvState {
	const EventOpData *last_od;
	const SGS_ProgramOpAdjcs *adjcs;
	uint32_t flags;
} OpConvState;

typedef struct VoConvState {
	const EventVoData *last_vd;
	const SGS_ProgramOpGraph *graph;
} VoConvState;

SGS_GArr_DEF(OpRefArr, SGS_ProgramOpRef, )

typedef struct ProgConv {
	OpRefArr vo_oprefs; // operators for current voice traversal
	SGS_Program *program;
	size_t ev_count;
	size_t ev_val_count;
	size_t ev_op_data_count;
	uint16_t vo_count;
	uint32_t op_count;
	EventNode *events;
	EventValue *ev_values;
	EventOpData *ev_op_data;
	VoiceNode *voices;
	VoConvState *vo_ca;
	OperatorNode *operators;
	OpConvState *op_ca;
	size_t op_nest_depth;
} ProgConv;

static bool _ProgConv_init(ProgConv *o, SGS_Program *prg) {
	size_t i;
	*o = (ProgConv){0};
	o->program = prg;

	i = prg->ev_count;
	if (i > 0) {
		o->events = calloc(i, sizeof(EventNode));
		if (!o->events) return false;
		o->ev_count = i;
	}
	size_t ev_val_count = 0, ev_op_data_count = 0;
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SGS_ProgramEvent *ev = &prg->events[i];
		ev_val_count += count_ev_values(ev);
		ev_op_data_count += ev->op_data_count;
	}
	if (ev_val_count > 0) {
		o->ev_values = calloc(ev_val_count, sizeof(EventValue));
		if (!o->ev_values) return false;
		o->ev_val_count = ev_val_count;
	}
	if (ev_op_data_count > 0) {
		o->ev_op_data = calloc(ev_op_data_count, sizeof(EventOpData));
		if (!o->ev_op_data) return false;
		o->ev_op_data_count = ev_op_data_count;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = calloc(i, sizeof(VoiceNode));
		o->vo_ca = calloc(i, sizeof(VoConvState));
		if (!o->voices || !o->vo_ca) return false;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = calloc(i, sizeof(OperatorNode));
		o->op_ca = calloc(i, sizeof(OpConvState));
		if (!o->operators || !o->op_ca) return false;
		o->op_count = i;
	}
	o->op_nest_depth = prg->op_nest_depth; // TODO: move alloc

	return true;
}

static void _ProgConv_fini(ProgConv *o) {
	OpRefArr_clear(&o->vo_oprefs);
	if (o->events) free(o->events);
	if (o->ev_values) free(o->ev_values);
	if (o->ev_op_data) free(o->ev_op_data);
	if (o->voices) free(o->voices);
	if (o->vo_ca) free(o->vo_ca);
	if (o->operators) free(o->operators);
	if (o->op_ca) free(o->op_ca);
}

//static void ProgConv_handle_event(ProgConv *o, size_t ev_id,
//		uint32_t srate,
//		uint32_t *vo_wait_time, EventValue **cur_ev_values) {
//}

static SGS_Generator *_ProgConv_copy_out(ProgConv *o) {
	size_t buf_count = BUF_COUNT(o->op_nest_depth);
	Buf *bufs = NULL;
	if (buf_count > 0) {
		bufs = calloc(buf_count, sizeof(Buf));
		if (!bufs) return NULL;
	}

	SGS_Generator *gen = calloc(1, sizeof(SGS_Generator));
	if (!gen) return NULL;

	gen->events = o->events;
	gen->ev_count = o->ev_count;
	o->events = NULL;
	gen->ev_values = o->ev_values;
	o->ev_values = NULL;
	gen->ev_op_data = o->ev_op_data;
	o->ev_op_data = NULL;
	gen->voices = o->voices;
	gen->vo_count = o->vo_count;
	o->voices = NULL;
	gen->operators = o->operators;
	gen->op_count = o->op_count;
	o->operators = NULL;
	gen->bufs = bufs;
	gen->buf_count = buf_count;

	return gen;
}

#if 0
/*
 * Traverse voice operator graph built during allocation,
 * assigning block IDs.
 */
static void ProgConv_traverse_operators(ProgConv *o,
		SGS_ProgramOpRef *op_ref, uint32_t level) {
	OpConvState *ocs = &o->op_ca[op_ref->id];
	uint32_t i;
	if ((ocs->flags & ON_VISITED) != 0) {
		fprintf(stderr,
"skipping operator %d; does not support circular references\n",
			op_ref->id);
		return;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	op_ref->level = level++;
	if (ocs->adjcs) {
		SGS_ProgramOpRef mod_op_ref;
		const SGS_ProgramOpAdjcs *adjcs = ocs->adjcs;
		const uint32_t *mods = ocs->adjcs->adjcs;
		uint32_t modc = 0;
		ocs->flags |= ON_VISITED;
		i = 0;
		modc += adjcs->fmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_OP_FMOD;
//			fprintf(stderr, "visit fmod node %d\n", mod_op_ref.id);
			ProgConv_traverse_operators(o, &mod_op_ref, level);
		}
		modc += adjcs->pmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_OP_PMOD;
//			fprintf(stderr, "visit pmod node %d\n", mod_op_ref.id);
			ProgConv_traverse_operators(o, &mod_op_ref, level);
		}
		modc += adjcs->amodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_OP_AMOD;
//			fprintf(stderr, "visit amod node %d\n", mod_op_ref.id);
			ProgConv_traverse_operators(o, &mod_op_ref, level);
		}
		ocs->flags &= ~ON_VISITED;
	}
	OpRefArr_add(&o->vo_oprefs, op_ref);
}

/*
 * Traverse operator graph for voice built during allocation,
 * assigning an operator reference list to the voice and
 * block IDs to the operators.
 */
static void ProgConv_traverse_voice(ProgConv *o, EventVoData *ev_vd) {
	SGS_ProgramOpRef op_ref = {0, SGS_OP_CARR, 0};
	VoConvState *vcs = &o->vo_ca[ev_vd->id];
	const SGS_ProgramOpGraph *graph = vcs->graph;
	uint32_t i;
	if (!graph) return;
	o->vo_oprefs.count = 0;
	for (i = 0; i < graph->opc; ++i) {
		op_ref.id = graph->ops[i];
//		fprintf(stderr, "visit node %d\n", op_ref.id);
		ProgConv_traverse_operators(o, &op_ref, 0);
	}

	if (ev_vd->op_list) {
		free((SGS_ProgramOpRef*) ev_vd->op_list);
		fprintf(stderr, "DOUBLE OP_LIST\n");
	}
	if (o->vo_oprefs.count) {
		OpRefArr_dupa(&o->vo_oprefs, &ev_vd->op_list);
	}
	ev_vd->op_count = o->vo_oprefs.count;
}
#endif

static SGS_Generator *ProgConv_convert(ProgConv *o, SGS_Program *prg,
		uint32_t srate) {
	if (!_ProgConv_init(o, prg)) {
		_ProgConv_fini(o);
		return NULL;
	}

	EventValue *ev_v = o->ev_values;
	EventOpData *ev_od = o->ev_op_data;
	uint32_t vo_wait_time = 0;
	for (size_t i = 0; i < prg->ev_count; ++i) {
		SGS_Program *prg = o->program;
		const SGS_ProgramEvent *prg_e = &prg->events[i];
		EventNode *e = &o->events[i];
		uint32_t params;
		uint16_t vo_id = prg_e->vo_id;
//		bool need_traverse = false;
		e->vals = ev_v;
		e->waittime = MS_TO_SRT(prg_e->wait_ms, srate);
		vo_wait_time += e->waittime;
		//e->vd.id = SGS_VO_NO_ID;
		e->vd.id = vo_id;
		e->od = ev_od;
		e->od_count = prg_e->op_data_count;
		for (size_t j = 0; j < prg_e->op_data_count; ++j) {
			const SGS_ProgramOpData *pod = &prg_e->op_data[j];
			uint32_t op_id = pod->id;
			params = pod->params;
			ev_od->id = op_id;
			ev_od->params = params;
			if (params & SGS_OPP_ADJCS) {
				o->op_ca[op_id].adjcs = pod->adjcs;
//				need_traverse = true;
				ev_od->adjcs = pod->adjcs;
			}
			if (params & SGS_OPP_ATTR)
				(*ev_v++).i = pod->attr;
			if (params & SGS_OPP_WAVE)
				(*ev_v++).i = pod->wave;
			if (params & SGS_OPP_TIME) {
				(*ev_v++).i = (pod->time_ms == SGS_TIME_INF) ?
					SGS_TIME_INF :
					MS_TO_SRT(pod->time_ms, srate);
			}
			if (params & SGS_OPP_SILENCE)
				(*ev_v++).i = MS_TO_SRT(pod->silence_ms, srate);
			if (params & SGS_OPP_FREQ)
				(*ev_v++).f = pod->freq;
			if (params & SGS_OPP_VALITFREQ) {
				(*ev_v++).i = MS_TO_SRT(pod->valitfreq.time_ms, srate);
				(*ev_v++).f = pod->valitfreq.goal;
				(*ev_v++).i = pod->valitfreq.type;
			}
			if (params & SGS_OPP_DYNFREQ)
				(*ev_v++).f = pod->dynfreq;
			if (params & SGS_OPP_PHASE)
				(*ev_v++).i = SGS_Osc_PHASE(pod->phase);
			if (params & SGS_OPP_AMP)
				(*ev_v++).f = pod->amp;
			if (params & SGS_OPP_VALITAMP) {
				(*ev_v++).i = MS_TO_SRT(pod->valitamp.time_ms, srate);
				(*ev_v++).f = pod->valitamp.goal;
				(*ev_v++).i = pod->valitamp.type;
			}
			if (params & SGS_OPP_DYNAMP)
				(*ev_v++).f = pod->dynamp;
			o->op_ca[op_id].last_od = ev_od;
			++ev_od;
		}
		if (prg_e->vo_data) {
			const SGS_ProgramVoData *pvd = prg_e->vo_data;
			params = pvd->params;
			e->vd.params = params;
//			if (params & SGS_VOP_GRAPH) {
//				o->vo_ca[vo_id].graph = pvd->graph;
//				need_traverse = true;
//			}
			if (params & SGS_VOP_OPLIST) {
				e->vd.op_list = pvd->op_list;
				e->vd.op_count = pvd->op_count;
			}
			if (params & SGS_VOP_ATTR)
				(*ev_v++).i = pvd->attr;
			if (params & SGS_VOP_PANNING)
				(*ev_v++).f = pvd->panning;
			if (params & SGS_VOP_VALITPANNING) {
				(*ev_v++).i = MS_TO_SRT(pvd->valitpanning.time_ms, srate);
				(*ev_v++).f = pvd->valitpanning.goal;
				(*ev_v++).i = pvd->valitpanning.type;
			}
			o->voices[vo_id].pos = -vo_wait_time;
			o->vo_ca[vo_id].last_vd = &e->vd;
			vo_wait_time = 0;
		}
//		if (need_traverse) {
//			ProgConv_traverse_voice(o, &e->vd);
//		}
	}

	SGS_Generator *gen = _ProgConv_copy_out(o);
	_ProgConv_fini(o);
	return gen;
}

/*
 * Generator
 */

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(SGS_Program *prg, uint32_t srate) {
	ProgConv pc;
	SGS_Generator *o = ProgConv_convert(&pc, prg, srate);
	if (!o) return NULL;

	o->osc_coeff = SGS_Osc_SRATE_COEFF(srate);
	o->srate = srate;
	o->amp_scale = 1.f;
	if ((prg->flags & SGS_PROG_AMP_DIV_VOICES) != 0)
		o->amp_scale /= o->vo_count;
	SGS_global_init_Wave();
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Generator(SGS_Generator *o) {
	if (o->bufs) free(o->bufs);
	if (o->events) {
//		for (size_t i = 0; i < o->ev_count; ++i) {
//			EventNode *e = &o->events[i];
//			if (e->vd.op_list) free((void*) e->vd.op_list);
//		}
		free(o->events);
	}
	if (o->voices) free(o->voices);
	if (o->operators) free(o->operators);
	if (o->ev_values) free(o->ev_values);
	if (o->ev_op_data) free(o->ev_op_data);
	free(o);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SGS_Generator *o, VoiceNode *vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->op_count; ++i) {
		const SGS_ProgramOpRef *or = &vn->op_list[i];
		if (or->use != SGS_OP_CARR) continue;
		OperatorNode *on = &o->operators[or->id];
		if (on->time == SGS_TIME_INF) continue;
		if (on->time > time)
			time = on->time;
	}
	vn->duration = time;
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *o, EventNode *e) {
  if (1) /* more types to be added in the future */ {
    const EventValue *val = e->vals;
    uint32_t params;
    /*
     * Set state of operator and/or voice. Voice updates must be done last,
     * as recalculations may be made according to the state of the operators.
     */
    for (size_t i = 0; i < e->od_count; ++i) {
      EventOpData *od = &e->od[i];
      OperatorNode *on = &o->operators[od->id];
      params = od->params;
      if (params & SGS_OPP_ADJCS)
        on->adjcs = od->adjcs;
      if (params & SGS_OPP_ATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        if (!(params & SGS_OPP_FREQ)) {
          /* May change during processing; preserve state of FREQRATIO flag */
          attr &= ~SGS_OPAT_FREQRATIO;
          attr |= on->attr & SGS_OPAT_FREQRATIO;
        }
        on->attr = attr;
      }
      if (params & SGS_OPP_WAVE)
        on->wave = (*val++).i;
      if (params & SGS_OPP_TIME)
        on->time = (*val++).i;
      if (params & SGS_OPP_SILENCE)
        on->silence = (*val++).i;
      if (params & SGS_OPP_FREQ)
        on->freq = (*val++).f;
      if (params & SGS_OPP_VALITFREQ) {
        on->valitfreq.time = (*val++).i;
        on->valitfreq.pos = 0;
        on->valitfreq.goal = (*val++).f;
        on->valitfreq.type = (*val++).i;
      }
      if (params & SGS_OPP_DYNFREQ)
        on->dynfreq = (*val++).f;
      if (params & SGS_OPP_PHASE)
        SGS_Osc_SET_PHASE(&on->osc, (uint32_t)(*val++).i);
      if (params & SGS_OPP_AMP)
        on->amp = (*val++).f;
      if (params & SGS_OPP_VALITAMP) {
        on->valitamp.time = (*val++).i;
        on->valitamp.pos = 0;
        on->valitamp.goal = (*val++).f;
        on->valitamp.type = (*val++).i;
      }
      if (params & SGS_OPP_DYNAMP)
        on->dynamp = (*val++).f;
    }
    if (e->vd.id != SGS_VO_NO_ID) {
      VoiceNode *vn = &o->voices[e->vd.id];
      params = e->vd.params;
      if (params & SGS_VOP_OPLIST) {
        vn->op_list = e->vd.op_list;
        vn->op_count = e->vd.op_count;
      }
      if (params & SGS_VOP_ATTR) {
        uint8_t attr = (uint8_t)(*val++).i;
        vn->attr = attr;
      }
      if (params & SGS_VOP_PANNING)
        vn->panning = (*val++).f;
      if (params & SGS_VOP_VALITPANNING) {
        vn->valitpanning.time = (*val++).i;
        vn->valitpanning.pos = 0;
        vn->valitpanning.goal = (*val++).f;
        vn->valitpanning.type = (*val++).i;
      }
      vn->flags |= VN_INIT;
      vn->pos = 0;
      if (o->voice > e->vd.id) /* go back to re-activated node */
        o->voice = e->vd.id;
      set_voice_duration(o, vn);
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
  if (n->time < len && n->time != SGS_TIME_INF) {
    skip_len = len - n->time;
    len = n->time;
  }
  /*
   * Handle frequency (alternatively ratio) parameter, including frequency
   * modulation if modulators linked.
   */
  freq = *(nextbuf++);
  if (n->attr & SGS_OPAT_VALITFREQ) {
    vi = &n->valitfreq;
    if (n->attr & SGS_OPAT_VALITFREQRATIO) {
      freqmod = parent_freq;
      if (!(n->attr & SGS_OPAT_FREQRATIO)) {
        n->attr |= SGS_OPAT_FREQRATIO;
        n->freq /= parent_freq[0].f;
      }
    } else {
      freqmod = 0;
      if (n->attr & SGS_OPAT_FREQRATIO) {
        n->attr &= ~SGS_OPAT_FREQRATIO;
        n->freq *= parent_freq[0].f;
      }
    }
  } else {
    vi = 0;
    freqmod = (n->attr & SGS_OPAT_FREQRATIO) ? parent_freq : 0;
  }
  if (run_param(freq, len, vi, &n->freq, freqmod))
    n->attr &= ~(SGS_OPAT_VALITFREQ|SGS_OPAT_VALITFREQRATIO);
  if (fmodc) {
    const uint32_t *fmods = n->adjcs->adjcs;
    BufValue *fmbuf;
    for (i = 0; i < fmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, true, i);
    fmbuf = *nextbuf;
    if (n->attr & SGS_OPAT_FREQRATIO) {
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
    const uint32_t *pmods = &n->adjcs->adjcs[fmodc];
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
      const uint32_t *amods = &n->adjcs->adjcs[fmodc+pmodc];
      float dynampdiff = n->dynamp - n->amp;
      for (i = 0; i < amodc; ++i)
        run_block(o, nextbuf, len, &o->operators[amods[i]], freq, true, i);
      amp = *(nextbuf++);
      for (i = 0; i < len; ++i)
        amp[i].f = n->amp + amp[i].f * dynampdiff;
    } else {
      amp = *(nextbuf++);
      vi = (n->attr & SGS_OPAT_VALITAMP) ? &n->valitamp : 0;
      if (run_param(amp, len, vi, &n->amp, 0))
        n->attr &= ~SGS_OPAT_VALITAMP;
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
	if (vn->attr & SGS_VOAT_VALITPANNING) {
		BufValue *pan_buf = o->bufs[1];
		if (run_param(pan_buf, len, &vn->valitpanning,
				&vn->panning, 0)) {
			vn->attr &= ~SGS_VOAT_VALITPANNING;
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
  const SGS_ProgramOpRef *ops = vn->op_list;
  uint32_t opc = vn->op_count;
  if (!ops) goto RETURN;
  uint32_t time;
  uint32_t i;
  time = vn->duration;
  if (time > buf_len) time = buf_len;
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
      if (ops[i].use != SGS_OP_CARR) continue; // TODO: finish redesign
      OperatorNode *n = &o->operators[ops[i].id];
      if (n->time == 0) continue;
      last_len = run_block(o, o->bufs, len, n, 0, false, acc_ind++);
      if (last_len > gen_len) gen_len = last_len;
    }
    if (!gen_len) goto RETURN;
    mix_output(o, vn, &sp, gen_len);
    out_len += gen_len;
    vn->duration = (vn->duration > gen_len) ?
      vn->duration - gen_len :
      0;
  }
RETURN:
  vn->pos += out_len;
  return out_len;
}

/*
 * Any error checking following sound generation goes here.
 */
static void check_final_state(SGS_Generator *o) {
	for (uint16_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (!(vn->flags & VN_INIT)) {
			fprintf(stderr,
"warning (generator): voice %hd left uninitialized\n", i);
		}
	}
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
  while (o->event < o->ev_count) {
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
  for (i = o->voice; i < o->vo_count; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint32_t waittime = (uint32_t) -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->duration != 0) {
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
    if (o->voice == o->vo_count) {
      if (o->event != o->ev_count) break;
      /*
       * The end.
       */
      if (out_len) *out_len = gen_len;
      check_final_state(o);
      return false;
    }
    vn = &o->voices[o->voice];
    if (vn->duration != 0) break;
    ++o->voice;
  }
  /*
   * Further calls needed to complete signal.
   */
  if (out_len) *out_len = buf_len;
  return true;
}
