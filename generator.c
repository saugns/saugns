/* Copyright (c) 2011-2013 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "osc.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

#define MS_TO_ABS(x, srate) (int) (((float)(x)) * (srate) * .001f)

enum {
	SGS_FLAG_INIT = 1<<0,
	SGS_FLAG_EXEC = 1<<1
};

typedef union BufData {
	int i;
	float f;
} BufData;

#define BUF_LEN 256
typedef BufData Buf[BUF_LEN];

typedef struct ParameterValit {
	uint time, pos;
	float goal;
	uchar type;
} ParameterValit;

typedef struct OperatorNode {
	int time;
	uint silence;
	uint output_block_id;
	int freq_block_id, /* -1 if none */
	    freq_mod_block_id,
	    phase_mod_block_id,
	    amp_block_id,
	    amp_mod_block_id;
	uchar type, attr;
	float freq, dynfreq;
	SGSOscLuv *osctype;
	SGSOsc osc;
	float amp, dynamp;
	ParameterValit valitamp, valitfreq;
} OperatorNode;

typedef struct VoiceNode {
	int pos; /* negative for wait time */
	uint input_block_id;
	const int *operator_list;
	uint operator_c;
	int panning_block_id; /* -1 if none */
	uchar flag, attr;
	float panning;
	ParameterValit valitpanning;
} VoiceNode;

typedef union Data {
	int i;
	float f;
	void *v;
} Data;

typedef struct EventNode {
	uint waittime;
	uint params;
	const SGSProgramVoiceData *voice;
	const SGSProgramOperatorData *operator;
} EventNode;

struct SGSGenerator {
	uint srate;
	double osc_coeff;
	uint event, eventc;
	uint eventpos;
	EventNode *events;
	Buf *blocks;
	uint blockc;
	uint voice, voicec;
	VoiceNode *voices;
	OperatorNode operators[1]; /* sized to number of nodes */
};

/*
 * Allocate SGSGenerator with the passed sample rate and using the
 * given SGSProgram.
 */
SGSGenerator* SGS_generator_create(uint srate, struct SGSProgram *prg) {
	SGSGenerator *o;
	uint i, indexwaittime;
	uint size, eventssize, blockssize, voicessize, operatorssize;
	/*
	 * Establish allocation sizes.
	 */
	size = sizeof(SGSGenerator) - sizeof(OperatorNode);
	eventssize = sizeof(EventNode) * prg->eventc;
	blockssize = sizeof(Buf) * prg->blockc;
	voicessize = sizeof(VoiceNode) * prg->voicec;
	operatorssize = sizeof(OperatorNode) * prg->operatorc;
	/*
	 * Allocate & initialize.
	 */
	o = calloc(1, size + operatorssize + eventssize + blockssize +
		voicessize);
	o->srate = srate;
	o->osc_coeff = SGSOsc_COEFF(srate);
	o->event = 0;
	o->eventc = prg->eventc;
	o->eventpos = 0;
	o->events = (void*)(((uchar*)o) + size + operatorssize);
	o->blocks = (void*)(((uchar*)o) + size + operatorssize + eventssize);
	o->blockc = prg->blockc;
	o->voice = 0;
	o->voicec = prg->voicec;
	o->voices = (void*)(((uchar*)o) + size + operatorssize + eventssize +
		blockssize);
	SGSOsc_init();
	/*
	 * Fill in events according to the SGSProgram, ie. copy timed state
	 * changes for voices and operators.
	 */
	indexwaittime = 0;
	for (i = 0; i < prg->eventc; ++i) {
		const SGSProgramEvent *prg_e = &prg->events[i];
		EventNode *e = &o->events[i];
		e->waittime = MS_TO_ABS(prg_e->wait_ms, srate);
		e->params = prg_e->params;
		e->voice = prg_e->voice;
		e->operator = prg_e->operator;
		indexwaittime += e->waittime;
		if (prg_e->voice) {
			const SGSProgramVoiceData *vd = prg_e->voice;
			o->voices[vd->voice_id].pos = -indexwaittime;
			indexwaittime = 0;
		}
	}
	return o;
}

/*
 * Processes one event; to be called for the event when its time comes.
 */
static void SGS_generator_handle_event(SGSGenerator *o, EventNode *e) {
	if (1) /* more types to be added in the future */ {
		VoiceNode *vn;
		const SGSProgramVoiceData *vd = e->voice;
		OperatorNode *on;
		const SGSProgramOperatorData *od = e->operator;
		/*
		 * Set state of operator and/or voice. Voice updates must be done last,
		 * as operator updates may change node adjacents and buffer recalculation
		 * is currently done during voice updates.
		 */
		if (od != NULL) {
			on = &o->operators[od->operator_id];
			on->output_block_id = od->output_block_id;
			on->freq_block_id = od->freq_block_id;
			on->freq_mod_block_id = od->freq_mod_block_id;
			on->phase_mod_block_id = od->phase_mod_block_id;
			on->amp_block_id = od->amp_block_id;
			on->amp_mod_block_id = od->amp_mod_block_id;
			if (e->params & SGS_OPATTR) {
				uchar attr = od->attr;
				if (!(e->params & SGS_FREQ)) {
					/* May change during processing; preserve state of FREQRATIO flag */
					attr &= ~SGS_ATTR_FREQRATIO;
					attr |= on->attr & SGS_ATTR_FREQRATIO;
				}
				on->attr = attr;
			}
			if (e->params & SGS_WAVE) switch (od->wave) {
			case SGS_WAVE_SIN:
				on->osctype = SGSOsc_sin;
				break;
			case SGS_WAVE_SRS:
				on->osctype = SGSOsc_srs;
				break;
			case SGS_WAVE_TRI:
				on->osctype = SGSOsc_tri;
				break;
			case SGS_WAVE_SQR:
				on->osctype = SGSOsc_sqr;
				break;
			case SGS_WAVE_SAW:
				on->osctype = SGSOsc_saw;
				break;
			}
			if (e->params & SGS_TIME)
				on->time = (od->time_ms == SGS_TIME_INF) ?
					SGS_TIME_INF :
					MS_TO_ABS(od->time_ms, o->srate);
			if (e->params & SGS_SILENCE)
				on->silence = MS_TO_ABS(od->silence_ms, o->srate);
			if (e->params & SGS_FREQ)
				on->freq = od->freq;
			if (e->params & SGS_VALITFREQ) {
				on->valitfreq.time = MS_TO_ABS(od->valitfreq.time_ms, o->srate);
				on->valitfreq.pos = 0;
				on->valitfreq.goal = od->valitfreq.goal;
				on->valitfreq.type = od->valitfreq.type;
			}
			if (e->params & SGS_DYNFREQ)
				on->dynfreq = od->dynfreq;
			if (e->params & SGS_PHASE)
				SGSOsc_SET_PHASE(&on->osc, SGSOsc_PHASE(od->phase));
			if (e->params & SGS_AMP)
				on->amp = od->amp;
			if (e->params & SGS_VALITAMP) {
				on->valitamp.time = MS_TO_ABS(od->valitamp.time_ms, o->srate);
				on->valitamp.pos = 0;
				on->valitamp.goal = od->valitamp.goal;
				on->valitamp.type = od->valitamp.type;
			}
			if (e->params & SGS_DYNAMP)
				on->dynamp = od->dynamp;
		}
		if (vd != NULL) {
			vn = &o->voices[vd->voice_id];
			vn->input_block_id = vd->input_block_id;
			if (vn->operator_list != vd->operator_list) {
				vn->operator_list = vd->operator_list;
				vn->operator_c = vd->operator_c;
			}
			if (e->params & SGS_VOATTR)
				vn->attr = vd->attr;
			if (e->params & SGS_PANNING)
				vn->panning = vd->panning;
			if (e->params & SGS_VALITPANNING) {
				vn->valitpanning.time = MS_TO_ABS(vd->valitpanning.time_ms, o->srate);
				vn->valitpanning.pos = 0;
				vn->valitpanning.goal = vd->valitpanning.goal;
				vn->valitpanning.type = vd->valitpanning.type;
			}
			vn->flag |= SGS_FLAG_INIT | SGS_FLAG_EXEC;
			vn->pos = 0;
			if (o->voice > vd->voice_id) /* go back to re-activated node */
				o->voice = vd->voice_id;
		}
	}
}

void SGS_generator_destroy(SGSGenerator *o) {
	free(o->blocks);
	free(o);
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
static uchar run_param(BufData *buf, uint buf_len, ParameterValit *vi,
		float *state, const BufData *modbuf) {
	uint i, end, len, filllen;
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
				(mod * (629.f/1792.f) +
					modp2 * (1163.f/1792.f));
			(*buf++).f = vi->goal + (s0 - vi->goal) * mod;
		}
		break;
	case SGS_VALIT_LOG:
		for (i = vi->pos, end = i + len; i < end; ++i) {
			double mod = i * coeff,
						 modp2 = mod * mod,
						 modp3 = modp2 * mod;
			mod = modp3 + (modp2 * modp3 - modp2) *
				(mod * (629.f/1792.f) +
					modp2 * (1163.f/1792.f));
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
 * Generate up to len samples for an operator node, the remainder (if any)
 * zero-filled if acc_ind is zero.
 *
 * Recursively visits the subnodes of the operator node in the process, if
 * any.
 *
 * Returns number of samples generated for the node.
 */
static uint run_block(SGSGenerator *o, uint len, OperatorNode *n,
		uint acc_ind) {
	uint i, zero_len, skip_len;
	BufData *sbuf = o->blocks[n->output_block_id],
		*freq = NULL,
		*freq_mod = NULL,
		*phase_mod = NULL,
		*amp = NULL,
		*amp_mod = NULL;
	ParameterValit *vi = NULL;
	/*
	 * If silence, zero-fill and delay processing for duration.
	 */
	zero_len = 0;
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
	 * Limit length to time duration of operator.
	 */
	skip_len = 0;
	if (n->time < (int)len && n->time != SGS_TIME_INF) {
		skip_len = len - n->time;
		len = n->time;
	}
	/*
	 * Handle frequency (alternatively ratio) parameter, including frequency
	 * modulation if modulators linked.
	 */
	if (n->freq_mod_block_id > -1)
		freq_mod = o->blocks[n->freq_mod_block_id];
	if (n->attr & SGS_ATTR_VALITFREQ) {
		vi = &n->valitfreq;
		if (n->attr & SGS_ATTR_VALITFREQRATIO) {
			if (!(n->attr & SGS_ATTR_FREQRATIO)) {
				n->attr |= SGS_ATTR_FREQRATIO;
				n->freq /= freq_mod[0].f;
			}
		} else {
			if (n->attr & SGS_ATTR_FREQRATIO) {
				n->attr &= ~SGS_ATTR_FREQRATIO;
				n->freq *= freq_mod[0].f;
			}
		}
	}
	if (n->freq_block_id > -1) {
		freq = o->blocks[n->freq_block_id];
		if (run_param(freq, len, vi, &n->freq, freq_mod))
			n->attr &= ~(SGS_ATTR_VALITFREQ |
				SGS_ATTR_VALITFREQRATIO);
	}
	if (n->phase_mod_block_id > -1)
		phase_mod = o->blocks[n->phase_mod_block_id];
	if (n->amp_block_id > -1)
		amp = o->blocks[n->amp_block_id];
	if (n->amp_mod_block_id > -1)
		amp_mod = o->blocks[n->amp_mod_block_id];
	if (!(n->attr & SGS_ATTR_WAVEENV)) {
		/*
		 * Handle amplitude parameter, including amplitude modulation
		 * if block specified.
		 */
		if (amp != NULL) {
			vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
			if (run_param(amp, len, vi, &n->amp, 0))
				n->attr &= ~SGS_ATTR_VALITAMP;
			if (amp_mod != NULL) {
				float dynampdiff = n->dynamp - n->amp;
				for (i = 0; i < len; ++i)
					amp[i].f = n->amp +
						amp_mod[i].f * dynampdiff;
			}
			/*
			 * Generate integer output using:
			 *  - freq buffer
			 *  - phase mod buffer (optionally)
			 *  - amp buffer
			 */
			for (i = 0; i < len; ++i) {
				int s, spm = 0;
				float sfreq = freq[i].f, samp = amp[i].f;
				if (phase_mod) spm = phase_mod[i].i;
				SGSOsc_RUN_PM(&n->osc, n->osctype,
					o->osc_coeff, sfreq, spm, samp, s);
				if (acc_ind) s += sbuf[i].i;
				sbuf[i].i = s;
			}
		} else {
			/*
			 * Generate integer output using:
			 *  - freq buffer
			 *  - phase mod buffer (optionally)
			 */
			for (i = 0; i < len; ++i) {
				int s, spm = 0;
				float sfreq = freq[i].f;
				if (phase_mod) spm = phase_mod[i].i;
				SGSOsc_RUN_PM(&n->osc, n->osctype,
					o->osc_coeff, sfreq, spm, n->amp, s);
				if (acc_ind) s += sbuf[i].i;
				sbuf[i].i = s;
			}
		}
	} else {
		if (amp != NULL) {
			/*
			 * Generate float output using:
			 *  - freq buffer
			 *  - phase mod buffer (optionally)
			 *  - amp buffer
			 *
			 *  FIXME: No amp parameter or buffer usage!
			 */
			for (i = 0; i < len; ++i) {
				float s, sfreq = freq[i].f;
				int spm = 0;
				if (phase_mod) spm = phase_mod[i].i;
				SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype,
					o->osc_coeff, sfreq, spm, s);
				if (acc_ind) s *= sbuf[i].f;
				sbuf[i].f = s;
			}
		} else {
			/*
			 * Generate float output using:
			 *  - freq buffer
			 *  - phase mod buffer (optionally)
			 *
			 *  FIXME: No amp parameter or buffer usage!
			 */
			for (i = 0; i < len; ++i) {
				float s, sfreq = freq[i].f;
				int spm = 0;
				if (phase_mod) spm = phase_mod[i].i;
				SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype,
					o->osc_coeff, sfreq, spm, s);
				if (acc_ind) s *= sbuf[i].f;
				sbuf[i].f = s;
			}
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
	return zero_len + len;
}

/*
 * Generate up to buf_len samples for a voice, these mixed into the
 * interleaved output stereo buffer by simple addition.
 *
 * Returns number of samples generated for the voice.
 */
static uint run_voice(SGSGenerator *o, VoiceNode *vn, short *out,
		uint buf_len) {
	BufData *input_block;
	BufData *panning_block = NULL;
	const int *ops;
	uint i, opc, ret_len = 0, finished = 1;
	int time;
	short *sp;
	if (!vn->operator_list) goto RETURN;
	input_block = o->blocks[vn->input_block_id];
	if (vn->panning_block_id > -1)
		panning_block = o->blocks[vn->panning_block_id];
	ops = vn->operator_list;
	opc = vn->operator_c;
	/*
	 * Set time == longest operator time limited to buf_len.
	 */
	time = 0;
	for (i = 0; i < opc; ++i) {
		OperatorNode *n = &o->operators[ops[i]];
		if (n->time == 0) continue;
		if (n->time > time && n->time != SGS_TIME_INF) time = n->time;
	}
	if (time > (int)buf_len) time = buf_len;
	/*
	 * Repeatedly generate up to BUF_LEN samples until done.
	 */
	sp = out;
	while (time) {
		uint acc_ind = 0;
		uint gen_len = 0;
		uint len = (time < BUF_LEN) ? time : BUF_LEN;
		time -= len;
		for (i = 0; i < opc; ++i) {
			uint last_len;
			OperatorNode *n = &o->operators[ops[i]];
			if (n->time == 0) continue;
			last_len = run_block(o, len, n, acc_ind++);
			if (last_len > gen_len) gen_len = last_len;
		}
		if (!gen_len) goto RETURN;
		if (panning_block != NULL) {
			if (vn->attr & SGS_ATTR_VALITPANNING) {
				if (run_param(panning_block, gen_len,
						&vn->valitpanning,
						&vn->panning, 0))
					vn->attr &= ~SGS_ATTR_VALITPANNING;
			}
			for (i = 0; i < gen_len; ++i) {
				int s = input_block[i].i, p;
				SET_I2FV(p, ((float)s) * panning_block[i].f);
				*sp++ += s - p;
				*sp++ += p;
			}
		} else {
			for (i = 0; i < gen_len; ++i) {
				int s = input_block[i].i, p;
				SET_I2FV(p, ((float)s) * vn->panning);
				*sp++ += s - p;
				*sp++ += p;
			}
		}
		ret_len += gen_len;
	}
	for (i = 0; i < opc; ++i) {
		OperatorNode *n = &o->operators[ops[i]];
		if (n->time != 0) {
			finished = 0;
			break;
		}
	}
RETURN:
	vn->pos += ret_len;
	if (finished)
		vn->flag &= ~SGS_FLAG_EXEC;
	return ret_len;
}

/*
 * Main sound generation/processing function. Call repeatedly to fill
 * interleaved stereo buffer buf with up to len new samples, the
 * remainder (if any, which may occur at end of signal) zero-filled.
 *
 * If supplied, gen_len will be set to the precise length generated.
 *
 * Returns non-zero until the end of the generated signal has been
 * reached.
 */
uchar SGS_generator_run(SGSGenerator *o, short *buf, uint buf_len,
		uint *gen_len) {
	short *sp = buf;
	uint i, len = buf_len, skip_len, ret_len = 0, last_len;
	for (i = len; i--; sp += 2) {
		sp[0] = 0;
		sp[1] = 0;
	}
PROCESS:
	skip_len = 0;
	while (o->event < o->eventc) {
		EventNode *e = &o->events[o->event];
		if (o->eventpos < e->waittime) {
			uint waittime = e->waittime - o->eventpos;
			if (waittime < len) {
				/*
				 * Limit len to waittime, further splitting processing into two
				 * blocks; otherwise, voice processing could get ahead of event
				 * handling in some cases - which would give undefined results!
				 */
				skip_len = len - waittime;
				len = waittime;
			}
			o->eventpos += len;
			break;
		}
		SGS_generator_handle_event(o, e);
		++o->event;
		o->eventpos = 0;
	}
	last_len = 0;
	for (i = o->voice; i < o->voicec; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (vn->pos < 0) {
			uint waittime = -vn->pos;
			if (waittime >= len) {
				vn->pos += len;
				break; /* end for now; waittimes accumulate across nodes */
			}
			buf += waittime+waittime; /* doubled given stereo interleaving */
			len -= waittime;
			vn->pos = 0;
		}
		if (vn->flag & SGS_FLAG_EXEC) {
			uint voice_len = run_voice(o, vn, buf, len);
			if (voice_len > last_len) last_len = voice_len; 
		}
	}
	ret_len += last_len;
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
		if (o->voice == o->voicec) {
			if (o->event != o->eventc) break;
			if (gen_len) *gen_len = ret_len;
			return 0;
		}
		vn = &o->voices[o->voice];
		if (!(vn->flag & SGS_FLAG_INIT) || vn->flag & SGS_FLAG_EXEC) break;
		++o->voice;
	}
	if (gen_len) *gen_len = buf_len;
	return 1;
}
