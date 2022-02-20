/* sgensys: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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
#include "mempool.h"
#define SGS_dtoi SGS_i64rint  // use for wrap-around behavior
#define SGS_ftoi SGS_i64rintf // use for wrap-around behavior
#include "generator/osc.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_LEN 1024
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
	const SGS_ProgramIDArr *amods, *fmods, *pmods, *fpmods;
	SGS_Ramp amp, freq;
	SGS_Ramp amp2, freq2;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
};

typedef struct VoiceNode {
	uint32_t duration;
	uint8_t flags;
	uint32_t carr_op_id;
	SGS_Ramp pan;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	const SGS_ProgramEvent *prg_event;
} EventNode;

/*
 * Generator flags.
 */
enum {
	GEN_OUT_CLEAR = 1<<0,
};

struct SGS_Generator {
	uint32_t srate;
	uint16_t gen_flags;
	uint16_t gen_mix_add_max;
	Buf *restrict gen_bufs, *restrict mix_bufs;
	size_t event, ev_count;
	EventNode *events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	float amp_scale;
	uint32_t op_count;
	OperatorNode *operators;
	SGS_Mempool *mem;
};

// maximum number of buffers needed for op nesting depth
#define COUNT_GEN_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 6)

static bool alloc_for_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg) {
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = SGS_mpalloc(o->mem, i * sizeof(EventNode));
		if (!o->events) goto ERROR;
		o->ev_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = SGS_mpalloc(o->mem, i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = SGS_mpalloc(o->mem,
				i * sizeof(OperatorNode));
		if (!o->operators) goto ERROR;
		o->op_count = i;
	}
	i = COUNT_GEN_BUFS(prg->op_nest_depth);
	if (i > 0) {
		o->gen_bufs = calloc(i, sizeof(Buf));
		if (!o->gen_bufs) goto ERROR;
	}
	o->mix_bufs = calloc(2, sizeof(Buf));
	if (!o->mix_bufs) goto ERROR;

	return true;
ERROR:
	return false;
}

static const SGS_ProgramIDArr blank_idarr = {0};

static bool convert_program(SGS_Generator *restrict o,
		const SGS_Program *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;

	/*
	 * The event timeline needs carry to ensure event node timing doesn't
	 * run short (with more nodes, more values), compared to other nodes.
	 */
	int ev_time_carry = 0;
	o->srate = srate;
	o->amp_scale = 0.5f; // half for panning sum
	if ((prg->mode & SGS_PMODE_AMP_DIV_VOICES) != 0)
		o->amp_scale /= o->vo_count;
	for (size_t i = 0; i < prg->op_count; ++i) {
		OperatorNode *on = &o->operators[i];
		SGS_init_Osc(&on->osc, srate);
		on->amods = on->fmods = on->pmods = on->fpmods = &blank_idarr;
	}
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SGS_ProgramEvent *prg_e = &prg->events[i];
		EventNode *e = &o->events[i];
		e->wait = SGS_ms_in_samples(prg_e->wait_ms, srate,
				&ev_time_carry);
		e->prg_event = prg_e;
	}

	return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SGS_Generator* SGS_create_Generator(const SGS_Program *restrict prg,
		uint32_t srate) {
	SGS_Mempool *mem = SGS_create_Mempool(0);
	if (!mem)
		return NULL;
	SGS_Generator *o = SGS_mpalloc(mem, sizeof(SGS_Generator));
	if (!o) {
		SGS_destroy_Mempool(mem);
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
	free(o->mix_bufs);
	SGS_destroy_Mempool(o->mem);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SGS_Generator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	OperatorNode *on = &o->operators[vn->carr_op_id];
	if (on->time > time)
		time = on->time;
	vn->duration = time;
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SGS_Generator *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		const SGS_ProgramEvent *pe = e->prg_event;
		const SGS_ProgramVoData *vd = pe->vo_data;
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		VoiceNode *vn = NULL;
		if (pe->vo_id != SGS_PVO_NO_ID)
			vn = &o->voices[pe->vo_id];
		for (size_t i = 0; i < pe->op_data_count; ++i) {
			const SGS_ProgramOpData *od = &pe->op_data[i];
			OperatorNode *on = &o->operators[od->id];
			uint32_t params = od->params;
			if (od->amods) on->amods = od->amods;
			if (od->fmods) on->fmods = od->fmods;
			if (od->pmods) on->pmods = od->pmods;
			if (od->fpmods) on->fpmods = od->fpmods;
			if (params & SGS_POPP_WAVE)
				SGS_Osc_set_wave(&on->osc, od->wave);
			if (params & SGS_POPP_TIME) {
				const SGS_Time *src = &od->time;
				if (src->flags & SGS_TIMEP_IMPLICIT) {
					on->time = 0;
					on->flags |= ON_TIME_INF;
				} else {
					on->time = SGS_ms_in_samples(src->v_ms,
							o->srate, NULL);
					on->flags &= ~ON_TIME_INF;
				}
			}
			if (params & SGS_POPP_PHASE)
				SGS_Osc_set_phase(&on->osc, od->phase);
			SGS_Ramp_copy(&vn->pan, od->pan, o->srate);
			SGS_Ramp_copy(&on->amp, od->amp, o->srate);
			SGS_Ramp_copy(&on->amp2, od->amp2, o->srate);
			SGS_Ramp_copy(&on->freq, od->freq, o->srate);
			SGS_Ramp_copy(&on->freq2, od->freq2, o->srate);
		}
		if (vd) {
			vn->carr_op_id = vd->carr_op_id;
		}
		if (vn) {
			vn->flags |= VN_INIT;
			if (o->voice > pe->vo_id) {
				/* go back to re-activated node */
				o->voice = pe->vo_id;
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
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer) {
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
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabsf(s_amp);
			buf[i] *= s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabsf(s_amp);
			buf[i] = s;
		}
	}
}

/*
 * Generate up to \p buf_len samples for an operator node,
 * the remainder (if any) zero-filled when \p layer false.
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
		bool wave_env, bool layer) {
	uint32_t i, len;
	float *mix_buf = *(bufs++), *pm_buf = NULL, *fpm_buf = NULL;
	void *phase_buf = *(bufs++);
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
	 * Handle frequency (alternatively ratio) parameter,
	 * including frequency modulation if modulators linked.
	 */
	SGS_Ramp_run(&n->freq, freq, len, parent_freq);
	if (n->fmods->count > 0) {
		float *freq2 = *(bufs + 0); // #4
		SGS_Ramp_run(&n->freq2, freq2, len, parent_freq);
		for (i = 0; i < n->fmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->fmods->ids[i]],
					freq, true, i);
		float *fm_buf = *(bufs + 1); // #5
		for (i = 0; i < len; ++i)
			freq[i] += (freq2[i] - freq[i]) * fm_buf[i];
	} else {
		SGS_Ramp_skip(&n->freq2, len);
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
		pm_buf = *(bufs + 0); // #4
	}
	if (n->fpmods->count > 0) {
		for (i = 0; i < n->fpmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->fpmods->ids[i]],
					freq, false, i);
		fpm_buf = *(bufs + 1); // #5
	}
	SGS_Phasor_fill(&n->osc.phasor, phase_buf, len,
			freq, pm_buf, fpm_buf);
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	amp = *(bufs++); // #4 (++)
	SGS_Ramp_run(&n->amp, amp, len, NULL);
	if (n->amods->count > 0) {
		float *amp2 = *(bufs + 0); // #5
		SGS_Ramp_run(&n->amp2, amp2, len, NULL);
		for (i = 0; i < n->amods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->amods->ids[i]],
					freq, true, i);
		float *am_buf = *(bufs + 1); // #6
		for (i = 0; i < len; ++i)
			amp[i] += (amp2[i] - amp[i]) * am_buf[i];
	} else {
		SGS_Ramp_skip(&n->amp2, len);
	}
	tmp_buf = (*bufs + 0); // #5
	SGS_Osc_run(&n->osc, tmp_buf, len, phase_buf);
	(wave_env ?
	 block_mix_mul_waveenv :
	 block_mix_add)(mix_buf, len, layer, tmp_buf, amp);
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (!(n->flags & ON_TIME_INF)) {
		if (!layer && skip_len > 0) {
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
 * Clear the mix buffers. To be called before adding voice outputs.
 */
static void mix_clear(SGS_Generator *restrict o) {
	if (o->gen_mix_add_max == 0)
		return;
	memset(o->mix_bufs[0], 0, sizeof(float) * o->gen_mix_add_max);
	memset(o->mix_bufs[1], 0, sizeof(float) * o->gen_mix_add_max);
	o->gen_mix_add_max = 0;
}

/*
 * Add output for voice node \p vn into the mix buffers
 * (0 = left, 1 = right) from the first generator buffer.
 *
 * The second generator buffer is used for panning if dynamic panning
 * is used.
 */
static void mix_add(SGS_Generator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	float *s_buf = o->gen_bufs[0];
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	if (vn->pan.flags & SGS_RAMPP_GOAL) {
		float *pan_buf = o->gen_bufs[1];
		SGS_Ramp_run(&vn->pan, pan_buf, len, NULL);
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * pan_buf[i];
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else {
		SGS_Ramp_skip(&vn->pan, len);
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * vn->pan.v0;
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	}
	if (o->gen_mix_add_max < len) o->gen_mix_add_max = len;
}

/**
 * Write the final output from the mix buffers (0 = left, 1 = right)
 * downmixed to mono into a 16-bit buffer
 * pointed to by \p spp. Advances \p spp.
 */
static void mix_write_mono(SGS_Generator *restrict o,
		int16_t **restrict spp, uint32_t len) {
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	o->gen_flags &= ~GEN_OUT_CLEAR;
	for (uint32_t i = 0; i < len; ++i) {
		float s_m = (mix_l[i] + mix_r[i]) * 0.5f;
		if (s_m > 1.f) s_m = 1.f;
		else if (s_m < -1.f) s_m = -1.f;
		*(*spp)++ += lrintf(s_m * (float) INT16_MAX);
	}
}

/*
 * Write the final output from the mix buffers (0 = left, 1 = right)
 * into the 16-bit stereo (interleaved) buffer pointed to by \p spp.
 * Advances \p spp.
 */
static void mix_write_stereo(SGS_Generator *restrict o,
		int16_t **restrict spp, uint32_t len) {
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	o->gen_flags &= ~GEN_OUT_CLEAR;
	for (uint32_t i = 0; i < len; ++i) {
		float s_l = mix_l[i];
		float s_r = mix_r[i];
		if (s_l > 1.f) s_l = 1.f;
		else if (s_l < -1.f) s_l = -1.f;
		if (s_r > 1.f) s_r = 1.f;
		else if (s_r < -1.f) s_r = -1.f;
		*(*spp)++ += lrintf(s_l * (float) INT16_MAX);
		*(*spp)++ += lrintf(s_r * (float) INT16_MAX);
	}
}

/*
 * Generate up to BUF_LEN samples for a voice, mixed into the
 * mix buffers.
 *
 * \return number of samples generated
 */
static uint32_t run_voice(SGS_Generator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	OperatorNode *n = &o->operators[vn->carr_op_id];
	uint32_t time = vn->duration, out_len = 0;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	if (n->time > 0)
		out_len = run_block(o, o->gen_bufs, time, n,
				NULL, false, false);
	if (out_len > 0)
		mix_add(o, vn, out_len);
	vn->duration -= time;
	return out_len;
}

/*
 * Run voices for \p time, repeatedly generating up to BUF_LEN samples
 * and writing them into the 16-bit interleaved channels buffer \p buf.
 *
 * \return number of samples generated
 */
static uint32_t run_for_time(SGS_Generator *restrict o,
		uint32_t time, int16_t *restrict buf, bool stereo) {
	int16_t *sp = buf;
	uint32_t gen_len = 0;
	while (time > 0) {
		uint32_t len = (time < BUF_LEN) ? time : BUF_LEN;
		time -= len;
		mix_clear(o);
		uint32_t last_len = 0;
		for (uint32_t i = o->voice; i < o->vo_count; ++i) {
			VoiceNode *vn = &o->voices[i];
			if (vn->duration != 0) {
				uint32_t voice_len = run_voice(o, vn, len);
				if (voice_len > last_len) last_len = voice_len;
			}
		}
		if (last_len > 0) {
			gen_len += last_len;
			(stereo ?
			 mix_write_stereo :
			 mix_write_mono)(o, &sp, last_len);
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
 * buf_len new samples into the interleaved channels buffer buf. Any values
 * after the end of the signal will be zero'd.
 *
 * If supplied, out_len will be set to the precise length generated
 * for this call, which is buf_len unless the signal ended earlier.
 *
 * Note that \p buf_len * channels is assumed not to increase between calls.
 *
 * \return true unless the signal has ended
 */
bool SGS_Generator_run(SGS_Generator *restrict o,
		int16_t *restrict buf, size_t buf_len, bool stereo,
		size_t *restrict out_len) {
	int16_t *sp = buf;
	uint32_t len = buf_len;
	uint32_t skip_len, last_len, gen_len = 0;
	if (!(o->gen_flags & GEN_OUT_CLEAR)) {
		o->gen_flags |= GEN_OUT_CLEAR;
		memset(buf, 0, sizeof(int16_t) * (stereo ? len * 2 : len));
	}
PROCESS:
	skip_len = 0;
	while (o->event < o->ev_count) {
		EventNode *e = &o->events[o->event];
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
	last_len = run_for_time(o, len, sp, stereo);
	if (skip_len > 0) {
		gen_len += len;
		if (stereo)
			sp += len * 2;
		else
			sp += len;
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
