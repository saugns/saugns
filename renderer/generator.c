/* sgensys: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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
#include "../mempool.h"
#include <stdio.h>
#include <stdlib.h>

#define BUF_LEN SGS_MIX_BUFLEN
typedef float Buf[BUF_LEN];

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
	ON_TIME_INF = 1<<1, /* used for SGS_TIMEP_IMPLICIT */
};

typedef struct OperatorNode {
	SGS_Osc osc;
	uint32_t time;
	uint8_t flags;
	const SGS_ProgramOpList *fmods, *pmods, *amods;
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
	const SGS_ProgramOpRef *graph;
	uint32_t op_count;
	SGS_Ramp pan;
	uint32_t pan_pos;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	uint16_t vo_id;
	const SGS_ProgramOpRef *graph;
	const SGS_ProgramOpData *op_data;
	const SGS_ProgramVoData *vo_data;
	uint32_t op_count;
	uint32_t op_data_count;
} EventNode;

struct SGS_Generator {
	uint32_t srate;
	uint32_t gen_buf_count;
	Buf *gen_bufs;
	SGS_Mixer *mixer;
	size_t event, ev_count;
	EventNode **events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	uint32_t op_count;
	OperatorNode *operators;
	SGS_MemPool *mem;
};

// maximum number of buffers needed for op nesting depth
#define COUNT_GEN_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 7)

static bool alloc_for_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg) {
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = SGS_MemPool_alloc(o->mem, i * sizeof(EventNode*));
		if (!o->events) goto ERROR;
		o->ev_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = SGS_MemPool_alloc(o->mem, i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = SGS_MemPool_alloc(o->mem,
				i * sizeof(OperatorNode));
		if (!o->operators) goto ERROR;
		o->op_count = i;
	}
	i = COUNT_GEN_BUFS(prg->op_nest_depth);
	if (i > 0) {
		o->gen_bufs = calloc(i, sizeof(Buf));
		if (!o->gen_bufs) goto ERROR;
		o->gen_buf_count = i;
	}
	o->mixer = SGS_create_Mixer();
	if (!o->mixer) goto ERROR;

	return true;
ERROR:
	return false;
}

static const SGS_ProgramOpList blank_oplist = {0};

static bool convert_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;

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
		on->fmods = on->pmods = on->amods = &blank_oplist;
	}
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SGS_ProgramEvent *prg_e = prg->events[i];
		EventNode *e = SGS_MemPool_alloc(o->mem, sizeof(EventNode));
		if (!e)
			return false;
		uint32_t params;
		uint16_t vo_id = prg_e->vo_id;
		e->wait = SGS_ms_in_samples(prg_e->wait_ms, srate);
		vo_wait_time += e->wait;
		//e->vo_id = SGS_PVO_NO_ID;
		e->vo_id = vo_id;
		e->op_data = prg_e->op_data;
		e->op_data_count = prg_e->op_data_count;
		if (prg_e->vo_data) {
			const SGS_ProgramVoData *pvd = prg_e->vo_data;
			params = pvd->params;
			if (params & SGS_PVOP_GRAPH) {
				e->graph = pvd->graph;
				e->op_count = pvd->op_count;
			}
			o->voices[vo_id].pos = -vo_wait_time;
			vo_wait_time = 0;
			e->vo_data = pvd;
		}
		o->events[i] = e;
	}

	return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(const SGS_Program *restrict prg,
		uint32_t srate) {
	SGS_MemPool *mem = SGS_create_MemPool(0);
	if (!mem)
		return NULL;
	SGS_Generator *o = SGS_MemPool_alloc(mem, sizeof(SGS_Generator));
	if (!o) {
		SGS_destroy_MemPool(mem);
		return NULL;
	}
	o->mem = mem;
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
	free(o->gen_bufs);
	SGS_destroy_Mixer(o->mixer);
	SGS_destroy_MemPool(o->mem);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SGS_Generator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->op_count; ++i) {
		const SGS_ProgramOpRef *or = &vn->graph[i];
		if (or->use != SGS_POP_CARR) continue;
		OperatorNode *on = &o->operators[or->id];
		if (on->time > time)
			time = on->time;
	}
	vn->duration = time;
}

/*
 * Process an event update for a timed parameter.
 */
static void handle_ramp_update(SGS_Ramp *restrict ramp,
		uint32_t *restrict ramp_pos,
		const SGS_Ramp *restrict ramp_src) {
	if ((ramp_src->flags & SGS_RAMPP_GOAL) != 0) {
		*ramp_pos = 0;
	}
	SGS_Ramp_copy(ramp, ramp_src);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		VoiceNode *vn = NULL;
		if (e->vo_id != SGS_PVO_NO_ID)
			vn = &o->voices[e->vo_id];
		for (size_t i = 0; i < e->op_data_count; ++i) {
			const SGS_ProgramOpData *od = &e->op_data[i];
			OperatorNode *on = &o->operators[od->id];
			uint32_t params = od->params;
			if (od->fmods != NULL) on->fmods = od->fmods;
			if (od->pmods != NULL) on->pmods = od->pmods;
			if (od->amods != NULL) on->amods = od->amods;
			if (params & SGS_POPP_WAVE)
				SGS_Osc_set_wave(&on->osc, od->wave);
			if (params & SGS_POPP_TIME) {
				const SGS_Time *src = &od->time;
				if (src->flags & SGS_TIMEP_IMPLICIT) {
					on->time = 0;
					on->flags |= ON_TIME_INF;
				} else {
					on->time = SGS_ms_in_samples(src->v_ms,
							o->srate);
					on->flags &= ~ON_TIME_INF;
				}
			}
			if (params & SGS_POPP_FREQ)
				handle_ramp_update(&on->freq,
						&on->freq_pos, &od->freq);
			if (params & SGS_POPP_FREQ2)
				handle_ramp_update(&on->freq2,
						&on->freq2_pos, &od->freq2);
			if (params & SGS_POPP_PHASE)
				SGS_Osc_set_phase(&on->osc,
						SGS_Osc_PHASE(od->phase));
			if (params & SGS_POPP_AMP)
				handle_ramp_update(&on->amp,
						&on->amp_pos, &od->amp);
			if (params & SGS_POPP_AMP2)
				handle_ramp_update(&on->amp2,
						&on->amp2_pos, &od->amp2);
			if (params & SGS_POPP_PAN)
				handle_ramp_update(&vn->pan,
						&vn->pan_pos, &od->pan);
		}
		if (vn != NULL) {
			if (e->graph != NULL) {
				vn->graph = e->graph;
				vn->op_count = e->op_count;
			}
			vn->flags |= VN_INIT;
			vn->pos = 0;
			if (o->voice > e->vo_id) {
				/* go back to re-activated node */
				o->voice = e->vo_id;
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
	len = buf_len;
	/*
	 * Guard against circular references.
	 */
	if ((n->flags & ON_VISITED) != 0) {
		for (i = 0; i < len; ++i)
			s_buf[i] = 0;
		return len;
	}
	n->flags |= ON_VISITED;
	/*
	 * Limit length to time duration of operator.
	 */
	uint32_t skip_len = 0;
	if (n->time < len && !(n->flags & ON_TIME_INF)) {
		skip_len = len - n->time;
		len = n->time;
	}
	/*
	 * Handle frequency, including frequency modulation
	 * if modulators linked.
	 */
	freq = *(bufs++);
	SGS_Ramp_run(&n->freq, &n->freq_pos, freq, len, o->srate, parent_freq);
	if (n->fmods->count > 0) {
		float *freq2 = *(bufs++);
		SGS_Ramp_run(&n->freq2, &n->freq2_pos,
				freq2, len, o->srate, parent_freq);
		for (i = 0; i < n->fmods->count; ++i)
			run_block(o, bufs, len, &o->operators[n->fmods->ids[i]],
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
	if (n->pmods->count > 0) {
		for (i = 0; i < n->pmods->count; ++i)
			run_block(o, bufs, len, &o->operators[n->pmods->ids[i]],
					freq, false, i);
		pm_buf = *(bufs++);
	}
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	amp = *(bufs++);
	SGS_Ramp_run(&n->amp, &n->amp_pos, amp, len, o->srate, NULL);
	if (n->amods->count > 0) {
		float *amp2 = *(bufs++);
		SGS_Ramp_run(&n->amp2, &n->amp2_pos, amp2, len, o->srate, NULL);
		for (i = 0; i < n->amods->count; ++i)
			run_block(o, bufs, len, &o->operators[n->amods->ids[i]],
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
	if (!(n->flags & ON_TIME_INF)) {
		if (!acc_ind && skip_len > 0) {
			s_buf += len;
			for (i = 0; i < skip_len; ++i)
				s_buf[i] = 0;
		}
		n->time -= len;
	}
	n->flags &= ~ON_VISITED;
	return len;
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
	const SGS_ProgramOpRef *ops = vn->graph;
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
		last_len = run_block(o, o->gen_bufs, time, n,
				NULL, false, acc_ind++);
		if (last_len > out_len) out_len = last_len;
	}
	if (out_len > 0) {
		SGS_Mixer_add(o->mixer, o->gen_bufs[0], out_len,
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
		EventNode *e = o->events[o->event];
		if (o->event_pos < e->wait) {
			/*
			 * Limit voice running len to waittime.
			 *
			 * Split processing into two blocks when needed to
			 * ensure event handling runs before voices.
			 */
			uint32_t waittime = e->wait - o->event_pos;
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
