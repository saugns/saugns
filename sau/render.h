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

#pragma once
#include "parse.h"

/* Macro used for lists which include audio rendering instruction names. */
#define SAU_RINS__ITEMS(X) \
	X(error) /* is first to make blank instructions yield error */ \
	X(gen_push_jz) \
	X(gen_pop_mix) \
	X(mix_to_voice) \
	X(run_noisegen) \
	X(run_ralsosc) \
	X(run_ralsosc_selfmod) \
	X(run_ralsosc_pdist) \
	X(run_waveosc) \
	X(run_waveosc_selfmod) \
	X(run_waveosc_pdist) \
	X(run_osc_phasor) \
	X(run_par_line) \
	X(run_par_env) \
	X(mix_valrange) \
	X(nsetf) \
	X(nmulf) \
	//
#define SAU_RINS__X_ID(NAME) SAU_RINS_N_##NAME,
#define SAU_RINS__X_NAME(NAME) #NAME,

/** Audio rendering instruction opcodes. */
enum {
	SAU_RINS__ITEMS(SAU_RINS__X_ID)
	SAU_RINS_NAMED
};

extern const char *const sauRIns_names[SAU_RINS_NAMED]; // define in semantics.h

/** Audio rendering mix mode flags. */
enum {
	SAU_RMIX_LAYER    = 1U<<0,
	SAU_RMIX_MUL_WE   = 1U<<1,
};

// for packing 2 16-bit values into 32-bit value; higher args go in higher bits
#define sau_rins_wpair(w0, w1) (((uint32_t)(w0)) | (((uint32_t)(w1)) << 16))
#define sau_rins_get_w0(wp) ((wp)         & UINT16_MAX)
#define sau_rins_get_w1(wp) (((wp) >> 16) & UINT16_MAX)

// for packing 4 8-bit values into 32-bit value; higher args go in higher bits
#define sau_rins_bquad(b0, b1, b2, b3) \
	(((uint32_t)(b0))         | (((uint32_t)(b1)) << 8) | \
	 (((uint32_t)(b2)) << 16) | (((uint32_t)(b3)) << 24))
// for packing 2 8-bit values into 16-bit value; higher args go in higher bits
#define sau_rins_bpair(b0, b1) (((uint16_t)(b0)) | (((uint16_t)(b1)) << 8))
#define sau_rins_get_b0(bp) ((bp)         & UINT8_MAX)
#define sau_rins_get_b1(bp) (((bp) >> 8)  & UINT8_MAX)
#define sau_rins_get_b2(bp) (((bp) >> 16) & UINT8_MAX)
#define sau_rins_get_b3(bp) (((bp) >> 24) & UINT8_MAX)

typedef struct sauRIns {
	uint8_t op;
	uint8_t mode;
	bool has_a : 1, has_a_f : 1;
	bool has_b : 1, has_b_f : 1;
	bool has_c : 1, has_c_f : 1;
	union sauRInsVal {
		uint32_t i;
		float f;
	} a, b, c;
	uint32_t x;
} sauRIns;

static inline sauRIns
sauRIns_gen_push_jz(uint32_t gen_id, uint32_t jmp_dst,
		uint32_t out_buf_id, uint8_t mix_mode) {
	return (sauRIns){.op = SAU_RINS_N_gen_push_jz,
		.mode = mix_mode,
		.a.i = gen_id, .has_a = true,
		.b.i = jmp_dst, .has_b = true,
		.x = out_buf_id,
	};
}

static inline sauRIns
sauRIns_gen_pop_mix(uint32_t out_buf_id, uint8_t mix_mode,
		uint32_t in_buf_id, uint32_t amp_buf_id, bool has_amp,
		float amp_v0) {
	sauRIns ins = {.op = SAU_RINS_N_gen_pop_mix,
		.mode = mix_mode,
		.a.i = in_buf_id, .has_a = true,
		.b.i = amp_buf_id, .has_b = true,
		.x = out_buf_id,
	};
	if (!has_amp) { ins.b.f = amp_v0; ins.has_b_f = true; }
	return ins;
}

static inline sauRIns
sauRIns_mix_to_voice(uint32_t in_buf_id, uint32_t pan_buf_id, bool has_pan,
		float pan_v0) {
	sauRIns ins = {.op = SAU_RINS_N_mix_to_voice,
		.a.i = in_buf_id, .has_a = true,
		.b.i = pan_buf_id, .has_b = true,
	};
	if (!has_pan) { ins.b.f = pan_v0; ins.has_b_f = true; }
	return ins;
}

static inline sauRIns
sauRIns_run_noisegen(uint32_t buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_noisegen,
		.x = buf_id,
	};
}

static inline sauRIns
sauRIns_run_ralsosc(uint32_t main_buf_id, uint32_t cycle_buf_id,
		uint32_t end_a_buf_id, uint32_t end_b_buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_ralsosc,
		.a.i = sau_rins_wpair(end_a_buf_id, end_b_buf_id),
		.has_a = true,
		.x = sau_rins_wpair(main_buf_id, cycle_buf_id),
	};
}

static inline sauRIns
sauRIns_run_ralsosc_selfmod(uint32_t main_buf_id, uint32_t cycle_buf_id,
		uint32_t pma_buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_ralsosc_selfmod,
		.a.i = pma_buf_id, .has_a = true,
		.x = sau_rins_wpair(main_buf_id, cycle_buf_id),
	};
}

static inline sauRIns
sauRIns_run_ralsosc_pdist(uint32_t phase_buf_id, uint32_t cycle_buf_id,
		uint32_t pd_buf_id, uint8_t pd_fn_id,
		uint32_t pd_f_buf_id, float pd_f_v0,
		uint32_t pd_p_buf_id, float pd_p_v0) {
	sauRIns ins = {.op = SAU_RINS_N_run_ralsosc_pdist,
		.mode = pd_fn_id,
		.a.i = pd_buf_id,   .has_a = true,
		.b.i = pd_f_buf_id, .has_b = true,
		.c.i = pd_p_buf_id, .has_c = true,
		.x = sau_rins_wpair(phase_buf_id, cycle_buf_id),
	};
	if (!pd_f_buf_id) { ins.b.f = pd_f_v0; ins.has_b_f = true; }
	if (!pd_p_buf_id) { ins.c.f = pd_p_v0; ins.has_c_f = true; }
	return ins;
}

static inline sauRIns
sauRIns_run_waveosc(uint32_t main_buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_waveosc,
		.x = main_buf_id,
	};
}

static inline sauRIns
sauRIns_run_waveosc_selfmod(uint32_t main_buf_id, uint32_t pma_buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_waveosc_selfmod,
		.a.i = pma_buf_id, .has_a = true,
		.x = main_buf_id,
	};
}

static inline sauRIns
sauRIns_run_waveosc_pdist(uint32_t phase_buf_id,
		uint32_t pd_buf_id, uint8_t pd_fn_id,
		uint32_t pd_f_buf_id, float pd_f_v0,
		uint32_t pd_p_buf_id, float pd_p_v0) {
	sauRIns ins = {.op = SAU_RINS_N_run_waveosc_pdist,
		.mode = pd_fn_id,
		.a.i = pd_buf_id,   .has_a = true,
		.b.i = pd_f_buf_id, .has_b = true,
		.c.i = pd_p_buf_id, .has_c = true,
		.x = phase_buf_id,
	};
	if (!pd_f_buf_id) { ins.b.f = pd_f_v0; ins.has_b_f = true; }
	if (!pd_p_buf_id) { ins.c.f = pd_p_v0; ins.has_c_f = true; }
	return ins;
}

static inline sauRIns
sauRIns_run_osc_phasor(uint32_t phase_buf_id,
		uint32_t cycle_buf_id, bool has_cycle_buf,
		uint32_t freq_buf_id,
		uint32_t pm_in_buf, bool has_pm_in) {
	return (sauRIns){.op = SAU_RINS_N_run_osc_phasor,
		.a.i = freq_buf_id, .has_a = true,
		.b.i = (has_pm_in ? pm_in_buf : 0), .has_b = has_pm_in,
		.x = sau_rins_wpair(phase_buf_id,
				has_cycle_buf ? cycle_buf_id : 0),
	};
}

static inline sauRIns
sauRIns_run_par_line(uint8_t par_id, uint8_t sub_id,
		const sauLine *restrict line,
		uint32_t par_buf_id, uint32_t mul_buf_id, bool has_mul) {
	return (sauRIns){.op = SAU_RINS_N_run_par_line,
		.a.f = line->v0, .has_a_f = true, .has_a = true,
		.b.f = line->vt, .has_b_f = true, .has_b = true,
		.c.i = sau_rins_bquad(par_id, sub_id, line->type, line->flags),
		.has_c = true,
		.x = sau_rins_wpair(par_buf_id, has_mul ? mul_buf_id : 0),
	};
}

static inline sauRIns
sauRIns_run_par_env(uint8_t par_id, uint32_t env_buf_id) {
	return (sauRIns){.op = SAU_RINS_N_run_par_env,
		.a.i = par_id, .has_a = true,
		.x = env_buf_id,
	};
}

static inline sauRIns
sauRIns_mix_valrange(uint32_t buf_id, bool is_buf_filled, float a_v0,
		uint32_t buf2_id, bool has_buf2, float b_v0,
		uint32_t buf3_id) {
	return (sauRIns){.op = SAU_RINS_N_mix_valrange,
		.a.f = a_v0, .has_a_f = true, .has_a = true,
		.b.f = b_v0, .has_b_f = true, .has_b = true,
		.c.i = sau_rins_wpair(buf3_id, has_buf2 ? buf2_id : 0),
		.has_c = true,
		.x = sau_rins_wpair(buf_id, is_buf_filled),
	};
}

static inline sauRIns
sauRIns_nsetf(uint32_t buf_id, float val) {
	return (sauRIns){.op = SAU_RINS_N_nsetf,
		.a.f = val, .has_a_f = true, .has_a = true,
		.x = buf_id,
	};
}

static inline sauRIns
sauRIns_nmulf(uint32_t buf_id, float val) {
	return (sauRIns){.op = SAU_RINS_N_nmulf,
		.b.f = val, .has_b_f = true, .has_b = true,
		.x = buf_id,
	};
}

static inline sauRIns
sauRIns_nmulnf(uint32_t buf_id, uint32_t buf2_id) {
	return (sauRIns){.op = SAU_RINS_N_nmulf,
		.a.i = buf2_id, .has_a = true,
		.x = buf_id,
	};
}

static inline sauRIns
sauRIns_nmulnff(uint32_t buf_id, uint32_t buf2_id, float val) {
	return (sauRIns){.op = SAU_RINS_N_nmulf,
		.a.i = buf2_id, .has_a = true,
		.b.f = val, .has_b_f = true, .has_b = true,
		.x = buf_id,
	};
}

struct sauGenerator;
typedef struct sauGenerator sauGenerator;

sauGenerator* sau_create_Generator(const sauParse *restrict prg,
		uint32_t srate) sauMalloclike;
void sau_destroy_Generator(sauGenerator *restrict o);

bool sauGenerator_run(sauGenerator *restrict o,
		int16_t *restrict buf, size_t buf_len, bool stereo,
		size_t *restrict out_len);
