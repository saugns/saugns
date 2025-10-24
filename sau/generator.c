/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2025 Joel K. Pettersson
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
#define sau_nzero(a, n) memset((a), 0, sizeof((a)[0]) * (n))
static void sau_nzerof(float *restrict a, size_t n) {
	for (size_t i=0; i<n; ++i) a[i]=0.f;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generator/envel.h"
#include "generator/noise.h"
#include "generator/wosc.h"
#include "generator/rasg.h"

#define BUF_LEN 1024
typedef float Buf[BUF_LEN];

struct ParWithRangeMod {
	sauLine a, b, e;
	sauEnvGen env;
	const sauProgramIDArr *mods1, *mods2, *r_mods, *e_mods, *mods_add;
};

struct ParPDSet {
	struct ParWithRangeMod main, freq, offset;
};

struct BlockBufIDs {
	uint8_t out_id, amp_id;
	uint8_t freq_id; // 0 if none/unused (freq is never mix buffer 0)
};

/*
 * Generator node flags.
 */
enum {
	GN_VISITED  = 1U<<0,
	GN_TIME_INF = 1U<<1, /* used for SAU_TIMEP_IMPLICIT */
};

typedef struct GenBase {
	uint32_t time;
	uint32_t note_dur; /* time without countdown, from here or carrier */
	uint32_t obj_id;
	uint8_t type;
	uint8_t flags;
	//uint8_t cycle_used; /* stored here, used for oscillator only */
	struct ParWithRangeMod amp, pan;
	struct ParWithRangeMod freq; // here so wrapper gens can pass to nested
} GenBase;

typedef struct AmpNode {
	GenBase gen;
} AmpNode;

typedef struct NoiseGNode {
	GenBase gen;
	sauNoiseG noiseg;
} NoiseGNode;

typedef struct OscBase {
	GenBase gen;
	const sauProgramIDArr *pmods, *fpmods;
	struct ParPDSet pd[SAU_PPD_TYPES];
	struct ParWithRangeMod pm_a;
} OscBase;

typedef struct WOscNode {
	OscBase osc;
	sauWOsc wosc;
} WOscNode;

typedef struct RasGNode {
	OscBase osc;
	sauRasG rasg;
} RasGNode;

typedef union AnyGen {
	GenBase gen; // generator base type
	AmpNode ag;
	NoiseGNode ng;
	OscBase osc; // oscillator base type
	WOscNode wo;
	RasGNode rg;
} AnyGen;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
};

typedef struct VoiceNode {
	uint32_t duration;
	uint8_t flags;
	uint32_t carr_gen_id;
} VoiceNode;

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
	Buf *bufs;
	size_t event, ev_count;
	const sauParseEvData *ev_data;
	uint32_t ev_pos, ev_wait;
	int ev_time_carry;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	float amp_scale;
	uint32_t gen_count;
	AnyGen *gens;
	sauMempool *mem;
};

// maximum number of buffers needed for generator nesting depth
#define COUNT_GEN_BUFS(gen_nest_depth) ((1 + (gen_nest_depth)) * 9)

#define MIX_BUFS 2

static bool alloc_for_program(sauGenerator *restrict o,
		const sauParse *restrict prg) {
	size_t i;
	o->ev_count = prg->ev_count;
	if ((i = prg->vo_count) > 0) {
		o->voices = sau_mpalloc(o->mem, i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	if ((i = prg->gen_count) > 0) {
		o->gens = sau_mpalloc(o->mem, i * sizeof(AnyGen));
		if (!o->gens) goto ERROR;
		o->gen_count = i;
	}
	i = COUNT_GEN_BUFS(prg->gen_nest_depth);
	if (!(o->bufs = calloc(i + MIX_BUFS, sizeof(Buf)))) goto ERROR;
	o->bufs += MIX_BUFS;
	return true;
ERROR:
	return false;
}

static const sauProgramIDArr blank_idarr = {0};

// default values for each phase distortion correspond to doing nothing
static const float pd_v_default[SAU_PPD_TYPES] = {
	[SAU_PPD_C] = 1.0,
	[SAU_PPD_D] = 1.0,
	[SAU_PPD_H] = 0.0,
	[SAU_PPD_X] = 0.5,
	[SAU_PPD_Y] = 0.5,
};

/*
 * The event timeline needs carry to ensure event node timing doesn't
 * run short (with more nodes, more values), compared to other nodes.
 */
static inline void prepare_event(sauGenerator *restrict o,
		sauParseEvData *restrict prg_e) {
	o->ev_data = prg_e;
	if (prg_e) o->ev_wait = sau_ms_in_samples(prg_e->wait_ms,
			o->srate, &o->ev_time_carry);
}

static bool convert_program(sauGenerator *restrict o,
		const sauParse *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;
	o->srate = srate;
	o->amp_scale = 0.5f; // half for panning sum
	if (prg->is_ampmult_set) o->amp_scale *= prg->sopt.ampmult;
	if (prg->is_amp_autoscaled) o->amp_scale /= o->vo_count;
	prepare_event(o, prg->events);
	return true;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
sauGenerator* sau_create_Generator(const sauParse *restrict prg,
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
	free(o->bufs - MIX_BUFS);
	sau_destroy_Mempool(o->mem);
}

/*
 * Set voice duration according to the current list of generators.
 */
static void set_voice_duration(sauGenerator *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	GenBase *gen = &o->gens[vn->carr_gen_id].gen;
	if (gen->time > time)
		time = gen->time;
	vn->duration = time;
}

// used to probe usage in updates, ahead of actually filling buffers later
static float *run_valrange_param(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		struct ParWithRangeMod *restrict n,
		float *restrict param_mulbuf,
		float *restrict reused_freq,
		bool is_freq, bool force_fill);

/*
 * Initialize range.
 *
 * The \p v0 value is a fallback which may differ from script defaults.
 */
static sauNoinline void
prepare_range(struct ParWithRangeMod *restrict rm, float v0, float vt) {
	sau_init_LinePar(&rm->a.par, v0);
	sau_init_LinePar(&rm->b.par, vt);
	sau_init_LinePar(&rm->e.par, vt);
	sau_init_EnvGen(&rm->env);
	rm->mods1 = rm->mods2 = rm->r_mods = rm->e_mods = rm->mods_add =
		&blank_idarr;
}

/*
 * Initialize a generator node for use as the given type.
 */
static void prepare_gen(sauGenerator *restrict o,
		AnyGen *restrict n,
		const sauParseGenData *restrict gd) {
	*n = (AnyGen){0};
	switch (gd->ref.gen_type) {
	case SAU_PGEN_N_amp: break;
	case SAU_PGEN_N_noise: break;
	case SAU_PGEN_N_wave: {
		WOscNode *wo = &n->wo;
		sau_init_WOsc(&wo->wosc, o->srate);
		goto OSC_COMMON; }
	case SAU_PGEN_N_raseg: {
		RasGNode *rg = &n->rg;
		sau_init_RasG(&rg->rasg, o->srate);
		goto OSC_COMMON; }
	}
	if (false)
	OSC_COMMON: {
		OscBase *osc = &n->osc;
		for (uint32_t i = 0; i < SAU_PPD_TYPES; ++i) {
			float def = pd_v_default[i];
			prepare_range(&osc->pd[i].main, def, def);
			prepare_range(&osc->pd[i].freq, 1.0, 0.0);
			prepare_range(&osc->pd[i].offset, 0.0, 0.0);
		}
		prepare_range(&osc->pm_a, 0.0, 0.0);
		osc->pmods = osc->fpmods = &blank_idarr;
	}
	GenBase *gen = &n->gen;
	prepare_range(&gen->amp, 1.0, 0.0);
	prepare_range(&gen->pan, 0.0, 0.0);
	prepare_range(&gen->freq, SAU_PDEF_FREQ, 0.0);
	gen->type = gd->ref.gen_type;
}

static void update_ids(AnyGen *restrict n,
		const sauProgramIDs *restrict ids) {
#define CASES_VALR(ID, FIELD) \
	case SAU_MOD_N_##ID:     FIELD.mods1		= ids->a; break; \
	case SAU_MOD_N_##ID##2:  FIELD.mods2 		= ids->a; break; \
	case SAU_MOD_N_##ID##_r: FIELD.r_mods		= ids->a; break; \
	case SAU_MOD_N_##ID##_e: FIELD.e_mods		= ids->a; break; \
	case SAU_MOD_N_##ID##_a: FIELD.mods_add		= ids->a; break; \
/**/
#define CASES_PDMODS(ID, FIELD) \
	CASES_VALR(    ID,       FIELD.main) \
	CASES_VALR(    ID##f,    FIELD.freq) \
	CASES_VALR(    ID##p,    FIELD.offset) \
/**/
	switch (ids->use) {
	case SAU_MOD_N_carr:     break;
	CASES_VALR(    c_am,     n->gen.pan)
	CASES_VALR(    a_am,     n->gen.amp)
	CASES_VALR(    f_fm,     n->gen.freq)
	case SAU_MOD_N_p_pm:     n->osc.pmods    	= ids->a; break;
	case SAU_MOD_N_pf_pm:    n->osc.fpmods   	= ids->a; break;
	CASES_VALR(    pa_pm,    n->osc.pm_a)
	CASES_PDMODS(  pd_c,     n->osc.pd[SAU_PPD_C])
	CASES_PDMODS(  pd_d,     n->osc.pd[SAU_PPD_D])
	CASES_PDMODS(  pd_h,     n->osc.pd[SAU_PPD_H])
	CASES_PDMODS(  pd_x,     n->osc.pd[SAU_PPD_X])
	CASES_PDMODS(  pd_y,     n->osc.pd[SAU_PPD_Y])
	}
}

/*
 * Update range sweep lines.
 */
static sauNoinline void
update_range(struct ParWithRangeMod *restrict rm,
		sauRange *restrict r, uint32_t srate) {
	if (!r)
		return;
	sauLine_copy(&rm->a, &r->a, srate);
	sauLine_copy(&rm->b, &r->b, srate);
	sauLine_copy(&rm->e, &r->e, srate);
	sauEnvGen_set_par(&rm->env, &r->env, srate);
}

/*
 * Update a generator node with new data from event.
 */
static void update_gen(sauGenerator *restrict o,
		AnyGen *restrict n,
		const sauParseGenData *restrict gd) {
	if (gd->copy_from_id != SAU_POBJ_NO_ID)
		*n = o->gens[gd->copy_from_id];
	else if (gd->ref.is_new)
		prepare_gen(o, n, gd);
	uint32_t params = gd->params;
	for (uint32_t i = 0; i < gd->mods_count; ++i)
		update_ids(n, &gd->mods_idarr[i]);
	switch (gd->ref.gen_type) {
	case SAU_PGEN_N_amp: break;
	case SAU_PGEN_N_noise: {
		NoiseGNode *ng = &n->ng;
		if (params & SAU_PGENP_MODE)
			sauNoiseG_set_noise(&ng->noiseg, gd->mode.main);
		if (params & SAU_PGENP_SEED)
			sauNoiseG_set_seed(&ng->noiseg, gd->seed);
		break; }
	case SAU_PGEN_N_wave: {
		WOscNode *wo = &n->wo;
		if (params & SAU_PGENP_MODE)
			sauWOsc_set_opt(&wo->wosc, gd->mode.woo);
		if (params & SAU_PGENP_PHASE)
			sauWOsc_set_phase(&wo->wosc, gd->phase);
		goto OSC_COMMON; }
	case SAU_PGEN_N_raseg: {
		RasGNode *rg = &n->rg;
		if (params & SAU_PGENP_MODE)
			sauRasG_set_opt(&rg->rasg, gd->mode.ras);
		if (params & SAU_PGENP_PHASE)
			sauRasG_set_phase(&rg->rasg, gd->phase);
		if (params & SAU_PGENP_SEED)
			sauRasG_set_cycle(&rg->rasg, gd->seed);
		goto OSC_COMMON; }
	}
	if (false)
	OSC_COMMON: {
		OscBase *osc = &n->osc;
		if (gd->pd) for (uint32_t i = 0; i < SAU_PPD_TYPES; ++i) {
			update_range(&osc->pd[i].main,
					&gd->pd[i].v, o->srate);
			update_range(&osc->pd[i].freq,
					&gd->pd[i].f, o->srate);
			update_range(&osc->pd[i].offset,
					&gd->pd[i].p, o->srate);
		}
		update_range(&osc->pm_a, gd->pm_a, o->srate);
	}
	GenBase *gen = &n->gen;
	gen->obj_id = gd->ref.obj_id;
	if (params & SAU_PGENP_TIME) {
		const sauTime *src = &gd->time;
		if (src->flags & SAU_TIMEP_IMPLICIT) {
			gen->time = 0;
			gen->flags |= GN_TIME_INF;
		} else {
			gen->time = sau_ms_in_samples(src->v_ms,
					o->srate, NULL);
			gen->note_dur = gen->time;
			gen->flags &= ~GN_TIME_INF;
		}
	}
	update_range(&gen->amp, gd->amp, o->srate);
	update_range(&gen->pan, gd->pan, o->srate);
	update_range(&gen->freq, gd->freq, o->srate);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(sauGenerator *restrict o) {
	const sauParseEvData *pe = o->ev_data;
	if (1) /* more types to be added in the future */ {
		/*
		 * Set state of generator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their generators.
		 */
		VoiceNode *vn = NULL;
		if (pe->vo_id != SAU_PVO_NO_ID)
			vn = &o->voices[pe->vo_id];
		for (size_t i = 0; i < pe->gen_data_count; ++i) {
			const sauParseGenData *gd = pe->gen_data[i];
			AnyGen *n = &o->gens[gd->ref.obj_id];
			update_gen(o, n, gd);
		}
		if (vn) {
			vn->carr_gen_id = pe->carr_obj_id;
			vn->flags |= VN_INIT;
			if (o->voice > pe->vo_id) {
				/* go back to re-activated node */
				o->voice = pe->vo_id;
			}
			set_voice_duration(o, vn);
		}
	}
	prepare_event(o, pe->next);
}

/*
 * Add audio layer from \p in_buf into \p buf scaled with \p amp.
 *
 * Used to generate output for carrier or additive modulator.
 */
static void block_mix_add(GenBase *restrict n,
		float *restrict buf, size_t buf_len,
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
#define MIX(OP, AMP) \
	for (size_t i = 0; i < buf_len; ++i) { \
		buf[i] OP in_buf[i] * (AMP); \
	} \
/**/
	if (layer) {
		if (amp) MIX(+=, amp[i])
		else     MIX(+=, n->amp.a.par.v0)
	} else {
		if (amp) MIX(=, amp[i])
		else     MIX(=, n->amp.a.par.v0)
	}
#undef MIX
}

/*
 * Multiply audio layer from \p in_buf into \p buf,
 * after scaling to a 0.0 to 1.0 range multiplied by
 * the absolute value of \p amp, and with the high and
 * low ends of the range flipped if \p amp is negative.
 *
 * Used to generate output for modulation with value range.
 */
static void block_mix_mul_waveenv(GenBase *restrict n,
		float *restrict buf, size_t buf_len,
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
#define MIX(OP, AMP) \
	for (size_t i = 0; i < buf_len; ++i) { \
		float s = in_buf[i]; \
		float s_amp = (AMP) * 0.5f; \
		s = (s * s_amp) + fabsf(s_amp); \
		buf[i] OP s; \
	} \
/**/
	if (layer) {
		if (amp) MIX(*=, amp[i])
		else     MIX(*=, n->amp.a.par.v0)
	} else {
		if (amp) MIX(=, amp[i])
		else     MIX(=, n->amp.a.par.v0)
	}
#undef MIX
}

/*
 * Handle audio layer according to options.
 */
static void block_mix(GenBase *restrict n,
		float *restrict buf, size_t buf_len,
		bool wave_env, bool layer,
		float *restrict in_buf,
		const float *restrict amp) {
	(wave_env ?
	 block_mix_mul_waveenv :
	 block_mix_add)(n, buf, buf_len, layer, in_buf, amp);
}

static struct BlockBufIDs
run_block(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t *restrict mix_len,
		AnyGen *restrict n, uint32_t note_dur,
		float *restrict parent_freq,
		bool wave_env, bool layer);

static float *run_mods(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		const sauProgramIDArr *restrict mods,
		float *restrict freq,
		bool wave_env, bool buf_filled) {
	for (uint32_t i = 0; i < mods->count; ++i) {
		run_block(o, bufs, &(uint32_t){len},
				&o->gens[mods->ids[i]], note_dur,
				freq, wave_env, buf_filled);
		buf_filled = true;
	}
	return buf_filled ? *bufs : NULL;
}

#define NEED_FILL(line, mods) \
	(((line)->par.flags & SAU_LINEP_GOAL) || (mods)->count > 0)

static float *run_line_plus_mods(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		sauLine *restrict line,
		const sauProgramIDArr *restrict mods,
		float *restrict mulbuf,
		float *restrict freq, bool force_fill) {
	if (!NEED_FILL(line, mods) && !force_fill && !mulbuf) {
		sauLine_skip(line, len);
		return NULL;
	}
	sauLine_run(line, *bufs, len, mulbuf);
	return run_mods(o, bufs, len, note_dur, mods, freq, false, true);
}

static float *run_valrange_mix(Buf *restrict bufs,
		float *restrict a, float aval,
		const float *restrict b, float bval,
		const float *restrict x, uint32_t len) {
	uint32_t i;
	if (a) {
		if (b) for (i = 0; i < len; ++i)
			a[i] += (b[i] - a[i]) * x[i];
		else   for (i = 0; i < len; ++i)
			a[i] += (bval - a[i]) * x[i];
	} else {
		a = bufs[0];
		if (aval != 0.f) {
			if (b) for (i = 0; i < len; ++i)
				a[i] = aval + (b[i] - aval) * x[i];
			else   for (i = 0; i < len; ++i)
				a[i] = aval + (bval - aval) * x[i];
		} else {
			if (b) for (i = 0; i < len; ++i)
				a[i] = b[i] * x[i];
			else   for (i = 0; i < len; ++i)
				a[i] = bval * x[i];
		}
	}
	return a;
}

/*
 * Run lines and modulators as needed for a parameter with them.
 *
 * Uses up to 1 extra buffers beyond the main output buffer for this level;
 * the buffer after the first doesn't count, as it belongs to a
 * next level of generator nesting, and so counts as its first.
 */
static inline float *run_valrange_mods(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		struct ParWithRangeMod *restrict n,
		float *restrict param_mulbuf,
		float *restrict freq,
		bool force_fill) {
	float *par_buf = run_line_plus_mods(o, (bufs+0), len, note_dur,
			&n->a, n->mods1, param_mulbuf, freq,
			force_fill || (n->mods_add->count > 0));
	if (n->r_mods->count > 0) {
		float *par2_buf = run_line_plus_mods(o, (bufs+1), len, note_dur,
				&n->b, n->mods2, param_mulbuf, freq, false);
		float *mod_buf = run_mods(o, (bufs+2), len, note_dur,
				n->r_mods, freq, true, false);
		par_buf = run_valrange_mix((bufs+0), par_buf, n->a.par.v0,
				par2_buf, n->b.par.v0, mod_buf, len);
	} else {
		sauLine_skip(&n->b, len);
		// to keep timing in sync, run mods2 despite discarding result
		run_mods(o, (bufs+1), len, note_dur,
				n->mods2, freq, false, true);
	}
	return par_buf;
}

/*
 * Run envelope and its extra line as needed for a parameter with them.
 *
 * Uses up to 2 extra buffers beyond the main output buffer for this level.
 */
static float *run_valrange_env(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		struct ParWithRangeMod *restrict n,
		float *restrict param_mulbuf,
		float *restrict freq,
		float *restrict par_buf) {
	if (n->env.stage > 0) {
		float *par2_buf = run_line_plus_mods(o, (bufs+1), len, note_dur,
				&n->e, n->e_mods, param_mulbuf, freq, false);
		float *env_buf = bufs[2];
		sauEnvGen_run(&n->env, env_buf, len, note_dur);
		par_buf = run_valrange_mix((bufs+0), par_buf, n->a.par.v0,
				par2_buf, n->e.par.v0, env_buf, len);
	} else {
		sauLine_skip(&n->e, len);
		// to keep timing in sync, run e_mods despite discarding result
		run_mods(o, (bufs+1), len, note_dur,
				n->e_mods, freq, false, true);
	}
	return par_buf;
}

/*
 * Run lines and modulators, with any envelope applied on top,
 * for a parameter with these.
 *
 * Uses up to 2 extra buffers beyond the main output buffer for this level.
 */
static sauNoinline float *
run_valrange_param(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len, uint32_t note_dur,
		struct ParWithRangeMod *restrict n,
		float *restrict param_mulbuf,
		float *restrict reused_freq,
		bool is_freq, bool force_fill) {
	float *freq = (reused_freq ? reused_freq : is_freq ? bufs[0] : NULL);
	float *par_buf = run_valrange_mods(o, bufs, len, note_dur,
			n, param_mulbuf, freq, force_fill);
	par_buf = run_valrange_env(o, bufs, len, note_dur,
			n, param_mulbuf, freq, par_buf);
	return run_mods(o, (bufs+0), len, note_dur,
			n->mods_add, freq, false, !!par_buf);
}

/*
 * Run PM and frequency-scaled PM modulators, filling the main PM input buffer.
 */
static float *run_pm_main_params(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		AnyGen *restrict n,
		float *restrict freq) {
	uint32_t note_dur = n->gen.note_dur;
	bool fpm = n->osc.fpmods->count > 0;
	if (fpm) {
		run_mods(o, bufs, len, note_dur,
				n->osc.fpmods, freq, false, false);
		const float fpm_scale = 1.f / SAU_HUMMID;
		for (uint32_t i = 0; i < len; ++i)
			(*bufs)[i] *= fpm_scale * freq[i];
	}
	return run_mods(o, bufs, len, note_dur,
			n->osc.pmods, freq, false, fpm);
}

/*
 * The AmpNode sub-function for run_block().
 *
 * Needs up to 4 buffers (IDs from 0) for its own node level.
 */
static struct BlockBufIDs
run_block_amp(sauGenerator *restrict o sauMaybeUnused,
		Buf *restrict bufs, uint32_t len,
		AnyGen *restrict n sauMaybeUnused,
		float *restrict parent_freq sauMaybeUnused) {
	// freq #1 (++), tmp #2..3
	/*float *freq =*/ run_valrange_param(o, bufs++, len, n->gen.note_dur,
			&n->gen.freq, parent_freq, NULL, true, true);
	float *out_buf = *(bufs++); // #2
	for (uint32_t i = 0; i < len; ++i) out_buf[i] = 1.f;
	bufs++; // amp #3 (++), tmp #4..5 (reserved highest ID returned)
	return (struct BlockBufIDs){.out_id = 2, .freq_id = 1, .amp_id = 3};
}

/*
 * The NoiseGNode sub-function for run_block().
 *
 * Needs up to 4 buffers (IDs from 0) for its own node level.
 */
static struct BlockBufIDs
run_block_noiseg(sauGenerator *restrict o sauMaybeUnused,
		Buf *restrict bufs, uint32_t len,
		AnyGen *restrict n,
		float *restrict parent_freq sauMaybeUnused) {
	// freq #1 (++), tmp #2..3
	/*float *freq =*/ run_valrange_param(o, bufs++, len, n->gen.note_dur,
			&n->gen.freq, parent_freq, NULL, true, true);
	float *out_buf = *(bufs++); // #2
	sauNoiseG_run(&n->ng.noiseg, out_buf, len);
	bufs++; // amp #3 (++), tmp #4..5 (reserved highest ID returned)
	return (struct BlockBufIDs){.out_id = 2, .freq_id = 1, .amp_id = 3};
}

/*
 * The WOscNode sub-function for run_block().
 *
 * Needs up to 9 buffers (IDs from 0) for its own node level.
 */
static struct BlockBufIDs
run_block_wosc(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		AnyGen *restrict n,
		float *restrict parent_freq) {
	// freq #1 (++), tmp #2..3
	float *freq = run_valrange_param(o, bufs++, len, n->gen.note_dur,
			&n->gen.freq, parent_freq, NULL, true, true);
	void *cycle_buf = *(bufs++); // cycle #2 (++)
	void *main_buf = *(bufs++); // phase #3 (++) later reused for output
	float *pm_buf = run_pm_main_params(o, bufs, len, n, freq); // #4
	/*if (!n->gen.cycle_used)*/ cycle_buf = NULL;
	sauPhasor_fill(&n->wo.wosc.phasor, cycle_buf, main_buf, len,
			freq, pm_buf); // #2 and #3 <- #4
	for (unsigned i = 0; i < SAU_PPD_TYPES; ++i) {
		struct ParPDSet *pd = &n->osc.pd[i];
		const float nop_value = pd_v_default[i];
		float *pd_f = run_valrange_param(o, bufs,
				len, n->gen.note_dur, &pd->freq,
				NULL, freq, false, false); // #3 <- #4..6
		float *pd_p = run_valrange_param(o, bufs+1,
				len, n->gen.note_dur, &pd->offset,
				NULL, freq, false, false); // #4 <- #5..7
		bool force_use = pd->main.a.par.v0 != nop_value ||
			(sau_pd_f_is_fmul(i) &&
			 (pd_f || pd->freq.a.par.v0 != 1.f));
		if (run_valrange_param(o, bufs+2, len, n->gen.note_dur,
					&pd->main, NULL, freq,
					false, force_use)) {
			// #2 and #3 <- #4; #5; #6..8
			sauWOsc_pdist(&n->wo.wosc, i,
					main_buf, cycle_buf, len, bufs[2],
					pd_f, pd->freq.a.par.v0,
					pd_p, pd->offset.a.par.v0);
		}
	}
	bufs++; // amp #4 (++), tmp #5..6 (reserved highest ID returned)
	if (run_valrange_param(o, bufs, len, n->gen.note_dur,
				&n->osc.pm_a, NULL, freq, false,
				n->osc.pm_a.a.par.v0 != 0.f)) {
		sauWOsc_run_selfmod(&n->wo.wosc, main_buf, len,
				bufs[0]); // #3 <- #2; #5, tmp #6..7
	} else {
		sauWOsc_run(&n->wo.wosc, main_buf, len);
	}
	return (struct BlockBufIDs){.out_id = 3, .freq_id = 1, .amp_id = 4};
}

/*
 * The RasGNode sub-function for run_block().
 *
 * Needs up to 9 buffers (IDs from 0) for its own node level.
 */
static struct BlockBufIDs
run_block_rasg(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t len,
		AnyGen *restrict n,
		float *restrict parent_freq) {
	// freq #1 (++), tmp #2..3
	float *freq = run_valrange_param(o, bufs++, len, n->gen.note_dur,
			&n->gen.freq, parent_freq, NULL, true, true);
	void *cycle_buf = *(bufs++); // cycle #2 (++)
	void *main_buf = *(bufs++);  // phase #3 (++) later reused for output
	float *pm_buf = run_pm_main_params(o, bufs, len, n, freq); // #4
	sauPhasor_fill(&n->rg.rasg.phasor, cycle_buf, main_buf, len,
			freq, pm_buf); // #2 and #3 <- #4
	for (unsigned i = 0; i < SAU_PPD_TYPES; ++i) {
		struct ParPDSet *pd = &n->osc.pd[i];
		const float nop_value = pd_v_default[i];
		float *pd_f = run_valrange_param(o, bufs,
				len, n->gen.note_dur, &pd->freq,
				NULL, freq, false, false); // #3 <- #4..6
		float *pd_p = run_valrange_param(o, bufs+1,
				len, n->gen.note_dur, &pd->offset,
				NULL, freq, false, false); // #4 <- #5..7
		bool force_use = pd->main.a.par.v0 != nop_value ||
			(sau_pd_f_is_fmul(i) &&
			 (pd_f || pd->freq.a.par.v0 != 1.f));
		if (run_valrange_param(o, bufs+2, len, n->gen.note_dur,
					&pd->main, NULL, freq,
					false, force_use)) {
			// #2 and #3 <- #4; #5; #6..8
			sauRasG_pdist(&n->rg.rasg, i,
					main_buf, cycle_buf, len, bufs[2],
					pd_f, pd->freq.a.par.v0,
					pd_p, pd->offset.a.par.v0);
		}
	}
	bufs++; // amp #4 (++), tmp #5..6 (reserved highest ID returned)
	if (run_valrange_param(o, bufs, len, n->gen.note_dur,
				&n->osc.pm_a, NULL, freq, false,
				n->osc.pm_a.a.par.v0 != 0.f)) {
		sauRasG_run_selfmod(&n->rg.rasg, len, main_buf, cycle_buf,
				bufs[0]); // #3 <- #2; #5, tmp #6..7
	} else {
		sauRasG_run(&n->rg.rasg, len, main_buf, bufs[0], bufs[1],
				cycle_buf); // #3 <- #2; tmp #5..6
	}
	return (struct BlockBufIDs){.out_id = 3, .freq_id = 1, .amp_id = 4};
}

/*
 * Generate up to \p mix_len samples for an generator node,
 * the remainder (if any) zero-filled when \p layer false.
 * The filled length is set to \p mix_len.
 *
 * Recursively visits the subnodes of the generator node,
 * if any. The first buffer will be used for the output.
 *
 * \return buffer ID assignments for reuse
 */
static struct BlockBufIDs
run_block(sauGenerator *restrict o,
		Buf *restrict bufs, uint32_t *restrict mix_len,
		AnyGen *restrict n, uint32_t note_dur,
		float *restrict parent_freq,
		bool wave_env, bool layer) {
	GenBase *gen = &n->gen;
	float *out_buf = *(bufs++); // #0 reserved for final output
	struct BlockBufIDs buf_ids = {0};
	uint32_t len = *mix_len;
	if (gen->flags & GN_TIME_INF) n->gen.note_dur = note_dur;
	note_dur = n->gen.note_dur;
	/*
	 * Guard against circular references.
	 */
	if ((gen->flags & GN_VISITED) != 0) {
		sau_nzerof(out_buf, len);
		return buf_ids;
	}
	gen->flags |= GN_VISITED;
	/*
	 * Limit length to time duration of generator.
	 */
	uint32_t skip_len = 0;
	if (gen->time < len && !(gen->flags & GN_TIME_INF)) {
		skip_len = len - gen->time;
		len = *mix_len = gen->time;
	}
	/*
	 * Use sub-function.
	 *
	 * The \a amp_id it returns must be above other IDs returned,
	 * so that the other buffers returned remain unclobbered.
	 */
	switch (gen->type) {
	case SAU_PGEN_N_amp:
		buf_ids = run_block_amp(o, bufs, len, n, parent_freq);
		break;
	case SAU_PGEN_N_noise:
		buf_ids = run_block_noiseg(o, bufs, len, n, parent_freq);
		break;
	case SAU_PGEN_N_wave:
		buf_ids = run_block_wosc(o, bufs, len, n, parent_freq);
		break;
	case SAU_PGEN_N_raseg:
		buf_ids = run_block_rasg(o, bufs, len, n, parent_freq);
		break;
	}
	float *gen_buf = bufs[buf_ids.out_id - 1];
	float *freq_buf = buf_ids.freq_id ? bufs[buf_ids.freq_id - 1] : NULL;
	float *amp_buf = run_valrange_param(o, (bufs + buf_ids.amp_id - 1),
				len, note_dur,
				&n->gen.amp, NULL, freq_buf, false, false);
	block_mix(&n->gen, out_buf, len, wave_env, layer, gen_buf, amp_buf);
	buf_ids.out_id = 0;
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (!(gen->flags & GN_TIME_INF)) {
		if (!layer) sau_nzerof(out_buf+len, skip_len);
		gen->time -= len;
	}
	gen->flags &= ~GN_VISITED;
	return buf_ids;
}

/*
 * Clear the mix buffers. To be called before adding voice outputs.
 */
static void mix_clear(sauGenerator *restrict o) {
	if (o->gen_mix_add_max == 0)
		return;
	sau_nzerof(o->bufs[0 - MIX_BUFS], o->gen_mix_add_max);
	sau_nzerof(o->bufs[1 - MIX_BUFS], o->gen_mix_add_max);
	o->gen_mix_add_max = 0;
}

/*
 * Add output for voice node \p vn into the mix buffers
 * (0 = left, 1 = right) from the first generator buffer.
 *
 * Dynamic panning will, for nested modulators, pass frequency
 * retrieved from the carrier generator. Buffers above are used
 * as temporary storage.
 */
static void mix_add(sauGenerator *restrict o,
		AnyGen *restrict n, struct BlockBufIDs buf_ids,
		uint32_t len) {
	float *s_buf = o->bufs[0];
	Buf *in_bufs = o->bufs + buf_ids.freq_id;
	float *freq_buf = buf_ids.freq_id ? in_bufs[0] : NULL;
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
	if (run_valrange_param(o, (in_bufs + 1), len, n->gen.note_dur,
				&n->gen.pan, NULL, freq_buf, false,
				n->gen.pan.a.par.v0 != 0.f)) {
		float *pan_buf = *(in_bufs + 1);
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * pan_buf[i];
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			mix_l[i] += s;
			mix_r[i] += s;
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
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
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
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
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
	AnyGen *n = &o->gens[vn->carr_gen_id];
	uint32_t time = vn->duration;
	struct BlockBufIDs buf_ids = {0};
	if (len > BUF_LEN) len = BUF_LEN;
	if (len > time) len = time;
	if (n->gen.time > 0)
		buf_ids = run_block(o, o->bufs, &len, n, 0,
				NULL, false, false);
	if (len > 0)
		mix_add(o, n, buf_ids, len);
	vn->duration -= len;
	return len;
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
		sau_nzero(buf, stereo ? len * 2 : len);
	}
PROCESS:
	skip_len = 0;
	while (o->event < o->ev_count) {
		if (o->ev_pos < o->ev_wait) {
			/*
			 * Limit voice running len to waittime.
			 *
			 * Split processing into two blocks when needed to
			 * ensure event handling runs before voices.
			 */
			uint32_t waittime = o->ev_wait - o->ev_pos;
			if (waittime < len) {
				skip_len = len - waittime;
				len = waittime;
			}
			o->ev_pos += len;
			break;
		}
		handle_event(o);
		++o->event;
		o->ev_pos = 0;
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
