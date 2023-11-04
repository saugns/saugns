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
	sauLine pan;
	const sauProgramIDArr *camods;
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
	uint8_t freq_buf_id; // zero if unavailable
	const sauProgramOpRef *graph;
	uint32_t op_count;
	uint32_t first_ins, ins_count;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	const sauProgramEvent *prg_event;
} EventNode;

/* Macro used for lists which include generator instruction names. */
#define GEN_INS__ITEMS(X) \
	X(error) \
	X(time_jz_sub) \
	X(mix_add) \
	X(mix_mul) \
	X(raparam) \
	X(ari_means) \
	X(phase) \
	X(cycle_phase) \
	X(fill_wosc) \
	X(fill_rasg) \
	//
#define GEN_INS__X_ID(NAME) INS_N_##NAME,
#define GEN_INS__X_NAME(NAME) #NAME,
#define GEN_INS__X_CASE(NAME) case INS_N_##NAME: return gen_ins_##NAME;

/*
 * Generator instructions.
 */
enum {
	GEN_INS__ITEMS(GEN_INS__X_ID)
	GEN_INS_NAMED
};

typedef struct GenIns {
	uint8_t id, mode;
	uint32_t dst;
	int32_t src, ext;
	void *data;
	//uint32_t max_len;
} GenIns;

typedef uint32_t (*GenIns_run_f)(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len);

/*
 * Macros for instruction struct initializers.
 */

#define INS_TIME_JZ_SUB(node, jmp_dst) \
(GenIns){ \
	INS_N_time_jz_sub, 0, (jmp_dst), -1, -1, (node) \
}

#define INS_RAPARAM(out_buf, in_buf, in2_buf, data) \
(GenIns){ \
	INS_N_raparam, 0, (out_buf), (in_buf), (in2_buf), (data) \
}

#define INS_MIX_ADD(out_buf, in_buf, amp_buf, is_first) \
(GenIns){ \
	INS_N_mix_add, !(is_first), (out_buf), (in_buf), (amp_buf), NULL \
}

#define INS_MIX_MUL(out_buf, in_buf, amp_buf, is_first) \
(GenIns){ \
	INS_N_mix_mul, !(is_first), (out_buf), (in_buf), (amp_buf), NULL \
}

#define INS_ARI_MEANS(buf, buf2, mix_buf) \
(GenIns){ \
	INS_N_ari_means, 0, (buf), (buf2), (mix_buf), NULL \
}

#define PHASE_MOD_TYPES(has_pm, has_fpm) \
	((has_pm) | ((has_fpm) << 1))

// uses 1 dst buffer, 1 src buffer, 2 ext buffers as optional inputs
#define INS_PHASE(out, freq, mod_from, node, has_pm, has_fpm) \
(GenIns){ \
	INS_N_phase, PHASE_MOD_TYPES((has_pm), (has_fpm)), \
	(out), (freq), (mod_from), (node) \
}

// uses 2 dst buffers, 1 src buffer, 2 ext buffers as optional inputs
#define INS_CYCLE_PHASE(out, freq, mod_from, node, has_pm, has_fpm) \
(GenIns){ \
	INS_N_cycle_phase, PHASE_MOD_TYPES((has_pm), (has_fpm)), \
	(out), (freq), (mod_from), (node) \
}

// uses 1 dst buffer, 1 src buffer, 2 ext buffers as temporary storage
#define INS_FILL_RASG(out, cycle_buf, tmp_from, node) \
(GenIns){ \
	INS_N_fill_rasg, 0, (out), (cycle_buf), (tmp_from), (node) \
}

// uses 1 dst buffer, 1 src buffer
#define INS_FILL_WOSC(out, phase_buf, node) \
(GenIns){ \
	INS_N_fill_wosc, 0, (out), (phase_buf), -1, (node) \
}

/*
 * Generator flags.
 */
enum {
	GEN_OUT_CLEAR = 1<<0,
	GEN_RUN_RESCHED = 1<<1,
};

struct sauGenerator {
	uint32_t srate;
	uint16_t gen_flags;
	uint16_t gen_mix_add_max;
	Buf *restrict gen_bufs, *restrict mix_bufs;
	GenIns *restrict gen_ins;
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

// maximum number of instructions needed for op nesting depth
#define COUNT_GEN_INS(op_count_max) ((op_count_max) * 8)

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
		i = COUNT_GEN_INS(prg->op_count); // excessive, but safe
		o->gen_ins = sau_mpalloc(o->mem,
				i * prg->vo_count * sizeof(GenIns));
		if (!o->gen_bufs || !o->gen_ins) goto ERROR;
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
	const sauProgramOpRef *root_op;
	if ((root_op = sauProgramOpRef_get_root(vn->graph, vn->op_count))) {
		GenNode *gen = &o->operators[root_op->id].gen;
		if (gen->time > time)
			time = gen->time;
	}
	vn->duration = time;
}

/*
 * Initialize an operator node for use as the given type.
 */
static void prepare_op(sauGenerator *restrict o,
		OperatorNode *restrict n, VoiceNode *restrict vn,
		const sauProgramOpData *restrict od) {
	if (od->use_type == SAU_POP_N_carr) {
		vn->freq_buf_id = 0;
	}
	switch (od->type) {
	case SAU_POPT_WAVE: {
		WOscNode *wo = &n->wo;
		memset(n, 0, sizeof(*wo));
		sau_init_WOsc(&wo->wosc, o->srate);
		if (od->use_type == SAU_POP_N_carr) // must match run_ins_wosc()
			vn->freq_buf_id = 3 - 1;
		goto OSC_COMMON; }
	case SAU_POPT_RAS: {
		RasGNode *rg = &n->rg;
		memset(n, 0, sizeof(*rg));
		sau_init_RasG(&rg->rasg, o->srate);
		sauRasG_set_cycle(&rg->rasg, od->seed);
		if (od->use_type == SAU_POP_N_carr) // must match run_ins_rasg()
			vn->freq_buf_id = 4 - 1;
		goto OSC_COMMON; }
	}
	if (false)
	OSC_COMMON: {
		OscNode *osc = &n->osc;
		osc->freq.mods = osc->freq.r_mods =
		osc->pmods = osc->fpmods = &blank_idarr;
	}
	GenNode *gen = &n->gen;
	gen->amp.mods = gen->amp.r_mods = gen->camods = &blank_idarr;
	gen->type = od->type;
	gen->flags = ON_INIT;
}

/*
 * Update an operator node with new data from event.
 */
static void update_op(sauGenerator *restrict o,
		OperatorNode *restrict n,
		const sauProgramOpData *restrict od) {
	uint32_t params = od->params;
	if (params & SAU_POPP_COPY) {
		const OperatorNode *src_n = &o->operators[od->copy_id];
		*n = *src_n;
	}
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
	if (od->camods) gen->camods = od->camods;
	if (od->amods) gen->amp.mods = od->amods;
	if (od->ramods) gen->amp.r_mods = od->ramods;
	sauLine_copy(&gen->amp.par, od->amp, o->srate);
	sauLine_copy(&gen->amp.r_par, od->amp2, o->srate);
	sauLine_copy(&gen->pan, od->pan, o->srate);
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
				prepare_op(o, n, vn, od);
			update_op(o, n, od);
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
		o->gen_flags |= GEN_RUN_RESCHED;
	}
}

static GenIns *
sched_ins(sauGenerator *restrict o,
		GenIns *restrict ins, uint32_t buf,
		OperatorNode *restrict n,
		int32_t parent_freq,
		bool wave_env, bool layer);

/*
 * Sub-function for a parameter with range modulation.
 *
 * Needs up to 3 buffers and 2 instructions for its own node level.
 */
static GenIns *
sched_param_with_rangemod(sauGenerator *restrict o,
		GenIns *restrict ins, uint32_t buf,
		struct ParWithRangeMod *restrict n,
		int32_t param_mulbuf,
		int32_t reused_freq) {
	uint32_t i;
	int32_t par_buf = buf, r_par_buf = -1;
	int32_t freq = (reused_freq >= 0 ? reused_freq : par_buf);
	if (n->r_mods->count > 0) {
		r_par_buf = par_buf + 1;
		for (i = 0; i < n->r_mods->count; ++i)
			ins = sched_ins(o, ins, par_buf + 2,
					&o->operators[n->r_mods->ids[i]],
					freq, true, i);
	}
	*(ins++) = INS_RAPARAM(par_buf, param_mulbuf, r_par_buf, n);
	if (r_par_buf >= 0) {
		*(ins++) = INS_ARI_MEANS(par_buf, par_buf + 1, par_buf + 2);
	}
	if (n->mods->count > 0) {
		for (i = 0; i < n->mods->count; ++i)
			ins = sched_ins(o, ins, par_buf,
					&o->operators[n->mods->ids[i]],
					freq, false, true);
	}
	return ins;
}

/*
 * The WOscNode sub-function for sched_ins().
 *
 * Needs up to 6 buffers and 7 + 1 instructions for its own node level.
 * (1 instruction for bookkeeping is added in the sched_ins() function.)
 */
static GenIns *
sched_ins_wosc(sauGenerator *restrict o,
		GenIns *restrict ins, uint32_t buf,
		OperatorNode *restrict n,
		int32_t parent_freq,
		bool wave_env, bool layer) {
	uint32_t i;
	int32_t mix_buf = buf++, pm_buf = -1, fpm_buf = -1;
	int32_t phase_buf = buf++;
	int32_t freq = -1, amp = -1;
	int32_t tmp_buf = -1;
	/*
	 * Handle frequency (alternatively ratio) parameter,
	 * including frequency modulation if modulators linked.
	 */
	freq = buf;
	ins = sched_param_with_rangemod(o, ins, buf,
			&n->osc.freq, parent_freq, -1);
	++buf; // freq is passed along to modulators that need it; kept below
	/*
	 * Pre-fill phase buffers.
	 *
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	if (n->osc.pmods->count > 0) {
		for (i = 0; i < n->osc.pmods->count; ++i)
			ins = sched_ins(o, ins, (buf + 0),
					&o->operators[n->osc.pmods->ids[i]],
					freq, false, i);
		pm_buf = buf; // temporary
	}
	if (n->osc.fpmods->count > 0) {
		for (i = 0; i < n->osc.fpmods->count; ++i)
			ins = sched_ins(o, ins, (buf + 1),
					&o->operators[n->osc.fpmods->ids[i]],
					freq, false, i);
		fpm_buf = buf + 1; // temporary
	}
	*(ins++) = INS_PHASE(phase_buf, freq, buf, n,
			pm_buf>=0, fpm_buf>=0);
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	ins = sched_param_with_rangemod(o, ins, buf,
			&n->gen.amp, -1, freq);
	amp = buf++;
	tmp_buf = buf;
	*(ins++) = INS_FILL_WOSC(tmp_buf, phase_buf, n);
	if (wave_env)
		*(ins++) = INS_MIX_MUL(mix_buf, tmp_buf, amp, !layer);
	else
		*(ins++) = INS_MIX_ADD(mix_buf, tmp_buf, amp, !layer);
	return ins;
}

/*
 * The RasGNode sub-function for sched_ins().
 *
 * Needs up to 7 buffers and 7 + 1 instructions for its own node level.
 * (1 instruction for bookkeeping is added in the sched_ins() function.)
 */
static GenIns *
sched_ins_rasg(sauGenerator *restrict o,
		GenIns *restrict ins, uint32_t buf,
		OperatorNode *restrict n,
		int32_t parent_freq,
		bool wave_env, bool layer) {
	uint32_t i;
	int32_t mix_buf = buf++, freq = -1, amp = -1;
	int32_t cycle_buf = buf++, rasg_buf = buf++;
	int32_t pm_buf = -1, fpm_buf = -1;
	int32_t tmp_buf = -1;
	/*
	 * Handle frequency (alternatively ratio) parameter,
	 * including frequency modulation if modulators linked.
	 */
	freq = buf;
	ins = sched_param_with_rangemod(o, ins, buf,
			&n->osc.freq, parent_freq, -1);
	++buf; // freq is passed along to modulators that need it; kept below
	/*
	 * Pre-fill cycle & phase buffers.
	 *
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	if (n->osc.pmods->count > 0) {
		for (i = 0; i < n->osc.pmods->count; ++i)
			ins = sched_ins(o, ins, (buf + 0),
					&o->operators[n->osc.pmods->ids[i]],
					freq, false, i);
		pm_buf = buf; // temporary
	}
	if (n->osc.fpmods->count > 0) {
		for (i = 0; i < n->osc.fpmods->count; ++i)
			ins = sched_ins(o, ins, (buf + 1),
					&o->operators[n->osc.fpmods->ids[i]],
					freq, false, i);
		fpm_buf = buf + 1; // temporary
	}
	*(ins++) = INS_CYCLE_PHASE(cycle_buf, freq, buf, n,
			pm_buf>=0, fpm_buf>=0); /* uses rasg_buf as out 2 */
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	ins = sched_param_with_rangemod(o, ins, buf,
			&n->gen.amp, -1, freq);
	amp = buf++;
	tmp_buf = buf;
	*(ins++) = INS_FILL_RASG(rasg_buf, cycle_buf, tmp_buf, n); /* 2 tmp */
	if (wave_env)
		*(ins++) = INS_MIX_MUL(mix_buf, rasg_buf, amp, !layer);
	else
		*(ins++) = INS_MIX_ADD(mix_buf, rasg_buf, amp, !layer);
	return ins;
}

/*
 * Recursively schedule instructions for subnodes of an operator node,
 *
 * \return first unused instructions after any filled in
 */
static GenIns *
sched_ins(sauGenerator *restrict o,
		GenIns *restrict ins, uint32_t buf,
		OperatorNode *restrict n,
		int32_t parent_freq,
		bool wave_env, bool layer) {
	GenNode *gen = &n->gen;
	if ((gen->flags & ON_VISITED) != 0)
		return ins; /* guard against circular references */
	GenIns *time_ins = NULL;
	if (!(gen->flags & ON_TIME_INF)) {
		if (gen->time == 0)
			return ins; /* omit entirely */
		time_ins = ins++; /* insert time check */
	}
	gen->flags |= ON_VISITED;
	/*
	 * Use sub-function.
	 */
	switch (gen->type) {
	case SAU_POPT_WAVE:
		ins = sched_ins_wosc(o, ins, buf,
				n, parent_freq, wave_env, layer);
		break;
	case SAU_POPT_RAS:
		ins = sched_ins_rasg(o, ins, buf,
				n, parent_freq, wave_env, layer);
		break;
	}
	if (time_ins) *time_ins = INS_TIME_JZ_SUB(n, (ins - o->gen_ins));
	gen->flags &= ~ON_VISITED;
	return ins;
}

/*
 * Loop through all voices and schedule instructions for them.
 */
static void run_resched(sauGenerator *restrict o) {
	GenIns *ins = o->gen_ins, *prev_ins;
	for (uint32_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		const sauProgramOpRef *root_op;
		if (!(root_op = sauProgramOpRef_get_root(vn->graph,
						vn->op_count)))
			continue;
		OperatorNode *n = &o->operators[root_op->id];
		prev_ins = ins;
		ins = sched_ins(o, ins, 0,
				n, -1, false, false);
		vn->first_ins = prev_ins - o->gen_ins;
		vn->ins_count = ins - prev_ins;
	}
	o->gen_flags &= ~GEN_RUN_RESCHED;
}

/* Called on any uninitialized instruction. */
static uint32_t gen_ins_error(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	(void)o;
	sau_error("generator", "gen_ins_error() called from ins %d", ins_i);
	return len;
}

static uint32_t gen_ins_time_jz_sub(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	OperatorNode *n = ins->data;
	if (!(n->gen.flags & ON_TIME_INF)) {
		n->gen.time = (n->gen.time > len) ? (n->gen.time - len) : 0;
	}
	return len;
}

/*
 * Add audio layer from \p in_buf into \p buf scaled with \p amp.
 *
 * Used to generate output for carrier or additive modulator.
 */
static inline void block_mix_add(float *restrict buf, uint32_t len,
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer) {
		for (uint32_t i = 0; i < len; ++i) {
			buf[i] += in_buf[i] * amp[i];
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			buf[i] = in_buf[i] * amp[i];
		}
		for (uint32_t i = len; i < BUF_LEN; ++i) {
			buf[i] = 0;
		}
	}
}

static uint32_t gen_ins_mix_add(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	block_mix_add(o->gen_bufs[ins->dst], len, (ins->mode != 0),
			o->gen_bufs[ins->src], o->gen_bufs[ins->ext]);
	return len;
}

/*
 * Multiply audio layer from \p in_buf into \p buf,
 * after scaling to a 0.0 to 1.0 range multiplied by
 * the absolute value of \p amp, and with the high and
 * low ends of the range flipped if \p amp is negative.
 *
 * Used to generate output for modulation with value range.
 */
static inline void block_mix_mul_waveenv(float *restrict buf, uint32_t len,
		bool layer,
		const float *restrict in_buf,
		const float *restrict amp) {
	if (layer) {
		for (uint32_t i = 0; i < len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabsf(s_amp);
			buf[i] *= s;
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = in_buf[i];
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabsf(s_amp);
			buf[i] = s;
		}
		for (uint32_t i = len; i < BUF_LEN; ++i) {
			buf[i] = 0;
		}
	}
	return len;
}

static uint32_t gen_ins_mix_mul(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	block_mix_mul_waveenv(o->gen_bufs[ins->dst], len, (ins->mode != 0),
			o->gen_bufs[ins->src], o->gen_bufs[ins->ext]);
	return len;
}

static uint32_t gen_ins_raparam(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	float *par_buf = o->gen_bufs[ins->dst];
	const float *param_mulbuf = o->gen_bufs[ins->src];
	struct ParWithRangeMod *n = ins->data;
	sauLine_run(&n->par, par_buf, len, param_mulbuf);
	if (ins->ext >= 0) {
		float *r_par_buf = o->gen_bufs[ins->ext];
		sauLine_run(&n->r_par, r_par_buf, len, param_mulbuf);
	} else {
		sauLine_skip(&n->r_par, len);
	}
	return len;
}

/*
 * Replace \p buf values with linear \p mix of \p buf and \p buf2.
 */
static inline void block_ari_means(float *restrict buf, uint32_t len,
		const float *restrict buf2,
		const float *restrict mix) {
	for (uint32_t i = 0; i < len; ++i)
		buf[i] += (buf2[i] - buf[i]) * mix[i];
}

static uint32_t gen_ins_ari_means(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	block_ari_means(o->gen_bufs[ins->dst], len,
			o->gen_bufs[ins->src], o->gen_bufs[ins->ext]);
	return len;
}

static uint32_t gen_ins_phase(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	void *restrict phase_buf = o->gen_bufs[ins->dst];
	const float *restrict freq = o->gen_bufs[ins->src];
	const float *restrict pm_buf = (ins->mode & PHASE_MOD_TYPES(1, 0))
		? o->gen_bufs[ins->ext] : NULL;
	const float *restrict fpm_buf = (ins->mode & PHASE_MOD_TYPES(0, 1))
		? o->gen_bufs[ins->ext + 1] : NULL;
	OperatorNode *restrict n = ins->data;
	sauPhasor_fill(&n->wo.wosc.phasor, phase_buf, len,
			freq, pm_buf, fpm_buf);
	return len;
}

static uint32_t gen_ins_cycle_phase(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	void *restrict cycle_buf = o->gen_bufs[ins->dst];
	void *restrict rasg_buf = o->gen_bufs[ins->dst + 1];
	const float *restrict freq = o->gen_bufs[ins->src];
	const float *restrict pm_buf = (ins->mode & PHASE_MOD_TYPES(1, 0))
		? o->gen_bufs[ins->ext] : NULL;
	const float *restrict fpm_buf = (ins->mode & PHASE_MOD_TYPES(0, 1))
		? o->gen_bufs[ins->ext + 1] : NULL;
	OperatorNode *restrict n = ins->data;
	sauCyclor_fill(&n->rg.rasg.cyclor, cycle_buf, rasg_buf, len,
			freq, pm_buf, fpm_buf);
	return len;
}

static uint32_t gen_ins_fill_wosc(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	void *restrict buf = o->gen_bufs[ins->dst];
	const void *restrict phase_buf = o->gen_bufs[ins->src];
	OperatorNode *restrict n = ins->data;
	sauWOsc_run(&n->wo.wosc, buf, len, phase_buf);
	return len;
}

static uint32_t gen_ins_fill_rasg(struct sauGenerator *restrict o,
		uint32_t ins_i, uint32_t len) {
	GenIns *ins = &o->gen_ins[ins_i];
	void *restrict buf = o->gen_bufs[ins->dst];
	const void *restrict cycle_buf = o->gen_bufs[ins->src];
	void *restrict tmp_buf = o->gen_bufs[ins->ext];
	void *restrict tmp2_buf = o->gen_bufs[ins->ext + 1];
	OperatorNode *restrict n = ins->data;
	sauRasG_run(&n->rg.rasg, len, buf, tmp_buf, tmp2_buf, cycle_buf);
	return len;
}

/*
 * Get function for instruction number.
 */
static inline GenIns_run_f get_gen_ins_run_f(unsigned ins) {
	switch (ins) {
	GEN_INS__ITEMS(GEN_INS__X_CASE)
	default: return NULL;
	}
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
	//block_mix(&n->wo.osc.gen, mix_buf, len, wave_env, layer, tmp_buf, amp);
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
	//block_mix(&n->rg.osc.gen, mix_buf, len, wave_env, layer, rasg_buf, amp);
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
		OperatorNode *restrict n,
		VoiceNode *restrict vn, uint32_t len) {
	float *s_buf = o->gen_bufs[0];
	float *pan_buf = NULL;
	float *mix_l = o->mix_bufs[0];
	float *mix_r = o->mix_bufs[1];
	if (n->gen.pan.flags & SAU_LINEP_GOAL ||
	    n->gen.camods->count > 0) {
		pan_buf = o->gen_bufs[1 + vn->freq_buf_id];
		sauLine_run(&n->gen.pan, pan_buf, len, NULL);
	} else {
		sauLine_skip(&n->gen.pan, len);
	}
	if (n->gen.camods->count > 0) {
		float *freq_buf = vn->freq_buf_id > 0 ?
			o->gen_bufs[vn->freq_buf_id] :
			NULL;
		for (uint32_t i = 0; i < n->gen.camods->count; ++i)
			run_block(o, (o->gen_bufs + 1 + vn->freq_buf_id), len,
					&o->operators[n->gen.camods->ids[i]],
					freq_buf, false, true);
	}
	if (pan_buf != NULL) {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * pan_buf[i];
			mix_l[i] += s - s_r;
			mix_r[i] += s + s_r;
		}
	} else {
		for (uint32_t i = 0; i < len; ++i) {
			float s = s_buf[i] * o->amp_scale;
			float s_r = s * n->gen.pan.v0;
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
#if 1
	uint32_t time = vn->duration, out_len = 0;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	for (uint32_t i = 0; i < vn->ins_count; ++i) {
		uint32_t ins_i = vn->first_ins + i;
		GenIns *ins = &o->gen_ins[ins_i];
		GenIns_run_f ins_f = get_gen_ins_run_f(ins->id);
		/*
		 * Last out_len set is for the final output of the voice.
		 */
		out_len = ins_f(o, ins_i, time);
	}
	if (out_len > 0) {
		const sauProgramOpRef *root_op =
			sauProgramOpRef_get_root(vn->graph, vn->op_count);
		OperatorNode *n = &o->operators[root_op->id];
		mix_add(o, n, vn, out_len);
	}
	vn->duration -= time;
	return out_len;
#else
	const sauProgramOpRef *root_op;
	if (!(root_op = sauProgramOpRef_get_root(vn->graph, vn->op_count)))
		return 0;
	OperatorNode *n = &o->operators[root_op->id];
	uint32_t time = vn->duration, out_len = 0;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	if (n->gen.time > 0)
		out_len = run_block(o, o->gen_bufs, time, n,
				NULL, false, false);
	if (out_len > 0)
		mix_add(o, n, vn, out_len);
	vn->duration -= time;
	return out_len;
#endif
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
	if (o->gen_flags & GEN_RUN_RESCHED)
		run_resched(o);
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
