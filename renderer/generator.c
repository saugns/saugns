/* saugns: Audio generator module.
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

#define BUF_LEN SAU_MIX_BUFLEN
typedef float Buf[BUF_LEN];

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
	ON_TIME_INF = 1<<1, /* used for SAU_TIMEP_IMPLICIT */
};

typedef struct OperatorNode {
	SAU_Osc osc;
	uint32_t time;
	uint8_t flags;
	const SAU_ProgramOpList *amods, *fmods, *pmods, *fpmods;
	SAU_Ramp amp, freq;
	SAU_Ramp amp2, freq2;
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
	const SAU_ProgramOpRef *graph;
	uint32_t op_count;
	SAU_Ramp pan;
	uint32_t pan_pos;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	uint16_t vo_id;
	const SAU_ProgramOpRef *graph;
	const SAU_ProgramOpData **op_data;
	const SAU_ProgramVoData *vo_data;
	uint32_t op_count;
	uint32_t op_data_count;
} EventNode;

struct SAU_Generator {
	uint32_t srate;
	uint32_t gen_buf_count;
	Buf *gen_bufs;
	SAU_Mixer *mixer;
	size_t event, ev_count;
	EventNode **events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	uint32_t op_count;
	OperatorNode *operators;
	SAU_MemPool *mem;
};

// maximum number of buffers needed for op nesting depth
#define COUNT_GEN_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 7)

static bool alloc_for_program(SAU_Generator *restrict o,
		const SAU_Program *restrict prg) {
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = SAU_MemPool_alloc(o->mem, i * sizeof(EventNode*));
		if (!o->events) goto ERROR;
		o->ev_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = SAU_MemPool_alloc(o->mem, i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = SAU_MemPool_alloc(o->mem,
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
	o->mixer = SAU_create_Mixer();
	if (!o->mixer) goto ERROR;

	return true;
ERROR:
	return false;
}

static const SAU_ProgramOpList blank_oplist = {0};

static bool convert_program(SAU_Generator *restrict o,
		const SAU_Program *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;

	uint32_t vo_wait_time = 0;
	o->srate = srate;
	float scale = 1.f;
	if ((prg->mode & SAU_PMODE_AMP_DIV_VOICES) != 0)
		scale /= o->vo_count;
	SAU_Mixer_set_srate(o->mixer, srate);
	SAU_Mixer_set_scale(o->mixer, scale);
	for (size_t i = 0; i < prg->op_count; ++i) {
		OperatorNode *on = &o->operators[i];
		SAU_init_Osc(&on->osc, srate);
		on->amods = on->fmods = on->pmods = on->fpmods = &blank_oplist;
	}
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SAU_ProgramEvent *prg_e = prg->events[i];
		EventNode *e = SAU_MemPool_alloc(o->mem, sizeof(EventNode));
		if (!e)
			return false;
		uint32_t params;
		uint16_t vo_id = prg_e->vo_id;
		e->wait = SAU_ms_in_samples(prg_e->wait_ms, srate);
		vo_wait_time += e->wait;
		//e->vo_id = SAU_PVO_NO_ID;
		e->vo_id = vo_id;
		e->op_data = prg_e->op_data;
		e->op_data_count = prg_e->op_data_count;
		if (prg_e->vo_data) {
			const SAU_ProgramVoData *pvd = prg_e->vo_data;
			params = pvd->params;
			if (params & SAU_PVOP_GRAPH) {
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
SAU_Generator* SAU_create_Generator(const SAU_Program *restrict prg,
		uint32_t srate) {
	SAU_MemPool *mem = SAU_create_MemPool(0);
	if (!mem)
		return NULL;
	SAU_Generator *o = SAU_MemPool_alloc(mem, sizeof(SAU_Generator));
	if (!o) {
		SAU_destroy_MemPool(mem);
		return NULL;
	}
	o->mem = mem;
	if (!convert_program(o, prg, srate)) {
		SAU_destroy_Generator(o);
		return NULL;
	}
	SAU_global_init_Wave();
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Generator(SAU_Generator *restrict o) {
	if (!o)
		return;
	free(o->gen_bufs);
	SAU_destroy_Mixer(o->mixer);
	SAU_destroy_MemPool(o->mem);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SAU_Generator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->op_count; ++i) {
		const SAU_ProgramOpRef *or = &vn->graph[i];
		if (or->use != SAU_POP_CARR) continue;
		OperatorNode *on = &o->operators[or->id];
		if (on->time > time)
			time = on->time;
	}
	vn->duration = time;
}

/*
 * Process an event update for a timed parameter.
 */
static void handle_ramp_update(SAU_Ramp *restrict ramp,
		uint32_t *restrict ramp_pos,
		const SAU_Ramp *restrict ramp_src) {
	if (!ramp_src)
		return;
	if ((ramp_src->flags & SAU_RAMPP_GOAL) != 0) {
		*ramp_pos = 0;
	}
	SAU_Ramp_copy(ramp, ramp_src);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SAU_Generator *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		VoiceNode *vn = NULL;
		if (e->vo_id != SAU_PVO_NO_ID)
			vn = &o->voices[e->vo_id];
		for (size_t i = 0; i < e->op_data_count; ++i) {
			const SAU_ProgramOpData *od = e->op_data[i];
			OperatorNode *on = &o->operators[od->id];
			uint32_t params = od->params;
			if (od->amods != NULL) on->amods = od->amods;
			if (od->fmods != NULL) on->fmods = od->fmods;
			if (od->pmods != NULL) on->pmods = od->pmods;
			if (od->fpmods != NULL) on->fpmods = od->fpmods;
			if (params & SAU_POPP_WAVE)
				SAU_Osc_set_wave(&on->osc, od->wave);
			if (params & SAU_POPP_TIME) {
				const SAU_Time *src = &od->time;
				if (src->flags & SAU_TIMEP_IMPLICIT) {
					on->time = 0;
					on->flags |= ON_TIME_INF;
				} else {
					on->time = SAU_ms_in_samples(src->v_ms,
							o->srate);
					on->flags &= ~ON_TIME_INF;
				}
			}
			if (params & SAU_POPP_PHASE)
				SAU_Osc_set_phase(&on->osc,
						SAU_Freqor_PHASE(od->phase));
			handle_ramp_update(&on->freq,
					&on->freq_pos, od->freq);
			handle_ramp_update(&on->freq2,
					&on->freq2_pos, od->freq2);
			handle_ramp_update(&on->amp,
					&on->amp_pos, od->amp);
			handle_ramp_update(&on->amp2,
					&on->amp2_pos, od->amp2);
			handle_ramp_update(&vn->pan,
					&vn->pan_pos, od->pan);
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
 * Add audio layer from \p in_buf into \p buf scaled with \p amp.
 *
 * Used to generate output for carrier or PM input.
 */
static void block_mix_add(float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer > 0) {
		for (size_t i = 0; i < buf_len; ++i) {
			buf[i] += in_buf[i] * amp[i];
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			buf[i] = in_buf[i] * amp[i];
		}
	}
}

/*
 * Multiply audio layer from \p in_buf into \p buf,
 * after scaling to a 0.0 to 1.0 range multiplied by
 * the absolute value of \p amp, and with the high and
 * low ends of the range flipped if \p amp is negative.
 *
 * Used to generate output for wave envelope FM or AM input.
 */
static void block_mix_mul_waveenv(float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer > 0) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			buf[i] *= s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			buf[i] = s;
		}
	}
}

/*
 * Generate up to buf_len samples for an operator node,
 * the remainder (if any) zero-filled if layer is zero.
 *
 * Recursively visits the subnodes of the operator node,
 * if any.
 *
 * Returns number of samples generated for the node.
 */
static uint32_t run_block(SAU_Generator *restrict o,
		Buf *restrict bufs, uint32_t buf_len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, uint32_t layer) {
	uint32_t i, len;
	float *mix_buf = *(bufs++), *pm_buf = NULL, *fpm_buf = NULL;
	void *pinc_buf = *(bufs++), *pofs_buf = *(bufs++);
	float *freq = *(bufs++), *amp = NULL;
	float *tmp_buf = NULL;
	len = buf_len;
	/*
	 * Guard against circular references.
	 */
	if ((n->flags & ON_VISITED) != 0) {
		for (i = 0; i < len; ++i)
			mix_buf[i] = 0;
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
	SAU_Ramp_run(&n->freq, &n->freq_pos, freq, len, o->srate, parent_freq);
	if (n->fmods->count > 0) {
		float *freq2 = *(bufs + 0); // #5
		SAU_Ramp_run(&n->freq2, &n->freq2_pos,
				freq2, len, o->srate, parent_freq);
		for (i = 0; i < n->fmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->fmods->ids[i]],
					freq, true, i);
		float *fm_buf = *(bufs + 1); // #6
		for (i = 0; i < len; ++i)
			freq[i] += (freq2[i] - freq[i]) * fm_buf[i];
	} else {
		SAU_Ramp_skip(&n->freq2, &n->freq2_pos, len, o->srate);
	}
	/*
	 * Pre-fill phase buffers.
	 *
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	if (n->pmods->count > 0) {
		for (i = 0; i < n->pmods->count; ++i)
			run_block(o, (bufs + 0), len,
					&o->operators[n->pmods->ids[i]],
					freq, false, i);
		pm_buf = *(bufs + 0); // #5
	}
	if (n->fpmods->count > 0) {
		for (i = 0; i < n->fpmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->fpmods->ids[i]],
					freq, false, i);
		fpm_buf = *(bufs + 1); // #6
	}
	if (!pm_buf && !fpm_buf)
		pofs_buf = NULL; /* run code without it */
	SAU_Freqor_fill(&n->osc.freqor, pinc_buf, pofs_buf, len,
			freq, pm_buf, fpm_buf);
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	amp = *(bufs++); // #5 (++)
	SAU_Ramp_run(&n->amp, &n->amp_pos, amp, len, o->srate, NULL);
	if (n->amods->count > 0) {
		float *amp2 = *(bufs + 0); // #6
		SAU_Ramp_run(&n->amp2, &n->amp2_pos, amp2, len, o->srate, NULL);
		for (i = 0; i < n->amods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->amods->ids[i]],
					freq, true, i);
		float *am_buf = *(bufs + 1); // #7
		for (i = 0; i < len; ++i)
			amp[i] += (amp2[i] - amp[i]) * am_buf[i];
	} else {
		SAU_Ramp_skip(&n->amp2, &n->amp2_pos, len, o->srate);
	}
	tmp_buf = (*bufs + 0); // #6
	SAU_Osc_run(&n->osc, tmp_buf, len, pinc_buf, pofs_buf);
	(wave_env ?
	 block_mix_mul_waveenv :
	 block_mix_add)(mix_buf, len, layer, tmp_buf, amp);
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (!(n->flags & ON_TIME_INF)) {
		if (layer == 0 && skip_len > 0) {
			mix_buf += len;
			for (i = 0; i < skip_len; ++i)
				mix_buf[i] = 0;
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
static uint32_t run_voice(SAU_Generator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	uint32_t out_len = 0;
	const SAU_ProgramOpRef *ops = vn->graph;
	uint32_t opc = vn->op_count;
	if (!ops)
		return 0;
	uint32_t layer = 0;
	uint32_t time;
	uint32_t i;
	time = vn->duration;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	for (i = 0; i < opc; ++i) {
		uint32_t last_len;
		// TODO: finish redesign
		if (ops[i].use != SAU_POP_CARR) continue;
		OperatorNode *n = &o->operators[ops[i].id];
		if (n->time == 0) continue;
		last_len = run_block(o, o->gen_bufs, time, n,
				NULL, false, layer++);
		if (last_len > out_len) out_len = last_len;
	}
	if (out_len > 0) {
		SAU_Mixer_add(o->mixer, o->gen_bufs[0], out_len,
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
static uint32_t run_for_time(SAU_Generator *restrict o,
		uint32_t time, int16_t *restrict buf) {
	int16_t *sp = buf;
	uint32_t gen_len = 0;
	while (time > 0) {
		uint32_t len = time;
		if (len > BUF_LEN) len = BUF_LEN;
		SAU_Mixer_clear(o->mixer);
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
			SAU_Mixer_write(o->mixer, &sp, last_len);
		}
	}
	return gen_len;
}

/*
 * Any error checking following audio generation goes here.
 */
static void check_final_state(SAU_Generator *restrict o) {
	for (uint16_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (!(vn->flags & VN_INIT)) {
			SAU_warning("generator",
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
bool SAU_Generator_run(SAU_Generator *restrict o,
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
