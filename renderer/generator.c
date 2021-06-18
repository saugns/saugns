/* sgensys: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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
#include "mixer.h"
#include "osc.h"
#include <stdio.h>
#include <stdlib.h>

#define BUF_LEN SGS_MIX_BUFLEN
typedef float Buf[BUF_LEN];

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
	const SGS_ProgramOpAdjcs *adjcs;
	SGS_Ramp amp, freq;
	SGS_Ramp amp2, freq2;
	uint32_t amp_pos, freq_pos;
	uint32_t amp2_pos, freq2_pos;
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
	const SGS_ProgramOpRef *op_list;
	uint32_t op_count;
	SGS_Ramp pan;
	uint32_t pan_pos;
} VoiceNode;

typedef union EventValue {
	int32_t ival;
	//float fval;
	const SGS_Ramp *ramp;
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
	uint32_t srate;
	uint32_t buf_count;
	Buf *bufs;
	SGS_Mixer *mixer;
	size_t event, ev_count;
	EventNode *events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	uint32_t op_count;
	OperatorNode *operators;
	EventValue *ev_values;
	EventOpData *ev_op_data;
	size_t ev_val_count;
	size_t ev_op_data_count;
};

static uint32_t count_flags(uint32_t flags) {
	uint32_t i, count = 0;
	for (i = 0; i < (8 * sizeof(uint32_t)); ++i) {
		count += flags & 1;
		flags >>= 1;
	}
	return count;
}

static size_t count_ev_values(const SGS_ProgramEvent *restrict e) {
	size_t count = 0;
	uint32_t params;
	if (e->vo_data) {
		params = e->vo_data->params;
		params &= ~(SGS_PVOP_OPLIST);
		count += count_flags(params);
	}
	for (size_t i = 0; i < e->op_data_count; ++i) {
		params = e->op_data[i].params;
		params &= ~(SGS_POPP_ADJCS);
		count += count_flags(params);
	}
	return count;
}

// maximum number of buffers needed for op nesting depth
#define COUNT_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 7)

static bool alloc_for_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg) {
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = calloc(i, sizeof(EventNode));
		if (!o->events)
			return false;
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
		if (!o->ev_values)
			return false;
		o->ev_val_count = ev_val_count;
	}
	if (ev_op_data_count > 0) {
		o->ev_op_data = calloc(ev_op_data_count, sizeof(EventOpData));
		if (!o->ev_op_data)
			return false;
		o->ev_op_data_count = ev_op_data_count;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = calloc(i, sizeof(VoiceNode));
		if (!o->voices)
			return false;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = calloc(i, sizeof(OperatorNode));
		if (!o->operators)
			return false;
		o->op_count = i;
	}
	i = COUNT_BUFS(prg->op_nest_depth);
	if (i > 0) {
		o->bufs = calloc(i, sizeof(Buf));
		if (!o->bufs)
			return false;
		o->buf_count = i;
	}
	o->mixer = SGS_create_Mixer();
	if (!o->mixer)
		return false;

	return true;
}

static bool convert_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;

	EventValue *ev_v = o->ev_values;
	EventOpData *ev_od = o->ev_op_data;
	uint32_t vo_wait_time = 0;

	o->srate = srate;
	float scale = 1.f;
	if ((prg->mode & SGS_PMODE_AMP_DIV_VOICES) != 0)
		scale /= o->vo_count;
	SGS_Mixer_set_srate(o->mixer, srate);
	SGS_Mixer_set_scale(o->mixer, scale);
	for (size_t i = 0; i < prg->op_count; ++i) {
		OperatorNode *on = &o->operators[i];
		SGS_init_Osc(&on->osc, srate);
	}
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SGS_ProgramEvent *prg_e = &prg->events[i];
		EventNode *e = &o->events[i];
		uint32_t params;
		uint16_t vo_id = prg_e->vo_id;
		e->vals = ev_v;
		e->waittime = SGS_MS_IN_SAMPLES(prg_e->wait_ms, srate);
		vo_wait_time += e->waittime;
		//e->vd.id = SGS_PVO_NO_ID;
		e->vd.id = vo_id;
		e->od = ev_od;
		e->od_count = prg_e->op_data_count;
		for (size_t j = 0; j < prg_e->op_data_count; ++j) {
			const SGS_ProgramOpData *pod = &prg_e->op_data[j];
			uint32_t op_id = pod->id;
			params = pod->params;
			ev_od->id = op_id;
			ev_od->params = params;
			if (params & SGS_POPP_ADJCS) {
				ev_od->adjcs = pod->adjcs;
			}
			if (params & SGS_POPP_WAVE)
				(*ev_v++).ival = pod->wave;
			if (params & SGS_POPP_TIME) {
				(*ev_v++).ival =
					(pod->time_ms == SGS_TIME_INF) ?
					SGS_TIME_INF :
					SGS_MS_IN_SAMPLES(pod->time_ms, srate);
			}
			if (params & SGS_POPP_SILENCE)
				(*ev_v++).ival =
					SGS_MS_IN_SAMPLES(pod->silence_ms,
							srate);
			if (params & SGS_POPP_FREQ)
				(*ev_v++).ramp = &pod->freq;
			if (params & SGS_POPP_FREQ2)
				(*ev_v++).ramp = &pod->freq2;
			if (params & SGS_POPP_PHASE)
				(*ev_v++).ival = SGS_Osc_PHASE(pod->phase);
			if (params & SGS_POPP_AMP)
				(*ev_v++).ramp = &pod->amp;
			if (params & SGS_POPP_AMP2)
				(*ev_v++).ramp = &pod->amp2;
			++ev_od;
		}
		if (prg_e->vo_data) {
			const SGS_ProgramVoData *pvd = prg_e->vo_data;
			params = pvd->params;
			e->vd.params = params;
			if (params & SGS_PVOP_OPLIST) {
				e->vd.op_list = pvd->op_list;
				e->vd.op_count = pvd->op_count;
			}
			if (params & SGS_PVOP_PAN)
				(*ev_v++).ramp = &pvd->pan;
			o->voices[vo_id].pos = -vo_wait_time;
			vo_wait_time = 0;
		}
	}

	return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(const SGS_Program *restrict prg,
		uint32_t srate) {
	SGS_Generator *o = calloc(1, sizeof(SGS_Generator));
	if (!o)
		return NULL;
	if (!convert_program(o, prg, srate)) {
		SGS_destroy_Generator(o);
		return NULL;
	}
	SGS_global_init_Wave();
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Generator(SGS_Generator *restrict o) {
	if (!o)
		return;
	SGS_destroy_Mixer(o->mixer);
	free(o->bufs);
	free(o->events);
	free(o->voices);
	free(o->operators);
	free(o->ev_values);
	free(o->ev_op_data);
	free(o);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SGS_Generator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->op_count; ++i) {
		const SGS_ProgramOpRef *or = &vn->op_list[i];
		if (or->use != SGS_POP_CARR) continue;
		OperatorNode *on = &o->operators[or->id];
		if (on->time == SGS_TIME_INF) continue;
		if (on->time > time)
			time = on->time;
	}
	vn->duration = time;
}

/*
 * Process an event update for a timed parameter.
 */
static const EventValue *handle_ramp_update(SGS_Ramp *restrict ramp,
		uint32_t *restrict ramp_pos,
		const EventValue *restrict val) {
	const SGS_Ramp *src = (*val++).ramp;
	if ((src->flags & SGS_RAMPP_GOAL) != 0) {
		*ramp_pos = 0;
	}
	SGS_Ramp_copy(ramp, src);
	return val;
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		const EventValue *val = e->vals;
		uint32_t params;
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		for (size_t i = 0; i < e->od_count; ++i) {
			EventOpData *od = &e->od[i];
			OperatorNode *on = &o->operators[od->id];
			params = od->params;
			if (params & SGS_POPP_ADJCS)
				on->adjcs = od->adjcs;
			if (params & SGS_POPP_WAVE) {
				uint8_t wave = (*val++).ival;
				on->osc.lut = SGS_Osc_LUT(wave);
			}
			if (params & SGS_POPP_TIME)
				on->time = (*val++).ival;
			if (params & SGS_POPP_SILENCE)
				on->silence = (*val++).ival;
			if (params & SGS_POPP_FREQ)
				val = handle_ramp_update(&on->freq,
						&on->freq_pos, val);
			if (params & SGS_POPP_FREQ2)
				val = handle_ramp_update(&on->freq2,
						&on->freq2_pos, val);
			if (params & SGS_POPP_PHASE)
				on->osc.phase = (*val++).ival;
			if (params & SGS_POPP_AMP)
				val = handle_ramp_update(&on->amp,
						&on->amp_pos, val);
			if (params & SGS_POPP_AMP2)
				val = handle_ramp_update(&on->amp2,
						&on->amp2_pos, val);
		}
		if (e->vd.id != SGS_PVO_NO_ID) {
			VoiceNode *vn = &o->voices[e->vd.id];
			params = e->vd.params;
			if (params & SGS_PVOP_OPLIST) {
				vn->op_list = e->vd.op_list;
				vn->op_count = e->vd.op_count;
			}
			if (params & SGS_PVOP_PAN)
				val = handle_ramp_update(&vn->pan,
						&vn->pan_pos, val);
			vn->flags |= VN_INIT;
			vn->pos = 0;
			if (o->voice > e->vd.id) {
				/* go back to re-activated node */
				o->voice = e->vd.id;
			}
			set_voice_duration(o, vn);
		}
	}
}

/*
 * Generate up to buf_len samples for an operator node,
 * the remainder (if any) zero-filled if acc_ind is zero.
 *
 * Recursively visits the subnodes of the operator node,
 * if any.
 *
 * Returns number of samples generated for the node.
 */
static uint32_t run_block(SGS_Generator *restrict o,
		Buf *restrict bufs, uint32_t buf_len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, uint32_t acc_ind) {
	uint32_t i, len;
	float *s_buf = *(bufs++), *pm_buf;
	float *freq, *amp;
	uint32_t fmodc = 0, pmodc = 0, amodc = 0;
	if (n->adjcs) {
		fmodc = n->adjcs->fmodc;
		pmodc = n->adjcs->pmodc;
		amodc = n->adjcs->amodc;
	}
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
			s_buf[i] = 0;
		len -= zero_len;
		if (n->time != SGS_TIME_INF) n->time -= zero_len;
		n->silence -= zero_len;
		if (!len)
			return zero_len;
		s_buf += zero_len;
	}
	/*
	 * Guard against circular references.
	 */
	if ((n->flags & ON_VISITED) != 0) {
		for (i = 0; i < len; ++i)
			s_buf[i] = 0;
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
	 * Handle frequency, including frequency modulation
	 * if modulators linked.
	 */
	freq = *(bufs++);
	SGS_Ramp_run(&n->freq, &n->freq_pos, freq, len, o->srate, parent_freq);
	if (fmodc) {
		float *freq2 = *(bufs++);
		SGS_Ramp_run(&n->freq2, &n->freq2_pos,
				freq2, len, o->srate, parent_freq);
		const uint32_t *fmods = n->adjcs->adjcs;
		for (i = 0; i < fmodc; ++i)
			run_block(o, bufs, len, &o->operators[fmods[i]],
					freq, true, i);
		float *fm_buf = *bufs;
		for (i = 0; i < len; ++i)
			freq[i] += (freq2[i] - freq[i]) * fm_buf[i];
	} else {
		SGS_Ramp_skip(&n->freq2, &n->freq2_pos, len, o->srate);
	}
	/*
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	pm_buf = NULL;
	if (pmodc) {
		const uint32_t *pmods = &n->adjcs->adjcs[fmodc];
		for (i = 0; i < pmodc; ++i)
			run_block(o, bufs, len, &o->operators[pmods[i]],
					freq, false, i);
		pm_buf = *(bufs++);
	}
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	amp = *(bufs++);
	SGS_Ramp_run(&n->amp, &n->amp_pos, amp, len, o->srate, NULL);
	if (amodc) {
		float *amp2 = *(bufs++);
		SGS_Ramp_run(&n->amp2, &n->amp2_pos, amp2, len, o->srate, NULL);
		const uint32_t *amods = &n->adjcs->adjcs[fmodc+pmodc];
		for (i = 0; i < amodc; ++i)
			run_block(o, bufs, len, &o->operators[amods[i]],
					freq, true, i);
		float *am_buf = *bufs;
		for (i = 0; i < len; ++i)
			amp[i] += (amp2[i] - amp[i]) * am_buf[i];
	} else {
		SGS_Ramp_skip(&n->amp2, &n->amp2_pos, len, o->srate);
	}
	if (!wave_env) {
		SGS_Osc_run(&n->osc, s_buf, len, acc_ind, freq, amp, pm_buf);
	} else {
		SGS_Osc_run_env(&n->osc, s_buf, len, acc_ind, freq, amp, pm_buf);
	}
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (n->time != SGS_TIME_INF) {
		if (!acc_ind && skip_len > 0) {
			s_buf += len;
			for (i = 0; i < skip_len; ++i)
				s_buf[i] = 0;
		}
		n->time -= len;
	}
	n->flags &= ~ON_VISITED;
	return zero_len + len;
}

/*
 * Generate up to BUF_LEN samples for a voice, mixed into the
 * mix buffers.
 *
 * \return number of samples generated
 */
static uint32_t run_voice(SGS_Generator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	uint32_t out_len = 0;
	const SGS_ProgramOpRef *ops = vn->op_list;
	uint32_t opc = vn->op_count;
	if (!ops)
		return 0;
	uint32_t acc_ind = 0;
	uint32_t time;
	uint32_t i;
	time = vn->duration;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	for (i = 0; i < opc; ++i) {
		uint32_t last_len;
		// TODO: finish redesign
		if (ops[i].use != SGS_POP_CARR) continue;
		OperatorNode *n = &o->operators[ops[i].id];
		if (n->time == 0) continue;
		last_len = run_block(o, o->bufs, time, n,
				NULL, false, acc_ind++);
		if (last_len > out_len) out_len = last_len;
	}
	if (out_len > 0) {
		SGS_Mixer_add(o->mixer, o->bufs[0], out_len,
				&vn->pan, &vn->pan_pos);
	}
	vn->duration -= time;
	vn->pos += time;
	return out_len;
}

/*
 * Run voices for \p time, repeatedly generating up to BUF_LEN samples
 * and writing them into the 16-bit stereo (interleaved) buffer \p buf.
 *
 * \return number of samples generated
 */
static uint32_t run_for_time(SGS_Generator *restrict o,
		uint32_t time, int16_t *restrict buf) {
	int16_t *sp = buf;
	uint32_t gen_len = 0;
	while (time > 0) {
		uint32_t len = time;
		if (len > BUF_LEN) len = BUF_LEN;
		SGS_Mixer_clear(o->mixer);
		uint32_t last_len = 0;
		for (uint32_t i = o->voice; i < o->vo_count; ++i) {
			VoiceNode *vn = &o->voices[i];
			if (vn->pos < 0) {
				/*
				 * Wait times accumulate across nodes.
				 *
				 * Reduce length by wait time and
				 * end if wait time(s) have swallowed it up.
				 */
				uint32_t wait_time = (uint32_t) -vn->pos;
				if (wait_time >= len) {
					vn->pos += len;
					break;
				}
				sp += wait_time+wait_time; /* stereo double */
				len -= wait_time;
				gen_len += wait_time;
				vn->pos = 0;
			}
			if (vn->duration != 0) {
				uint32_t voice_len = run_voice(o, vn, len);
				if (voice_len > last_len) last_len = voice_len;
			}
		}
		time -= len;
		if (last_len > 0) {
			gen_len += last_len;
			SGS_Mixer_write(o->mixer, &sp, last_len);
		}
	}
	return gen_len;
}

/*
 * Any error checking following audio generation goes here.
 */
static void check_final_state(SGS_Generator *restrict o) {
	for (uint16_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (!(vn->flags & VN_INIT)) {
			SGS_warning("generator",
"voice %hd left uninitialized (never used)", i);
		}
	}
}

/**
 * Main audio generation/processing function. Call repeatedly to write
 * buf_len new samples into the interleaved stereo buffer buf. Any values
 * after the end of the signal will be zero'd.
 *
 * If supplied, out_len will be set to the precise length generated
 * for this call, which is buf_len unless the signal ended earlier.
 *
 * \return true unless the signal has ended
 */
bool SGS_Generator_run(SGS_Generator *restrict o,
		int16_t *restrict buf, size_t buf_len,
		size_t *restrict out_len) {
	int16_t *sp = buf;
	uint32_t i, len = buf_len;
	for (i = len; i--; sp += 2) {
		sp[0] = 0;
		sp[1] = 0;
	}
	sp = buf;
	uint32_t skip_len, last_len, gen_len = 0;
PROCESS:
	skip_len = 0;
	while (o->event < o->ev_count) {
		EventNode *e = &o->events[o->event];
		if (o->event_pos < e->waittime) {
			/*
			 * Limit voice running len to waittime.
			 *
			 * Split processing into two blocks when needed to
			 * ensure event handling runs before voices.
			 */
			uint32_t waittime = e->waittime - o->event_pos;
			if (waittime < len) {
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
	last_len = run_for_time(o, len, sp);
	if (skip_len > 0) {
		gen_len += len;
		sp += len+len; /* stereo double */
		len = skip_len;
		goto PROCESS;
	} else {
		gen_len += last_len;
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
