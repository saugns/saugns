/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2026 Joel K. Pettersson
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

#include <sau/render.h>
#include <sau/mempool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generator/array.h"
#include "generator/envel.h"
#include "generator/filter.h"
#include "generator/noise.h"
#include "generator/wosc.h"
#include "generator/rosc.h"

#define BUF_LEN 1024
typedef float Buf[BUF_LEN];

struct LineTime {
	uint32_t pos, end;
};

struct ParWithRangeMod {
	struct LineTime a, b, e;
	sauEnvGen env;
};

/*
 * Extra state to accommodate adjustment for use with multiplier buffer.
 */
struct RangeModDynVal {
	float a_v0, b_v0, e_v0;
};

typedef uint32_t (*RIns_run_fn)(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len);

/*
 * Generator node flags.
 */
enum {
	GN_ROOT_GEN   = 1U<<0, // genereator corresponds to voice output
	GN_TIME_INF   = 1U<<1, // used for SAU_TIMEP_IMPLICIT
	GN_NEW_FREQ_A = 1U<<2, // need to update \a freq_dyn.a_v0
	GN_NEW_FREQ_B = 1U<<3, // need to update \a freq_dyn.b_v0
	GN_NEW_FREQ_E = 1U<<4, // need to update \a freq_dyn.e_v0
};

enum {
	MAIN_FILT = 0,
	GEN_FILTERS
};

/*
 * Generator filter flag values.
 */
#define GN_FILT(i, TYPE) (1U<<((i)*2 + GN_FILT_##TYPE))
#define GN_FILT_LPF 0
#define GN_FILT_HPF 1

typedef struct GenBase {
	uint32_t time;
	uint32_t note_dur; /* time without countdown, from here or carrier */
	uint32_t obj_id;
	uint8_t type;
	uint8_t flags;
	uint8_t pan_law;
	uint8_t filt; // flags from GN_FILT()
	struct RangeModDynVal freq_dyn; // parameter changed by some mulbuf[0]
	struct ParWithRangeMod valr[SAU_PVALR_TYPES];
	struct FilterCoeff main_lpf_c[GEN_FILTERS], main_hpf_c[GEN_FILTERS];
	struct Filter main_lpf[GEN_FILTERS], main_hpf[GEN_FILTERS];
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
} OscBase;

typedef struct WOscNode {
	OscBase osc;
	sauWOsc wosc;
} WOscNode;

typedef struct ROscNode {
	OscBase osc;
	sauROsc rosc;
} ROscNode;

typedef union AnyGen {
	GenBase gen; // generator base type
	AmpNode ag;
	NoiseGNode ng;
	OscBase osc; // oscillator base type
	WOscNode wo;
	ROscNode ro;
} AnyGen;

/*
 * Data for generator chain/block nesting, to keep apart scope levels.
 */
struct GenBlock {
	uint32_t run_len;  // length for buffer use, limiting for gen time
	uint32_t skip_len; // the number of skipped samples after limiting
	AnyGen *n;
};

struct sauGenerator {
	uint32_t srate;
	bool is_run_out_clear : 1;
	bool has_mix_lpf      : 1;
	bool has_mix_hpf      : 1;
	uint16_t gen_mix_add_max;
	Buf *bufs;
	size_t event, ev_count;
	const sauParseEvData *ev_data;
	uint32_t ev_pos, ev_wait;
	int ev_time_carry;
	uint32_t cur_vo_dur; // longest current remaining duration among voices
	uint32_t tail_len;   // length added by the final post-filtering if any
	uint32_t ins_i, ins_count;
	const sauRIns *ins;
	struct GenBlock *block_stack, *cur_block;
	float amp_scale;
	uint32_t gen_count;
	AnyGen *gens;
	struct Filter mix_l_lpf, mix_r_lpf;
	struct Filter mix_l_hpf, mix_r_hpf;
	struct FilterCoeff mix_lpf_c;
	struct FilterCoeff mix_hpf_c;
	sauMempool *mem;
};

#define MIX_BUFS 2

static bool alloc_for_program(sauGenerator *restrict o,
		const sauParse *restrict prg) {
	size_t i;
	i = prg->sbuf_count;
	if (!(o->bufs = calloc(i + MIX_BUFS, sizeof(Buf)))) goto ERROR;
	o->bufs += MIX_BUFS; // place final channel mix buffers below 0
	o->ev_count = prg->ev_count;
	// stack for generator/timing block nesting (1 deep with no nesting)
	i = prg->gen_nest_depth + 1;
	o->block_stack = sau_mpalloc(o->mem, i * sizeof(struct GenBlock));
	if (!o->block_stack) goto ERROR;
	if ((i = prg->gen_count) > 0) {
		o->gens = sau_mpalloc(o->mem, i * sizeof(AnyGen));
		if (!o->gens) goto ERROR;
		o->gen_count = i;
	}
	return true;
ERROR:
	return false;
}

static inline void prepare_event(sauGenerator *restrict o,
		sauParseEvData *restrict prg_e) {
	o->ev_data = prg_e;
	if (!prg_e)
		return;
	/*
	 * The event timeline needs carry to ensure event node timing doesn't
	 * run short (with more nodes, more values), compared to other nodes.
	 */
	o->ev_wait = sau_ms_in_samples(prg_e->wait_ms,
			o->srate, &o->ev_time_carry);
}

static bool convert_program(sauGenerator *restrict o,
		const sauParse *restrict prg, uint32_t srate) {
	if (!alloc_for_program(o, prg))
		return false;
	o->srate = srate;
	o->amp_scale = 1.0;
	if (prg->is_ampmult_set) o->amp_scale *= prg->sopt.ampmult;
	if (prg->is_amp_autoscaled) o->amp_scale /= prg->vo_count;
	if ((o->has_mix_lpf = prg->sopt.mix_filt.l_v > 0)) {
		uint32_t len = sau_set_filt_1p(&o->mix_lpf_c,
				prg->sopt.mix_filt.l_v, srate);
		if (o->tail_len < len) o->tail_len = len;
	}
	if ((o->has_mix_hpf = prg->sopt.mix_filt.h_v > 0)) {
		uint32_t len = sau_set_filt_1p(&o->mix_hpf_c,
				prg->sopt.mix_filt.h_v, srate);
		if (o->tail_len < len) o->tail_len = len;
	}
	uint32_t max_tail = sau_ms_in_samples(100, srate, NULL);
	// frequency can go low, don't lengthen audio by ridiculous lengths
	if (o->tail_len > max_tail) o->tail_len = max_tail;
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
 * Initialize range.
 *
 * The \p v0 value is a fallback which may differ from script defaults.
 */
static void
prepare_range(struct ParWithRangeMod *restrict rm) {
	sau_init_EnvGen(&rm->env);
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
		break; }
	case SAU_PGEN_N_rals: {
		ROscNode *rg = &n->ro;
		sau_init_ROsc(&rg->rosc, o->srate);
		break; }
	}
	GenBase *gen = &n->gen;
	for (uint32_t i = 0; i < SAU_PVALR_TYPES; ++i)
		prepare_range(&gen->valr[i]);
	gen->type = gd->ref.gen_type;
	gen->flags |= GN_NEW_FREQ_A | GN_NEW_FREQ_B | GN_NEW_FREQ_E;
	gen->pan_law = SAU_PAN_DEFAULT;
}

/*
 * Update line state.
 */
static void
update_line(struct LineTime *restrict o,
		const sauLine *restrict src, uint32_t srate) {
	o->pos = sau_ms_in_samples(src->pos, srate, NULL);
	o->end = sau_ms_in_samples(src->end, srate, NULL);
}

/*
 * Update range sweep lines.
 */
static void
update_range(struct ParWithRangeMod *restrict rm,
		sauRange *restrict r, uint32_t srate) {
	if (!r)
		return;
	update_line(&rm->a, &r->a, srate);
	update_line(&rm->b, &r->b, srate);
	update_line(&rm->e, &r->e, srate);
	sauEnvGen_set_par(&rm->env, &r->env, srate);
}

/*
 * Update filter coefficents.
 */
static void
update_filt(AnyGen *restrict n, unsigned filter_i,
		const sauParseGenData *restrict gd, uint32_t srate) {
	sauFiltPar *par = gd->main_filt;
	if (!par)
		return;
	if (par->flags & SAU_FILTP_LPF) {
		unsigned flag = GN_FILT(filter_i, LPF);
		struct Filter *s = &n->gen.main_lpf[filter_i];
		struct FilterCoeff *c = &n->gen.main_lpf_c[filter_i];
		if (par->l_v > 0.f) {
			n->gen.filt |= flag;
			sau_set_filt_1p(c, par->l_v, srate);
		} else {
			n->gen.filt &= ~flag;
		}
		s->t[0] = s->t[1] = 0.f; // reset, prevent burst
	}
	if (par->flags & SAU_FILTP_HPF) {
		unsigned flag = GN_FILT(filter_i, HPF);
		struct Filter *s = &n->gen.main_hpf[filter_i];
		struct FilterCoeff *c = &n->gen.main_hpf_c[filter_i];
		if (par->h_v > 0.f) {
			n->gen.filt |= flag;
			sau_set_filt_1p(c, par->h_v, srate);
		} else {
			n->gen.filt &= ~flag;
		}
		s->t[0] = s->t[1] = 0.f; // reset, prevent burst
	}
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
	sauRange *const *valr = gd->valr ? (*gd->valr) : NULL;
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
		break; }
	case SAU_PGEN_N_rals: {
		ROscNode *rg = &n->ro;
		if (params & SAU_PGENP_MODE)
			sauROsc_set_opt(&rg->rosc, gd->mode.ras);
		if (params & SAU_PGENP_PHASE)
			sauROsc_set_phase(&rg->rosc, gd->phase);
		if (params & SAU_PGENP_SEED)
			sauROsc_set_cycle(&rg->rosc, gd->seed);
		break; }
	}
	GenBase *gen = &n->gen;
	gen->obj_id = gd->ref.obj_id;
	if (!gd->ref.is_nested)
		gen->flags |= GN_ROOT_GEN;
	else
		gen->flags &= ~GN_ROOT_GEN;
	if (params & SAU_PGENP_TIME) {
		const sauTime *src = &gd->time;
		bool allow_inf_time = !(gen->flags & GN_ROOT_GEN);
		if (allow_inf_time && src->flags & SAU_TIMEP_IMPLICIT) {
			gen->time = 0;
			gen->flags |= GN_TIME_INF;
		} else {
			gen->time = sau_ms_in_samples(src->v_ms,
					o->srate, NULL);
			gen->note_dur = gen->time;
			gen->flags &= ~GN_TIME_INF;
		}
	}
	if (valr) {
		for (uint32_t i = 0; i < SAU_PVALR_TYPES; ++i)
			update_range(&gen->valr[i], valr[i], o->srate);
		sauRange *r_pan = valr[SAU_PVALR_PAN];
		if (r_pan && r_pan->a.user_flags)
			gen->pan_law = r_pan->a.user_flags;
		sauRange *r_freq = valr[SAU_PVALR_FREQ];
		if (r_freq && r_freq->a.flags & SAU_LINEP_STATE)
			gen->flags |= GN_NEW_FREQ_A;
		if (r_freq && r_freq->b.flags & SAU_LINEP_STATE)
			gen->flags |= GN_NEW_FREQ_B;
		if (r_freq && r_freq->e.flags & SAU_LINEP_STATE)
			gen->flags |= GN_NEW_FREQ_E;
	}
	update_filt(n, MAIN_FILT, gd, o->srate);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(sauGenerator *restrict o) {
	const sauParseEvData *pe = o->ev_data;
	/*
	 * Set state of generators.
	 */
	for (size_t i = 0; i < pe->gen_data_count; ++i) {
		const sauParseGenData *gd = pe->gen_data[i];
		AnyGen *n = &o->gens[gd->ref.obj_id];
		update_gen(o, n, gd);
	}
	o->ins_count = pe->ins_count;
	o->ins = pe->ins;
	prepare_event(o, pe->next);
}

// provided separately as LPF can be used even when HPF cannot
static inline void block_mix_lpf(float *restrict buf, size_t len,
		AnyGen *restrict n, unsigned filter_i) {
	if (n->gen.filt & GN_FILT(filter_i, LPF))
		sau_arr_lpf_1p(buf, len, &n->gen.main_lpf[filter_i],
				&n->gen.main_lpf_c[filter_i]);
}

static inline void block_mix_hpf(float *restrict buf, size_t len,
		AnyGen *restrict n, unsigned filter_i) {
	if (n->gen.filt & GN_FILT(filter_i, HPF))
		sau_arr_hpf_1p(buf, len, &n->gen.main_hpf[filter_i],
				&n->gen.main_hpf_c[filter_i]);
}

/*
 * Apply main filter(s) to audio buffer.
 */
static void block_mix_filter(float *restrict buf, size_t len,
		AnyGen *restrict n, unsigned filter_i) {
	block_mix_lpf(buf, len, n, filter_i);
	block_mix_hpf(buf, len, n, filter_i);
}

/*
 * Add audio layer from \p in_buf into \p buf scaled with \p amp.
 *
 * Used to generate output for carrier or additive modulator.
 */
static void block_mix_add(float *restrict buf, size_t len,
		AnyGen *restrict n, float *restrict in_buf,
		float *restrict amp, float amp_v0) {
	if (amp)
		sau_nmulnf(in_buf, len, amp);
	else if (amp_v0 != 1.f)
		sau_nmulf(in_buf, len, amp_v0);
	block_mix_filter(in_buf, len, n, MAIN_FILT);
	if (buf != in_buf) sau_naddnf(buf, len, in_buf);
}

/*
 * Multiply audio layer from \p in_buf into \p buf,
 * after scaling to a 0.0 to 1.0 range multiplied by
 * the absolute value of \p amp, and with the high and
 * low ends of the range flipped if \p amp is negative.
 *
 * Used to generate output for modulation with value range.
 */
static void block_mix_mul_waveenv(float *restrict buf, size_t len,
		AnyGen *restrict n, float *restrict in_buf,
		float *restrict amp, float amp_v0) {
#define MIX(X, AMP) \
	for (size_t i = 0; i < len; ++i) { \
		float s_amp = AMP * 0.5f; \
		X = X * s_amp + fabsf(s_amp); \
	} \
/**/
	// run HPF on input only, to keep it from messing up amp and result;
	// amp is normally filled with DC, and the result is made unipolar
	block_mix_hpf(in_buf, len, n, MAIN_FILT);
	if (amp)
		MIX(in_buf[i], amp[i])
	else if (amp_v0 != 1.f)
		MIX(in_buf[i], amp_v0)
	else
		MIX(in_buf[i], 1)
	block_mix_lpf(in_buf, len, n, MAIN_FILT);
	if (buf != in_buf) sau_nmulnf(buf, len, in_buf);
#undef MIX
}

/*
 * Handle audio layer according to options.
 */
static void block_mix(float *restrict buf, size_t len,
		AnyGen *restrict n, bool wave_env, float *restrict in_buf,
		float *restrict amp, float amp_v0) {
	(wave_env ?
	 block_mix_mul_waveenv :
	 block_mix_add)(buf, len, n, in_buf, amp, amp_v0);
}

static float *mix_valrange(bool is_a_filled,
		float *restrict a, float aval,
		const float *restrict b, float bval,
		const float *restrict x, uint32_t len) {
	uint32_t i;
	if (is_a_filled) {
		if (b) for (i = 0; i < len; ++i)
			a[i] += (b[i] - a[i]) * x[i];
		else if (bval) for (i = 0; i < len; ++i)
			a[i] += (bval - a[i]) * x[i];
		else   for (i = 0; i < len; ++i)
			a[i] -= a[i] * x[i];
	} else if (aval) {
		if (b) for (i = 0; i < len; ++i)
			a[i] = aval + (b[i] - aval) * x[i];
		else if (bval) for (i = 0; i < len; ++i)
			a[i] = aval + (bval - aval) * x[i];
		else   for (i = 0; i < len; ++i)
			a[i] = aval - aval * x[i];
	} else {
		if (b) for (i = 0; i < len; ++i)
			a[i] = b[i] * x[i];
		else if (bval) for (i = 0; i < len; ++i)
			a[i] = bval * x[i];
		else   for (i = 0; i < len; ++i)
			a[i] = 0.f;
	}
	return a;
}

/*
 * Clear the mix buffers. To be called before adding voice outputs.
 */
static void mix_clear(sauGenerator *restrict o) {
	if (o->gen_mix_add_max == 0)
		return;
	sau_nsetf(o->bufs[0 - MIX_BUFS], o->gen_mix_add_max, 0.0);
	sau_nsetf(o->bufs[1 - MIX_BUFS], o->gen_mix_add_max, 0.0);
	o->gen_mix_add_max = 0;
}

/*
 * Linear panning version of mix_add().
 */
static void mix_add_pan_lin(sauGenerator *restrict o,
		float *restrict s_buf,
		const float *restrict pan_buf, float pan_v0,
		uint32_t len) {
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
	float amp_scale = o->amp_scale * 0.5f;
	if (pan_buf) {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			float s_r = s * pan_buf[i];
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else if (pan_v0 != 0.f) {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			float s_r = s * pan_v0;
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			mix_l[i] += s;
			mix_r[i] += s;
		}
	}
}

/*
 * Full-volume panning version of mix_add().
 */
static void mix_add_pan_full(sauGenerator *restrict o,
		float *restrict s_buf,
		const float *restrict pan_buf, float pan_v0,
		uint32_t len) {
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
	float amp_scale = o->amp_scale;
	if (pan_buf) {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			float p = pan_buf[i];
			float s_p = s * p;
			mix_l[i] += s - (p >= 0.f ? s_p : 0.f);
			mix_r[i] += s + (p >= 0.f ? 0.f : s_p);
		}
	} else if (pan_v0 != 0.f) {
		if (pan_v0 > 0.f) for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			float s_r = s * pan_v0;
			mix_l[i] += s - s_r;
			mix_r[i] += s;
		} else for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			float s_l = s * pan_v0;
			mix_l[i] += s;
			mix_r[i] += s + s_l;
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * amp_scale;
			mix_l[i] += s;
			mix_r[i] += s;
		}
	}
}

/*
 * Add output for generator node \p n into the mix buffers
 * (0 = left, 1 = right) from the first generator buffer.
 *
 * Dynamic panning will, for nested modulators, pass frequency
 * retrieved from the carrier generator. Buffers above are used
 * as temporary storage.
 */
static void mix_add(sauGenerator *restrict o,
		AnyGen *restrict n,
		float *restrict s_buf,
		const float *restrict pan_buf, float pan_v0,
		uint32_t len) {
	switch (n->gen.pan_law) {
	case SAU_PAN_LIN:
		mix_add_pan_lin(o, s_buf, pan_buf, pan_v0, len); break;
	case SAU_PAN_FULL:
		mix_add_pan_full(o, s_buf, pan_buf, pan_v0, len); break;
	}
	if (o->gen_mix_add_max < len) o->gen_mix_add_max = len;
}

static void mix_filter(sauGenerator *restrict o, uint32_t len) {
	uint32_t gen_len = o->gen_mix_add_max;
	float *mix_l = o->bufs[0 - MIX_BUFS];
	float *mix_r = o->bufs[1 - MIX_BUFS];
	if ((o->event == o->ev_count) && (gen_len < len)) {
		/*
		 * Extend audio duration to include tail end of filtering.
		 */
		uint32_t mix_len = gen_len + o->tail_len;
		if (mix_len > len) mix_len = len;
		uint32_t skip_len = mix_len - gen_len;
		o->cur_vo_dur += skip_len;
		o->tail_len -= skip_len;
		len = mix_len;
	}
	if (o->has_mix_lpf) {
		sau_arr_lpf_1p(mix_l, len, &o->mix_l_lpf, &o->mix_lpf_c);
		sau_arr_lpf_1p(mix_r, len, &o->mix_r_lpf, &o->mix_lpf_c);
	}
	if (o->has_mix_hpf) {
		sau_arr_hpf_1p(mix_l, len, &o->mix_l_hpf, &o->mix_hpf_c);
		sau_arr_hpf_1p(mix_r, len, &o->mix_r_hpf, &o->mix_hpf_c);
	}
	// extend if needed to make sure no filter output values are missed
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
	o->is_run_out_clear = false;
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
	o->is_run_out_clear = false;
	for (uint32_t i = 0; i < len; ++i) {
		float s_l = mix_l[i];
		float s_r = mix_r[i];
		s_l = sau_fclampf(s_l, -1.f, 1.f);
		s_r = sau_fclampf(s_r, -1.f, 1.f);
		*(*spp)++ += lrintf(s_l * (float) INT16_MAX);
		*(*spp)++ += lrintf(s_r * (float) INT16_MAX);
	}
}

static struct LineTime *
get_line(AnyGen *restrict n, uint8_t par_id, uint8_t sub_id) {
	struct ParWithRangeMod *par = &n->gen.valr[par_id];
	switch (sub_id) {
	default: return NULL; break;
	case SAU_RANGE_A: return &par->a; break;
	case SAU_RANGE_B: return &par->b; break;
	case SAU_RANGE_E: return &par->e; break;
	}
}

static sauEnvGen *
get_env(AnyGen *restrict n, uint8_t par_id) {
	struct ParWithRangeMod *par = &n->gen.valr[par_id];
	return &par->env;
}

/*
 * Called on any blank or bogus opcode instruction.
 */
static uint32_t run_rins_error(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	sau_error("generator", "run_rins_error() called from ins %d [op %d]",
			o->ins_i, ins->op);
	return len;
}

/*
 * Handle time bookkeeping for a series of instructions for a generator.
 * Matched by run_rins_gen_pop_mix() for ending a series.
 */
static uint32_t run_rins_gen_push_jz(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	uint32_t gen_id = ins->a.i;
	AnyGen *n = &o->gens[gen_id];
	GenBase *gen = &n->gen;
	if (gen->time == 0 && !(gen->flags & GN_TIME_INF)) {
		/*
		 * Skip all instructions for this generator.
		 *
		 * If a modulator and mixing mode requires a
		 * valid buffer, zero to length before jump.
		 */
		if (!(gen->flags & GN_ROOT_GEN)) {
			bool layer = ins->mode & SAU_RMIX_LAYER;
			float *out_buf = o->bufs[ins->x];
			if (!layer) sau_nsetf(out_buf, len, 0.0);
		}
		o->ins_i = ins->b.i - 1; // -1 compensates for increment after
		return len;
	}
	/*
	 * Ensure generator has note duration.
	 */
	if (gen->flags & GN_TIME_INF) // case for modulators (nested) only
		gen->note_dur = o->cur_block->n->gen.note_dur;
	/*
	 * Store length and skipped length adjusted for the current generator.
	 * Note cur_block pre-increment; it reaches the first entry with it.
	 */
	uint32_t skip_len = 0;
	if (gen->time > 0) {
		if (len > gen->time) {
			skip_len = len - gen->time;
			len = gen->time;
			gen->time = 0;
		} else {
			if ((gen->flags & GN_ROOT_GEN) &&
			    o->cur_vo_dur < gen->time)
				o->cur_vo_dur = gen->time;
			gen->time -= len;
		}
	}
	*(++o->cur_block) = (struct GenBlock){
		.run_len = len, .skip_len = skip_len, .n = n
	};
	return len;
}

/*
 * Prepare final output from generator. Pops from time stack; ends a series of
 * instructions for a generator.
 */
static uint32_t run_rins_gen_pop_mix(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	bool layer  = ins->mode & SAU_RMIX_LAYER;
	bool mul_we = ins->mode & SAU_RMIX_MUL_WE;
	float *in_buf  = o->bufs[ins->a.i];
	float *amp_buf = !ins->has_b_f ? o->bufs[ins->b.i] : NULL;
	float amp_v0 = ins->b.f; // fallback only
	float *out_buf = o->bufs[ins->x];
	uint32_t skip_len = o->cur_block->skip_len;
	AnyGen *n = o->cur_block->n;
	block_mix(out_buf, len, n, mul_we, in_buf, amp_buf, amp_v0);
	if (n->gen.flags & GN_ROOT_GEN) {
		// handle panning and add result to voice output
		float *pan_buf = !ins->has_c_f ? o->bufs[ins->c.i] : NULL;
		float pan_v0 = ins->c.f; // fallback only
		mix_add(o, n, out_buf, pan_buf, pan_v0, len);
	} else {
		// handle samples skipped due to generator time limit
		if (!layer && skip_len > 0)
			sau_nsetf(out_buf+len, skip_len, 0.0);
	}
	// pop stack, restore len used for instructions after
	--o->cur_block;
	return len + skip_len;
}

static uint32_t run_rins_run_noisegen(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	float *buf = o->bufs[ins->x];
	sauNoiseG_run(&n->ng.noiseg, buf, len);
	return len;
}

/*
 * Final step for random line segments oscillator.
 * Requires input from run_rins_run_osc_phasor().
 */
static uint32_t run_rins_run_ralsosc(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *main_buf   = o->bufs[sau_rins_get_w0(ins->x)];
	void *cycle_buf  = o->bufs[sau_rins_get_w1(ins->x)];
	float *end_a_buf = o->bufs[sau_rins_get_w0(ins->a.i)];
	float *end_b_buf = o->bufs[sau_rins_get_w1(ins->a.i)];
	sauROsc_run(&n->ro.rosc, len, main_buf,
			end_a_buf, end_b_buf, cycle_buf);
	return len;
}

/*
 * Final step for random line segments oscillator, running with self-modulation.
 * Requires input from run_rins_run_osc_phasor() and PM self-modulation input.
 */
static uint32_t run_rins_run_ralsosc_selfmod(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *main_buf   = o->bufs[sau_rins_get_w0(ins->x)];
	void *cycle_buf  = o->bufs[sau_rins_get_w1(ins->x)];
	float *pma_in_buf = o->bufs[ins->a.i];
	sauROsc_run_selfmod(&n->ro.rosc, len, main_buf, cycle_buf, pma_in_buf);
	return len;
}

/*
 * Can be applied to phase and cycle. Use before the final ralsosc instruction.
 */
static uint32_t run_rins_run_ralsosc_pdist(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *phase_buf = o->bufs[sau_rins_get_w0(ins->x)];
	void *cycle_buf = o->bufs[sau_rins_get_w1(ins->x)];
	void *pd_buf = o->bufs[ins->a.i];
	uint8_t pd_fn_id = ins->mode;
	void *pd_f_buf = !ins->has_b_f ? o->bufs[ins->b.i] : NULL;
	void *pd_p_buf = !ins->has_c_f ? o->bufs[ins->c.i] : NULL;
	float pd_f_v0 = ins->b.f; // fallback only
	float pd_p_v0 = ins->c.f; // fallback only
	sauROsc_pdist(&n->ro.rosc, pd_fn_id,
			phase_buf, cycle_buf, len, pd_buf,
			pd_f_buf, pd_f_v0, pd_p_buf, pd_p_v0);
	return len;
}

/*
 * Final step for wave oscillator.
 * Requires input from run_rins_run_osc_phasor().
 */
static uint32_t run_rins_run_waveosc(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *main_buf = o->bufs[ins->x];
	sauWOsc_run(&n->wo.wosc, main_buf, len);
	return len;
}

/*
 * Final step for wave oscillator, running with self-modulation.
 * Requires input from run_rins_run_osc_phasor() and PM self-modulation input.
 */
static uint32_t run_rins_run_waveosc_selfmod(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *main_buf = o->bufs[ins->x];
	float *pma_in_buf = o->bufs[ins->a.i];
	sauWOsc_run_selfmod(&n->wo.wosc, main_buf, len, pma_in_buf);
	return len;
}

/*
 * Can be applied to phase buffer. Use before the final waveosc instruction.
 */
static uint32_t run_rins_run_waveosc_pdist(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *phase_buf = o->bufs[ins->x];
	void *pd_buf = o->bufs[ins->a.i];
	uint8_t pd_fn_id = ins->mode;
	void *pd_f_buf = !ins->has_b_f ? o->bufs[ins->b.i] : NULL;
	void *pd_p_buf = !ins->has_c_f ? o->bufs[ins->c.i] : NULL;
	float pd_f_v0 = ins->b.f; // fallback only
	float pd_p_v0 = ins->c.f; // fallback only
	sauWOsc_pdist(&n->wo.wosc, pd_fn_id,
			phase_buf, NULL, len, pd_buf,
			pd_f_buf, pd_f_v0, pd_p_buf, pd_p_v0);
	return len;
}

static uint32_t run_rins_run_osc_phasor(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	void *phase_buf = o->bufs[sau_rins_get_w0(ins->x)];
	uint32_t cycle_buf_id = sau_rins_get_w1(ins->x);
	void *cycle_buf = cycle_buf_id ? o->bufs[cycle_buf_id] : NULL;
	float *freq_buf = !ins->has_a_f ? o->bufs[ins->a.i] : NULL;
	float freq_v0 = ins->a.f; // fallback only
	float *pm_in_buf = ins->has_b ? o->bufs[ins->b.i] : NULL;
	if (cycle_buf_id)
		sauPhasor_fill(&n->ro.rosc.phasor, cycle_buf, phase_buf, len,
				freq_buf, freq_v0, pm_in_buf);
	else
		sauPhasor_fill(&n->wo.wosc.phasor, cycle_buf, phase_buf, len,
				freq_buf, freq_v0, pm_in_buf);
	return len;
}

static float *
adjust_freq_v0(GenBase *restrict gen, uint8_t sub_id,
		sauLine *restrict line, const float *restrict mulbuf) {
	if (!mulbuf || !(line->flags & SAU_LINEP_GOAL))
		return NULL;
	float *gen_freq_v0 = NULL;
	unsigned mask = 0;
	switch (sub_id) {
	default: return NULL;
	case SAU_RANGE_A:
		 gen_freq_v0 = &gen->freq_dyn.a_v0;
		 mask = GN_NEW_FREQ_A;
		 break;
	case SAU_RANGE_B:
		 gen_freq_v0 = &gen->freq_dyn.b_v0;
		 mask = GN_NEW_FREQ_B;
		 break;
	case SAU_RANGE_E:
		 gen_freq_v0 = &gen->freq_dyn.e_v0;
		 mask = GN_NEW_FREQ_E;
		 break;
	}
	if (gen->flags & mask) {
		gen->flags &= ~mask;
		sauLine_adjust_v0(line, mulbuf[0]);
	} else {
		sauLine_adjust_v0(line, 1.0); // just adjust flags
		line->v0 = *gen_freq_v0;
	}
	return gen_freq_v0;
}

static uint32_t run_rins_run_par_line(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	uint8_t par_id = sau_rins_get_b0(ins->c.i);
	uint8_t sub_id = sau_rins_get_b1(ins->c.i);
	struct LineTime *time = get_line(n, par_id, sub_id);
	sauLine line = {
		.v0 = ins->a.f,
		.vt = ins->b.f,
		.type  = sau_rins_get_b2(ins->c.i),
		.flags = sau_rins_get_b3(ins->c.i),
		.pos = time->pos,
		.end = time->end,
	};
	float *par_buf = o->bufs[sau_rins_get_w0(ins->x)];
	uint32_t mul_buf_id    = sau_rins_get_w1(ins->x);
	float *mul_buf = mul_buf_id ? o->bufs[mul_buf_id] : NULL;
	float *store_val = NULL;
	if (par_id == SAU_PVALR_FREQ)
		store_val = adjust_freq_v0(&n->gen, sub_id, &line, mul_buf);
	sauLine_run(&line, par_buf, len, mul_buf);
	if (store_val) *store_val = line.v0;
	time->pos = line.pos;
	time->end = line.end;
	return len;
}

static uint32_t run_rins_run_par_env(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	AnyGen *n = o->cur_block->n;
	uint8_t par_id = ins->a.i;
	sauEnvGen *env = get_env(n, par_id);
	void *env_buf = o->bufs[ins->x];
	sauEnvGen_run(env, env_buf, len, n->gen.note_dur);
	return len;
}

static uint32_t run_rins_mix_valrange(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	float a_v0 = ins->a.f, b_v0 = ins->b.f;
	float *a = o->bufs[sau_rins_get_w0(ins->x)];
	bool is_a_filled = sau_rins_get_w1(ins->x);
	float *x = o->bufs[sau_rins_get_w0(ins->c.i)];
	uint32_t b_buf_id = sau_rins_get_w1(ins->c.i);
	float *b = b_buf_id ? o->bufs[b_buf_id] : NULL;
	mix_valrange(is_a_filled, a, a_v0, b, b_v0, x, len);
	return len;
}

static uint32_t run_rins_nsetf(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	float *buf = o->bufs[ins->x];
	sau_nsetf(buf, len, ins->a.f);
	return len;
}

static uint32_t run_rins_nmulf(struct sauGenerator *restrict o,
		const sauRIns *restrict ins, uint32_t len) {
	float *x = o->bufs[ins->x];
	if (ins->has_a) {
		float *a = o->bufs[ins->a.i];
		if (ins->has_b_f) sau_nmulnff(x, len, a, ins->b.f);
		else              sau_nmulnf(x, len, a);
	} else {
		sau_nmulf(x, len, ins->b.f);
	}
	return len;
}

#define SAU_RINS__X_CASE(NAME) case SAU_RINS_N_##NAME: return run_rins_##NAME;

/*
 * Get function for instruction number.
 */
static inline RIns_run_fn get_rins_run_fn(unsigned ins) {
	switch (ins) {
	default: return run_rins_error;
	SAU_RINS__ITEMS(SAU_RINS__X_CASE)
	}
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
		o->cur_vo_dur = 0; // reset, is increased during new run
		o->cur_block = o->block_stack-1; // reset gen block scope
		for (o->ins_i = 0; o->ins_i < o->ins_count; ++o->ins_i) {
			const sauRIns *ins = &o->ins[o->ins_i];
			RIns_run_fn run_fn = get_rins_run_fn(ins->op);
			/*
			 * Instructions may reduce \a len, but not lengthen it
			 * beyond the original. This is managed using a stack.
			 */
			len = run_fn(o, ins, len); // may change o->ins_i
		}
		if (o->has_mix_lpf || o->has_mix_hpf)
			mix_filter(o, len);
		uint32_t last_len = o->gen_mix_add_max;
		if (last_len > 0) {
			gen_len += last_len;
			(stereo ?
			 mix_write_stereo :
			 mix_write_mono)(o, &sp, last_len);
		}
	}
	return gen_len;
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
	if (!o->is_run_out_clear) {
		o->is_run_out_clear = true;
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
	 * Check for end of signal.
	 */
	if (o->event == o->ev_count && o->cur_vo_dur == 0) {
		if (out_len) *out_len = gen_len;
		return false;
	}
	/*
	 * Further calls needed to complete signal.
	 */
	if (out_len) *out_len = buf_len;
	return true;
}
