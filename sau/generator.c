/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include <sau/generator.h>
#include <sau/mempool.h>
#define sau_dtoi sau_i64rint  // use for wrap-around behavior
#define sau_ftoi sau_i64rintf // use for wrap-around behavior
#define sau_dscalei(i, scale) (((int32_t)(i)) * (double)(scale))
#define sau_fscalei(i, scale) (((int32_t)(i)) * (float)(scale))
#define sau_divi(i, div) (((int32_t)(i)) / (int32_t)(div))
#include "generator/wosc.h"
#include "generator/rasg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_LEN 1024
typedef float Buf[BUF_LEN];

struct ParWithRangeMod {
	sauLine par, r_par;
	const sauProgramIDArr *mods, *r_mods;
};

/*
 * Operator node flags.
 */
enum {
	ON_INIT = 1<<0,
	ON_VISITED = 1<<1,
	ON_TIME_INF = 1<<2, /* used for SAU_TIMEP_IMPLICIT */
};

typedef struct GenNode {
	uint32_t time;
	uint8_t type;
	uint8_t flags;
	struct ParWithRangeMod amp;
	struct sauClipParam amp_clip;
} GenNode;

typedef struct OscNode {
	GenNode gen;
	struct ParWithRangeMod freq;
	const sauProgramIDArr *pmods, *fpmods;
} OscNode;

typedef struct WOscNode {
	OscNode osc;
	sauWOsc wosc;
} WOscNode;

typedef struct RasGNode {
	OscNode osc;
	sauRasG rasg;
} RasGNode;

typedef union OperatorNode {
	GenNode gen; // generator base type
	OscNode osc; // oscillator base type
	WOscNode wo;
	RasGNode rg;
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
	const sauProgramOpRef *graph;
	uint32_t op_count;
	sauLine pan;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	const sauProgramEvent *prg_event;
} EventNode;

/*
 * Generator flags.
 */
enum {
	GEN_OUT_CLEAR = 1<<0,
};

struct sauGenerator {
	uint32_t srate;
	uint16_t gen_flags;
	uint16_t gen_mix_add_max;
	Buf *gen_bufs, *mix_bufs;
	size_t event, ev_count;
	EventNode *events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	float amp_scale;
	uint32_t op_count;
	OperatorNode *operators;
	sauMempool *mem;
};

// maximum number of buffers needed for op nesting depth
#define COUNT_GEN_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 7)

static bool alloc_for_program(sauGenerator *restrict o,
		const sauProgram *restrict prg) {
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = sau_mpalloc(o->mem, i * sizeof(EventNode));
		if (!o->events) goto ERROR;
		o->ev_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = sau_mpalloc(o->mem, i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = sau_mpalloc(o->mem, i * sizeof(OperatorNode));
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

static const sauProgramIDArr blank_idarr = {0};

static bool convert_program(sauGenerator *restrict o,
		const sauProgram *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;

	/*
	 * The event timeline needs carry to ensure event node timing doesn't
	 * run short (with more nodes, more values), compared to other nodes.
	 */
	int ev_time_carry = 0;
	o->srate = srate;
	o->amp_scale = 0.5f; // half for panning sum
	if ((prg->mode & SAU_PMODE_AMP_DIV_VOICES) != 0)
		o->amp_scale /= o->vo_count;
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const sauProgramEvent *prg_e = &prg->events[i];
		EventNode *e = &o->events[i];
		e->wait = sau_ms_in_samples(prg_e->wait_ms, srate,
				&ev_time_carry);
		e->prg_event = prg_e;
	}

	return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
sauGenerator* sau_create_Generator(const sauProgram *restrict prg,
		uint32_t srate) {
	sauMempool *mem = sau_create_Mempool(0);
	if (!mem)
		return NULL;
	sauGenerator *o = sau_mpalloc(mem, sizeof(sauGenerator));
	if (!o) {
		sau_destroy_Mempool(mem);
		return NULL;
	}
	o->mem = mem;
	if (!convert_program(o, prg, srate)) {
		sau_destroy_Generator(o);
		return NULL;
	}
	sau_global_init_Wave();
	return o;
}

/**
 * Destroy instance.
 */
void sau_destroy_Generator(sauGenerator *restrict o) {
	if (!o)
		return;
	free(o->gen_bufs);
	free(o->mix_bufs);
	sau_destroy_Mempool(o->mem);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(sauGenerator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->op_count; ++i) {
		const sauProgramOpRef *or = &vn->graph[i];
		if (or->use != SAU_POP_CARR) continue;
		GenNode *gen = &o->operators[or->id].gen;
		if (gen->time > time)
			time = gen->time;
	}
	vn->duration = time;
}

/*
 * Initialize an operator node for use as the given type.
 */
static void prepare_op(sauGenerator *restrict o,
		OperatorNode *restrict n, const sauProgramOpData *restrict od) {
	switch (od->type) {
	case SAU_POPT_WAVE: {
		WOscNode *wo = &n->wo;
		memset(n, 0, sizeof(*wo));
		sau_init_WOsc(&wo->wosc, o->srate);
		goto OSC_COMMON; }
	case SAU_POPT_RAS: {
		RasGNode *rg = &n->rg;
		memset(n, 0, sizeof(*rg));
		sau_init_RasG(&rg->rasg, o->srate);
		sauRasG_set_cycle(&rg->rasg, od->seed);
		goto OSC_COMMON; }
	}
	if (false)
	OSC_COMMON: {
		OscNode *osc = &n->osc;
		osc->freq.mods = osc->freq.r_mods =
		osc->pmods = osc->fpmods = &blank_idarr;
	}
	GenNode *gen = &n->gen;
	gen->amp.mods = gen->amp.r_mods = &blank_idarr;
	gen->amp_clip.level = 1.f;
	gen->type = od->type;
	gen->flags = ON_INIT;
}

/*
 * Update an operator node with new data from event.
 */
static void update_op(sauGenerator *restrict o,
		OperatorNode *restrict n, const sauProgramOpData *restrict od) {
	uint32_t params = od->params;
	switch (od->type) {
	case SAU_POPT_WAVE: {
		WOscNode *wo = &n->wo;
		if (params & SAU_POPP_PHASE)
			sauWOsc_set_phase(&wo->wosc, od->phase);
		if (params & SAU_POPP_WAVE)
			sauWOsc_set_wave(&wo->wosc, od->wave);
		goto OSC_COMMON; }
	case SAU_POPT_RAS: {
		RasGNode *rg = &n->rg;
		if (params & SAU_POPP_PHASE)
			sauRasG_set_phase(&rg->rasg, od->phase);
		if (params & SAU_POPP_RAS)
			sauRasG_set_opt(&rg->rasg, &od->ras_opt);
		goto OSC_COMMON; }
	}
	if (false)
	OSC_COMMON: {
		OscNode *osc = &n->osc;
		if (od->fmods) osc->freq.mods = od->fmods;
		if (od->rfmods) osc->freq.r_mods = od->rfmods;
		if (od->pmods) osc->pmods = od->pmods;
		if (od->fpmods) osc->fpmods = od->fpmods;
		sauLine_copy(&osc->freq.par, od->freq, o->srate);
		sauLine_copy(&osc->freq.r_par, od->freq2, o->srate);
	}
	GenNode *gen = &n->gen;
	if (params & SAU_POPP_TIME) {
		const sauTime *src = &od->time;
		if (src->flags & SAU_TIMEP_IMPLICIT) {
			gen->time = 0;
			gen->flags |= ON_TIME_INF;
		} else {
			gen->time = sau_ms_in_samples(src->v_ms,
					o->srate, NULL);
			gen->flags &= ~ON_TIME_INF;
		}
	}
	if (od->amods) gen->amp.mods = od->amods;
	if (od->ramods) gen->amp.r_mods = od->ramods;
	sauLine_copy(&gen->amp.par, od->amp, o->srate);
	sauLine_copy(&gen->amp.r_par, od->amp2, o->srate);
	if (od->amp_clip.set_type)
		gen->amp_clip.type = od->amp_clip.type;
	if (od->amp_clip.set_level)
		gen->amp_clip.level = od->amp_clip.level;
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(sauGenerator *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		const sauProgramEvent *pe = e->prg_event;
		const sauProgramVoData *vd = pe->vo_data;
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		VoiceNode *vn = NULL;
		if (pe->vo_id != SAU_PVO_NO_ID)
			vn = &o->voices[pe->vo_id];
		for (size_t i = 0; i < pe->op_data_count; ++i) {
			const sauProgramOpData *od = &pe->op_data[i];
			OperatorNode *n = &o->operators[od->id];
			if (!(n->gen.flags & ON_INIT))
				prepare_op(o, n, od);
			update_op(o, n, od);
			sauLine_copy(&vn->pan, od->pan, o->srate);
		}
		if (vd) {
			if (vd->op_list) {
				vn->graph = vd->op_list;
				vn->op_count = vd->op_count;
			}
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
 * Used to generate output for carrier or additive modulator.
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
 * Used to generate output for modulation with value range.
 */
static void block_mix_mul_waveenv(float *restrict buf, size_t buf_len,
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer) {
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
 * Handle audio layer according to options.
 */
static void block_mix(GenNode *restrict gen,
		float *restrict buf, size_t buf_len,
		bool wave_env, bool layer,
		float *restrict in_buf,
		const float *restrict amp) {
	if (gen->amp_clip.type)
		sauClip_apply_funcs[gen->amp_clip.type](in_buf, buf_len,
				gen->amp_clip.level);
	(wave_env ?
	 block_mix_mul_waveenv :
	 block_mix_add)(buf, buf_len, layer, in_buf, amp);
}

static uint32_t run_block(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t buf_len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, bool layer);

static void run_param_with_rangemod(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		struct ParWithRangeMod *restrict n,
		float *restrict param_mulbuf,
		float *restrict reused_freq) {
	uint32_t i;
	float *par_buf = *(bufs + 0);
	float *freq = (reused_freq ? reused_freq : par_buf);
	sauLine_run(&n->par, par_buf, len, param_mulbuf);
	if (n->r_mods->count > 0) {
		float *r_par_buf = *(bufs + 1);
		sauLine_run(&n->r_par, r_par_buf, len, param_mulbuf);
		for (i = 0; i < n->r_mods->count; ++i)
			run_block(o, (bufs + 2), len,
					&o->operators[n->r_mods->ids[i]],
					freq, true, i);
		float *mod_buf = *(bufs + 2);
		for (i = 0; i < len; ++i)
			par_buf[i] += (r_par_buf[i] - par_buf[i]) * mod_buf[i];
	} else {
		sauLine_skip(&n->r_par, len);
	}
	if (n->mods->count > 0) {
		for (i = 0; i < n->mods->count; ++i)
			run_block(o, (bufs + 0), len,
					&o->operators[n->mods->ids[i]],
					freq, false, true);
	}
}

/*
 * The WOscNode sub-function for run_block().
 *
 * Needs up to 6 buffers for its own node level.
 */
static void run_block_wosc(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, bool layer) {
	uint32_t i;
	float *mix_buf = *(bufs++), *pm_buf = NULL, *fpm_buf = NULL;
	void *phase_buf = *(bufs++);
	float *freq = NULL, *amp = NULL;
	float *tmp_buf = NULL;
	/*
	 * Handle frequency (alternatively ratio) parameter,
	 * including frequency modulation if modulators linked.
	 */
	run_param_with_rangemod(o, bufs, len, &n->osc.freq, parent_freq, NULL);
	freq = *(bufs++); // #3 (++) and temporary #4, #5
	/*
	 * Pre-fill phase buffers.
	 *
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	if (n->osc.pmods->count > 0) {
		for (i = 0; i < n->osc.pmods->count; ++i)
			run_block(o, (bufs + 0), len,
					&o->operators[n->osc.pmods->ids[i]],
					freq, false, i);
		pm_buf = *(bufs + 0); // #4
	}
	if (n->osc.fpmods->count > 0) {
		for (i = 0; i < n->osc.fpmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->osc.fpmods->ids[i]],
					freq, false, i);
		fpm_buf = *(bufs + 1); // #5
	}
	sauPhasor_fill(&n->wo.wosc.phasor, phase_buf, len,
			freq, pm_buf, fpm_buf);
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	run_param_with_rangemod(o, bufs, len, &n->gen.amp, NULL, freq);
	amp = *(bufs++); // #4 (++) and temporary #5, #6
	tmp_buf = (*bufs + 0); // #5
	sauWOsc_run(&n->wo.wosc, tmp_buf, len, phase_buf);
	block_mix(&n->wo.osc.gen, mix_buf, len, wave_env, layer, tmp_buf, amp);
}

/*
 * The RasGNode sub-function for run_block().
 *
 * Needs up to 7 buffers for its own node level.
 */
static void run_block_rasg(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, bool layer) {
	uint32_t i;
	float *mix_buf = *(bufs++), *pm_buf = NULL, *fpm_buf = NULL;
	void *cycle_buf = *(bufs++), *rasg_buf = *(bufs++);
	float *freq = NULL, *amp = NULL;
	float *tmp_buf = NULL, *tmp2_buf = NULL;
	/*
	 * Handle frequency (alternatively ratio) parameter,
	 * including frequency modulation if modulators linked.
	 */
	run_param_with_rangemod(o, bufs, len, &n->osc.freq, parent_freq, NULL);
	freq = *(bufs++); // #4 (++) and temporary #5, #6
	/*
	 * Pre-fill cycle & phase buffers.
	 *
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	if (n->osc.pmods->count > 0) {
		for (i = 0; i < n->osc.pmods->count; ++i)
			run_block(o, (bufs + 0), len,
					&o->operators[n->osc.pmods->ids[i]],
					freq, false, i);
		pm_buf = *(bufs + 0); // #5
	}
	if (n->osc.fpmods->count > 0) {
		for (i = 0; i < n->osc.fpmods->count; ++i)
			run_block(o, (bufs + 1), len,
					&o->operators[n->osc.fpmods->ids[i]],
					freq, false, i);
		fpm_buf = *(bufs + 1); // #6
	}
	sauCyclor_fill(&n->rg.rasg.cyclor, cycle_buf, rasg_buf, len,
			freq, pm_buf, fpm_buf);
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	run_param_with_rangemod(o, bufs, len, &n->gen.amp, NULL, freq);
	amp = *(bufs++); // #5 (++) and temporary #6, #7
	tmp_buf = *(bufs + 0); // #6
	tmp2_buf = *(bufs + 1); // #7
	sauRasG_run(&n->rg.rasg, len, rasg_buf, tmp_buf, tmp2_buf, cycle_buf);
	block_mix(&n->rg.osc.gen, mix_buf, len, wave_env, layer, rasg_buf, amp);
}

/*
 * Generate up to \p buf_len samples for an operator node,
 * the remainder (if any) zero-filled when \p layer false.
 *
 * Recursively visits the subnodes of the operator node,
 * if any. The first buffer will be used for the output.
 *
 * \return number of samples generated
 */
static uint32_t run_block(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t buf_len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, bool layer) {
	GenNode *gen = &n->gen;
	float *mix_buf = *bufs;
	/*
	 * Guard against circular references.
	 */
	if ((gen->flags & ON_VISITED) != 0) {
		for (uint32_t i = 0; i < buf_len; ++i)
			mix_buf[i] = 0;
		return buf_len;
	}
	gen->flags |= ON_VISITED;
	/*
	 * Limit length to time duration of operator.
	 */
	uint32_t len = buf_len, skip_len = 0;
	if (gen->time < len && !(gen->flags & ON_TIME_INF)) {
		skip_len = len - gen->time;
		len = gen->time;
	}
	/*
	 * Use sub-function.
	 */
	switch (gen->type) {
	case SAU_POPT_WAVE:
		run_block_wosc(o, bufs, len, n, parent_freq, wave_env, layer);
		break;
	case SAU_POPT_RAS:
		run_block_rasg(o, bufs, len, n, parent_freq, wave_env, layer);
		break;
	}
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (!(gen->flags & ON_TIME_INF)) {
		if (!layer && skip_len > 0) {
			mix_buf += len;
			for (uint32_t i = 0; i < skip_len; ++i)
				mix_buf[i] = 0;
		}
		gen->time -= len;
	}
	gen->flags &= ~ON_VISITED;
	return len;
}

/*
 * Clear the mix buffers. To be called before adding voice outputs.
 */
static void mix_clear(sauGenerator *restrict o) {
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
static void mix_add(sauGenerator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	float *s_buf = o->gen_bufs[0];
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	if (vn->pan.flags & SAU_LINEP_GOAL) {
		float *pan_buf = o->gen_bufs[1];
		sauLine_run(&vn->pan, pan_buf, len, NULL);
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * pan_buf[i];
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else {
		sauLine_skip(&vn->pan, len);
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
static void mix_write_mono(sauGenerator *restrict o,
		int16_t **restrict spp, uint32_t len) {
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	o->gen_flags &= ~GEN_OUT_CLEAR;
	for (uint32_t i = 0; i < len; ++i) {
		float s_m = (mix_l[i] + mix_r[i]) * 0.5f;
		s_m = sau_fclampf(s_m, -1.f, 1.f);
		*(*spp)++ += lrintf(s_m * (float) INT16_MAX);
	}
}

/*
 * Write the final output from the mix buffers (0 = left, 1 = right)
 * into the 16-bit stereo (interleaved) buffer pointed to by \p spp.
 * Advances \p spp.
 */
static void mix_write_stereo(sauGenerator *restrict o,
		int16_t **restrict spp, uint32_t len) {
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	o->gen_flags &= ~GEN_OUT_CLEAR;
	for (uint32_t i = 0; i < len; ++i) {
		float s_l = mix_l[i];
		float s_r = mix_r[i];
		s_l = sau_fclampf(s_l, -1.f, 1.f);
		s_r = sau_fclampf(s_r, -1.f, 1.f);
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
static uint32_t run_voice(sauGenerator *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	uint32_t out_len = 0;
	const sauProgramOpRef *ops = vn->graph;
	uint32_t opc = vn->op_count;
	if (!ops)
		return 0;
	bool layer = false;
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
		if (n->gen.time == 0) continue;
		last_len = run_block(o, o->gen_bufs, time, n,
				NULL, false, layer);
		layer = true;
		if (last_len > out_len) out_len = last_len;
	}
	if (out_len > 0) {
		mix_add(o, vn, out_len);
	}
	vn->duration -= time;
	return out_len;
}

/*
 * Run voices for \p time, repeatedly generating up to BUF_LEN samples
 * and writing them into the 16-bit interleaved channels buffer \p buf.
 *
 * \return number of samples generated
 */
static uint32_t run_for_time(sauGenerator *restrict o,
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
static void check_final_state(sauGenerator *restrict o) {
	for (uint16_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (!(vn->flags & VN_INIT)) {
			sau_warning("generator",
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
bool sauGenerator_run(sauGenerator *restrict o,
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
