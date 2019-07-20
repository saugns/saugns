/* saugns: Script file parser.
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

#include "scanner.h"
#include "parser.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

typedef struct ScanLookup {
	SAU_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*ramp_names;
} ScanLookup;

/*
 * Default script options, used until changed in a script.
 */
static const SAU_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
};

static bool init_ScanLookup(ScanLookup *restrict o, SAU_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SAU_SymTab_pool_stra(st,
			SAU_Wave_names, SAU_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->ramp_names = SAU_SymTab_pool_stra(st,
			SAU_Ramp_names, SAU_RAMP_TYPES);
	if (!o->ramp_names)
		return false;
	return true;
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool handle_unknown_or_eof(SAU_Scanner *restrict o, uint8_t c) {
	if (c == 0)
		return false;
	if (IS_VISIBLE(c)) {
		SAU_Scanner_warning(o, NULL,
				"invalid character '%c'", c);
	} else {
		SAU_Scanner_warning(o, NULL,
				"invalid character (value 0x%02hhX)", c);
	}
	return true;
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_eof_without_closing(SAU_Scanner *restrict o, uint8_t c) {
	SAU_Scanner_warning(o, NULL, "end of file without closing '%c'", c);
}

/*
 * Print warning for scope-closing character without scope-opening character.
 */
static void warn_closing_without_opening(SAU_Scanner *restrict o,
		uint8_t close_c, uint8_t open_c) {
	SAU_Scanner_warning(o, NULL, "closing '%c' without opening '%c'",
			close_c, open_c);
}

typedef float (*NumSym_f)(SAU_Scanner *restrict o);

typedef struct NumParser {
	SAU_Scanner *sc;
	NumSym_f numsym_f;
	SAU_ScanFrame sf_start;
	bool has_infnum;
} NumParser;
enum {
	NUMEXP_SUB = 0,
	NUMEXP_ADT,
	NUMEXP_MLT,
	NUMEXP_POW,
	NUMEXP_NUM,
};
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
	SAU_Scanner *sc = o->sc;
	double num;
	bool minus = false;
	uint8_t c;
	if (level == 1) SAU_Scanner_setws_level(sc, SAU_SCAN_WS_NONE);
	c = SAU_Scanner_getc(sc);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SAU_Scanner_getc(sc);
	}
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else if (o->numsym_f && IS_ALPHA(c)) {
		SAU_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (isnan(num))
			return NAN;
	} else {
		size_t read_len;
		SAU_Scanner_ungetc(sc);
		SAU_Scanner_getd(sc, &num, false, &read_len);
		if (read_len == 0)
			return NAN;
	}
	if (minus) num = -num;
	if (level == 0 || pri == NUMEXP_NUM)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		c = SAU_Scanner_getc(sc);
		switch (c) {
		case '(':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_SUB, level+1);
			break;
		case ')':
			if (pri != NUMEXP_SUB) goto DEFER;
			return num;
		case '^':
			if (pri >= NUMEXP_POW) goto DEFER;
			num = exp(log(num) * scan_num_r(o, NUMEXP_POW, level));
			break;
		case '*':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '/':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num /= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '+':
			if (pri >= NUMEXP_ADT) goto DEFER;
			num += scan_num_r(o, NUMEXP_ADT, level);
			break;
		case '-':
			if (pri >= NUMEXP_ADT) goto DEFER;
			num -= scan_num_r(o, NUMEXP_ADT, level);
			break;
		default:
			if (pri == NUMEXP_SUB) {
				SAU_Scanner_warning(sc, &o->sf_start,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	return num;
}
static sauNoinline bool scan_num(SAU_Scanner *restrict o,
		NumSym_f scan_numsym, float *restrict var) {
	NumParser np = {o, scan_numsym, o->sf, false};
	uint8_t ws_level = o->ws_level;
	float num = scan_num_r(&np, NUMEXP_NUM, 0);
	SAU_Scanner_setws_level(o, ws_level); // restore if changed
	if (isnan(num))
		return false;
	if (isinf(num)) np.has_infnum = true;
	if (np.has_infnum) {
		SAU_Scanner_warning(o, &np.sf_start,
				"discarding expression with infinite number");
		return false;
	}
	*var = num;
	return true;
}

static sauNoinline bool scan_time_val(SAU_Scanner *restrict o,
		uint32_t *restrict val) {
	SAU_ScanFrame sf = o->sf;
	float val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		SAU_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = lrint(val_s * 1000.f);
	return true;
}

#define OCTAVES 11
static float scan_note(SAU_Scanner *restrict o) {
	static const float octaves[OCTAVES] = {
		(1.f/16.f),
		(1.f/8.f),
		(1.f/4.f),
		(1.f/2.f),
		1.f, /* no. 4 - standard tuning here */
		2.f,
		4.f,
		8.f,
		16.f,
		32.f,
		64.f
	};
	static const float notes[3][8] = {
		{ /* flat */
			48.f/25.f,
			16.f/15.f,
			6.f/5.f,
			32.f/25.f,
			36.f/25.f,
			8.f/5.f,
			9.f/5.f,
			96.f/25.f
		},
		{ /* normal (9/8 replaced with 10/9 for symmetry) */
			1.f,
			10.f/9.f,
			5.f/4.f,
			4.f/3.f,
			3.f/2.f,
			5.f/3.f,
			15.f/8.f,
			2.f
		},
		{ /* sharp */
			25.f/24.f,
			75.f/64.f,
			125.f/96.f,
			25.f/18.f,
			25.f/16.f,
			225.f/128.f,
			125.f/64.f,
			25.f/12.f
		}
	};
	ScanLookup *sl = o->data;
	float freq;
	uint8_t c = SAU_Scanner_getc(o);
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	size_t read_len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = SAU_Scanner_getc(o);
	}
	if (c < 'A' || c > 'G') {
		SAU_Scanner_warning(o, NULL,
"invalid note specified - should be C, D, E, F, G, A or B");
		return NAN;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = SAU_Scanner_getc(o);
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else
		SAU_Scanner_ungetc(o);
	SAU_Scanner_geti(o, &octave, false, &read_len);
	if (read_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SAU_Scanner_warning(o, NULL,
"invalid octave specified for note - valid range 0-10");
		octave = 4;
	}
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static SAU_SymStr *scan_label(SAU_Scanner *restrict o,
		char op) {
	SAU_SymStr *s = NULL;
	SAU_Scanner_get_symstr(o, &s);
	if (!s) {
		SAU_Scanner_warning(o, NULL,
				"ignoring %c without label name", op);
	}
	return s;
}

static bool scan_symafind(SAU_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SAU_ScanFrame sf_begin = o->sf;
	SAU_SymStr *s = NULL;
	SAU_Scanner_get_symstr(o, &s);
	if (!s) {
		SAU_Scanner_warning(o, NULL,
				"%s type value missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SAU_Scanner_warning(o, &sf_begin,
			"invalid %s type value; available are:", print_type);
	SAU_print_names(stra, "\t", stderr);
	return false;
}

static bool scan_wavetype(SAU_Scanner *restrict o, size_t *restrict found_id) {
	ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names,
			found_id, "wave type");
}

static bool scan_ramp_state(SAU_Scanner *restrict o, NumSym_f scan_numsym,
		SAU_Ramp *restrict ramp, bool mult) {
	if (!scan_num(o, scan_numsym, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SAU_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SAU_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SAU_RAMPP_STATE;
	return true;
}

static bool scan_ramp(SAU_Scanner *restrict o, NumSym_f scan_numsym,
		SAU_Ramp *restrict ramp, bool mult) {
	ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SAU_RAMPP_TIME) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t type = ramp->type; // has default
	if ((ramp->flags & SAU_RAMPP_GOAL) != 0) {
		// allow partial change
		if (((ramp->flags & SAU_RAMPP_GOAL_RATIO) != 0) == mult) {
			goal = true;
			vt = ramp->vt;
		}
		time_ms = ramp->time_ms;
	}
	for (;;) {
		uint8_t c = SAU_Scanner_getc(o);
		switch (c) {
		case SAU_SCAN_SPACE:
		case SAU_SCAN_LNBRK:
			break;
		case 'c': {
			size_t id;
			if (scan_symafind(o, sl->ramp_names,
					&id, "ramp curve")) {
				type = id;
			}
			break; }
		case 't':
			if (scan_time_val(o, &time_ms))
				time_set = true;
			break;
		case 'v':
			if (scan_num(o, scan_numsym, &vt))
				goal = true;
			break;
		case '}':
			goto RETURN;
		default:
			if (!handle_unknown_or_eof(o, c)) {
				warn_eof_without_closing(o, '}');
				goto RETURN;
			}
			break;
		}
	}
RETURN:
	if (!goal) {
		SAU_Scanner_warning(o, NULL,
				"ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->type = type;
	ramp->flags |= SAU_RAMPP_GOAL;
	if (mult)
		ramp->flags |= SAU_RAMPP_GOAL_RATIO;
	else
		ramp->flags &= ~SAU_RAMPP_GOAL_RATIO;
	if (time_set)
		ramp->flags |= SAU_RAMPP_TIME;
	else
		ramp->flags &= ~SAU_RAMPP_TIME;
	return true;
}

/*
 * Parser
 */

typedef struct SAU_Parser {
	ScanLookup sl;
	SAU_Scanner *sc;
	SAU_SymTab *st;
	SAU_MemPool *mp;
	uint32_t call_level;
	/* node state */
	SAU_ParseEvData *events;
	SAU_ParseEvData *last_event;
} SAU_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SAU_Parser *restrict o) {
	SAU_destroy_Scanner(o->sc);
	SAU_destroy_SymTab(o->st);
	SAU_destroy_MemPool(o->mp);
}

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 *
 * \return true, or false on allocation failure
 */
static bool init_Parser(SAU_Parser *restrict o) {
	SAU_MemPool *mp = SAU_create_MemPool(0);
	SAU_SymTab *st = SAU_create_SymTab(mp);
	SAU_Scanner *sc = SAU_create_Scanner(st);
	*o = (SAU_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mp = mp;
	if (!sc || !st || !mp) goto ERROR;
	if (!init_ScanLookup(&o->sl, st)) goto ERROR;
	sc->data = &o->sl;
	return true;
ERROR:
	fini_Parser(o);
	return false;
}

/*
 * Scope values.
 */
enum {
	SCOPE_TOP = 0,
	SCOPE_BIND,
	SCOPE_NEST,
};

/*
 * Current "location" (what is being parsed/worked on) for parse level.
 */
enum {
	SDPL_IN_NONE = 0, // no target for parameters
	SDPL_IN_DEFAULTS, // adjusting default values
	SDPL_IN_EVENT,    // adjusting operator and/or voice
};

/*
 * Parse level flags.
 */
enum {
	SDPL_BIND_MULTIPLE = 1<<0, // previous node interpreted as set of nodes
	SDPL_NESTED_SCOPE = 1<<1,
	SDPL_ACTIVE_EV = 1<<2,
	SDPL_ACTIVE_OP = 1<<3,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
typedef struct ParseLevel {
	SAU_Parser *o;
	struct ParseLevel *parent;
	uint32_t pl_flags;
	uint8_t location;
	uint8_t scope;
	SAU_ParseEvData *event, *last_event;
	SAU_ParseOpData *operator, *first_operator, *last_operator;
	SAU_ParseOpData *parent_op, *op_prev;
	uint8_t linktype;
	uint8_t last_linktype; /* FIXME: kludge */
	SAU_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SAU_ParseEvData *group_from; /* where to begin for group_events() */
	SAU_ParseEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

static bool parse_waittime(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SAU_Scanner_tryc(sc, 't')) {
		if (!pl->last_operator) {
			SAU_Scanner_warning(sc, NULL,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SAU_SDEV_ADD_WAIT_DURATION;
	} else {
		uint32_t wait_ms;
		if (!scan_time_val(sc, &wait_ms))
			return false;
		pl->next_wait_ms += wait_ms;
	}
	return true;
}

/*
 * Node- and scope-handling functions
 */

enum {
	/* node list/node link types */
	NL_REFER = 0,
	NL_GRAPH,
	NL_FMODS,
	NL_PMODS,
	NL_AMODS,
};

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SAU_ParseOpData *restrict op) {
	size_t i;
	SAU_ParseOpData **ops;
	ops = (SAU_ParseOpData**) SAU_PtrArr_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrArr_clear(&op->fmods);
	ops = (SAU_ParseOpData**) SAU_PtrArr_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrArr_clear(&op->pmods);
	ops = (SAU_ParseOpData**) SAU_PtrArr_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrArr_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SAU_ParseEvData *restrict e) {
	size_t i;
	SAU_ParseOpData **ops;
	ops = (SAU_ParseOpData**) SAU_PtrArr_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrArr_clear(&e->operators);
	free(e);
}

static void end_operator(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_OP;
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_ParseOpData *op = pl->operator;
	if (SAU_Ramp_ENABLED(&op->freq))
		op->op_params |= SAU_POPP_FREQ;
	if (SAU_Ramp_ENABLED(&op->freq2))
		op->op_params |= SAU_POPP_FREQ2;
	if (SAU_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SAU_POPP_AMP;
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SAU_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SAU_POPP_AMP2;
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			op->amp2.v0 *= sl->sopt.ampmult;
			op->amp2.vt *= sl->sopt.ampmult;
		}
	}
	SAU_ParseOpData *pop = op->op_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params |= SAU_POPP_ADJCS |
			SAU_POPP_WAVE |
			SAU_POPP_TIME |
			SAU_POPP_SILENCE |
			SAU_POPP_FREQ |
			SAU_POPP_FREQ2 |
			SAU_POPP_PHASE |
			SAU_POPP_AMP |
			SAU_POPP_AMP2;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SAU_POPP_WAVE;
		/* SAU_TIME set when time set */
		if (op->silence_ms != 0)
			op->op_params |= SAU_POPP_SILENCE;
		/* SAU_PHASE set when phase set */
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_EV;
	SAU_ParseEvData *e = pl->event;
	end_operator(pl);
	if (SAU_Ramp_ENABLED(&e->pan))
		e->vo_params |= SAU_PVOP_PAN;
	SAU_ParseEvData *pve = e->vo_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
		e->vo_params |= SAU_PVOP_PAN;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl,
		bool is_composite) {
	SAU_Parser *o = pl->o;
	SAU_ParseEvData *e;
	end_event(pl);
	pl->event = calloc(1, sizeof(SAU_ParseEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SAU_Ramp_reset(&e->pan);
	if (pl->op_prev != NULL) {
		SAU_ParseEvData *pve = pl->op_prev->event;
		pve->ev_flags |= SAU_SDEV_VOICE_LATER_USED;
		if (is_composite) {
			if (!pl->composite) {
				pve->composite = e;
				pl->composite = pve;
			} else {
				pve->next = e;
			}
		} else if (pve->composite != NULL) {
			SAU_ParseEvData *last_ce;
			for (last_ce = pve->composite; last_ce->next;
					last_ce = last_ce->next) ;
			last_ce->ev_flags |= SAU_SDEV_VOICE_LATER_USED;
		}
		e->vo_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SAU_RAMPP_STATE;
	}
	if (!pl->group_from)
		pl->group_from = e;
	if (!is_composite) {
		if (!o->events)
			o->events = e;
		else
			o->last_event->next = e;
		o->last_event = e;
		pl->composite = NULL;
	}
	pl->pl_flags |= SDPL_ACTIVE_EV;
}

static void begin_operator(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_ParseEvData *e = pl->event;
	SAU_ParseOpData *op, *pop = pl->op_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(pl);
	pl->operator = calloc(1, sizeof(SAU_ParseOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	op->time.v_ms = sl->sopt.def_time_ms; /* time is not copied */
	SAU_Ramp_reset(&op->freq);
	SAU_Ramp_reset(&op->freq2);
	SAU_Ramp_reset(&op->amp);
	SAU_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		pop->op_flags |= SAU_SDOP_LATER_USED;
		op->op_prev = pop;
		op->op_flags = pop->op_flags &
			(SAU_SDOP_NESTED | SAU_SDOP_MULTIPLE);
		if (is_composite) {
			pop->op_flags |= SAU_SDOP_HAS_COMPOSITE;
		} else {
			op->time.flags |= SAU_TIMEP_SET;
		}
		op->wave = pop->wave;
		op->phase = pop->phase;
		SAU_PtrArr_soft_copy(&op->fmods, &pop->fmods);
		SAU_PtrArr_soft_copy(&op->pmods, &pop->pmods);
		SAU_PtrArr_soft_copy(&op->amods, &pop->amods);
		if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
			SAU_ParseOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SAU_SDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SAU_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SAU_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SAU_RAMPP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	if (linktype == NL_REFER ||
			linktype == NL_GRAPH) {
		SAU_PtrArr_add(&e->operators, op);
		if (linktype == NL_GRAPH) {
			e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
			op->op_flags |= SAU_SDOP_NEW_CARRIER;
		}
	} else {
		SAU_PtrArr *list = NULL;
		switch (linktype) {
		case NL_FMODS:
			list = &pl->parent_op->fmods;
			break;
		case NL_PMODS:
			list = &pl->parent_op->pmods;
			break;
		case NL_AMODS:
			list = &pl->parent_op->amods;
			break;
		}
		pl->parent_op->op_params |= SAU_POPP_ADJCS;
		SAU_PtrArr_add(list, op);
	}
	/*
	 * Assign label. If no new label but previous node
	 * (for a non-composite) has one, update label to
	 * point to new node, but keep pointer in previous node.
	 */
	if (pl->set_label != NULL) {
		op->label = pl->set_label;
		op->label->data = op;
		pl->set_label = NULL;
	} else if (!is_composite && pop != NULL && pop->label != NULL) {
		op->label = pop->label;
		op->label->data = op;
	}
	pl->pl_flags |= SDPL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
		SAU_ParseOpData *restrict previous,
		uint8_t linktype, bool is_composite) {
	pl->op_prev = previous;
	if (!pl->event ||
			pl->location != SDPL_IN_EVENT
			/* previous event implicitly ended */ ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, is_composite);
	begin_operator(pl, linktype, is_composite);
	pl->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SAU_Parser *restrict o, ParseLevel *restrict pl,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	*pl = (ParseLevel){0};
	pl->o = o;
	pl->scope = newscope;
	if (parent_pl != NULL) {
		pl->parent = parent_pl;
		pl->pl_flags = parent_pl->pl_flags &
			(SDPL_NESTED_SCOPE | SDPL_BIND_MULTIPLE);
		pl->location = parent_pl->location;
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		pl->parent_op = parent_pl->parent_op;
		if (newscope == SCOPE_BIND)
			pl->group_from = parent_pl->group_from;
		if (newscope == SCOPE_NEST) {
			pl->pl_flags |= SDPL_NESTED_SCOPE;
			pl->parent_op = parent_pl->operator;
		}
	}
	pl->linktype = linktype;
}

static void end_scope(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	end_operator(pl);
	if (pl->scope == SCOPE_BIND) {
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
			begin_node(pl->parent, pl->first_operator,
					pl->parent->last_linktype, false);
		}
	} else if (!pl->parent) {
		/*
		 * At end of top scope, ie. at end of script -
		 * end last event and adjust timing.
		 */
		SAU_ParseEvData *group_to;
		end_event(pl);
		group_to = (pl->composite) ? pl->composite : pl->last_event;
		if (group_to)
			group_to->groupfrom = pl->group_from;
	}
	if (pl->set_label != NULL) {
		SAU_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_Scanner *sc = o->sc;
	pl->location = SDPL_IN_DEFAULTS;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case 'a':
			if (scan_num(sc, NULL, &sl->sopt.ampmult))
				sl->sopt.changed |= SAU_SOPT_AMPMULT;
			break;
		case 'f':
			if (scan_num(sc, scan_note, &sl->sopt.def_freq))
				sl->sopt.changed |= SAU_SOPT_DEF_FREQ;
			break;
		case 'n': {
			float freq;
			if (scan_num(sc, NULL, &freq)) {
				if (freq < 1.f) {
					SAU_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
					break;
				}
				sl->sopt.A4_freq = freq;
				sl->sopt.changed |= SAU_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.changed |= SAU_SOPT_DEF_RATIO;
			break;
		case 't':
			if (scan_time_val(sc, &sl->sopt.def_time_ms))
				sl->sopt.changed |= SAU_SOPT_DEF_TIME;
			break;
		default:
			goto UNKNOWN;
		}
	}
	return false;
UNKNOWN:
	SAU_Scanner_ungetc(sc);
	return true; /* let parse_level() take care of it */
}

static bool parse_level(SAU_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
	if (SAU_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, NULL, &op->amp, false);
	} else {
		scan_ramp_state(sc, NULL, &op->amp, false);
	}
	if (SAU_Scanner_tryc(sc, ',')) {
		if (SAU_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, NULL, &op->amp2, false);
		} else {
			scan_ramp_state(sc, NULL, &op->amp2, false);
		}
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		if (op->amods.count > 0) {
			op->op_params |= SAU_POPP_ADJCS;
			SAU_PtrArr_clear(&op->amods);
		}
		parse_level(o, pl, NL_AMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	if (SAU_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, numsym_f, &op->freq, rel_freq);
	} else {
		scan_ramp_state(sc, numsym_f, &op->freq, rel_freq);
	}
	if (SAU_Scanner_tryc(sc, ',')) {
		if (SAU_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
		} else {
			scan_ramp_state(sc, numsym_f, &op->freq2, rel_freq);
		}
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		if (op->fmods.count > 0) {
			op->op_params |= SAU_POPP_ADJCS;
			SAU_PtrArr_clear(&op->fmods);
		}
		parse_level(o, pl, NL_FMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SAU_POPP_PHASE;
	}
	if (SAU_Scanner_tryc(sc, '+') && SAU_Scanner_tryc(sc, '[')) {
		if (op->pmods.count > 0) {
			op->op_params |= SAU_POPP_ADJCS;
			SAU_PtrArr_clear(&op->pmods);
		}
		parse_level(o, pl, NL_PMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_step(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseEvData *e = pl->event;
	SAU_ParseOpData *op = pl->operator;
	pl->location = SDPL_IN_EVENT;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case 'P':
			if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
				goto UNKNOWN;
			if (SAU_Scanner_tryc(sc, '{')) {
				scan_ramp(sc, NULL, &e->pan, false);
			} else {
				scan_ramp_state(sc, NULL, &e->pan, false);
			}
			break;
		case '\\':
			if (parse_waittime(pl)) {
				begin_node(pl, pl->operator, NL_REFER, false);
			}
			break;
		case 'a':
			if (parse_ev_amp(pl)) goto UNKNOWN;
			break;
		case 'f':
			if (parse_ev_freq(pl, false)) goto UNKNOWN;
			break;
		case 'p':
			if (parse_ev_phase(pl)) goto UNKNOWN;
			break;
		case 'r':
			if (parse_ev_freq(pl, true)) goto UNKNOWN;
			break;
		case 's':
			scan_time_val(sc, &op->silence_ms);
			break;
		case 't':
			if (SAU_Scanner_tryc(sc, '*')) {
				/* later fitted or kept to default */
				op->time.v_ms = o->sl.sopt.def_time_ms;
				op->time.flags = 0;
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_SDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->time.flags |= SAU_TIMEP_SET
					| SAU_TIMEP_LINKED;
			} else {
				if (!scan_time_val(sc, &op->time.v_ms))
					break;
				op->time.flags = SAU_TIMEP_SET;
			}
			op->op_params |= SAU_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			break; }
		default:
			goto UNKNOWN;
		}
	}
	return false;
UNKNOWN:
	SAU_Scanner_ungetc(sc);
	return true; /* let parse_level() take care of it */
}

enum {
	HANDLE_DEFER = 1<<1,
	DEFERRED_STEP = 1<<2,
	DEFERRED_SETTINGS = 1<<4
};
static bool parse_level(SAU_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	ParseLevel pl;
	SAU_SymStr *label;
	uint8_t flags = 0;
	bool endscope = false;
	begin_scope(o, &pl, parent_pl, linktype, newscope);
	++o->call_level;
	SAU_Scanner *sc = o->sc;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case SAU_SCAN_LNBRK:
			if (pl.scope == SCOPE_TOP) {
				/*
				 * On top level of script,
				 * each line has a new "subscope".
				 */
				if (o->call_level > 1)
					goto RETURN;
				flags = 0;
				pl.location = SDPL_IN_NONE;
				pl.first_operator = NULL;
			}
			break;
		case '\'':
			/*
			 * Label assignment (set to what follows).
			 */
			if (pl.set_label != NULL) {
				SAU_Scanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			pl.set_label = label = scan_label(sc, c);
			break;
		case ';':
			if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
				goto INVALID;
			begin_node(&pl, pl.operator, NL_REFER, true);
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break;
		case '@':
			if (SAU_Scanner_tryc(sc, '[')) {
				end_operator(&pl);
				if (parse_level(o, &pl, pl.linktype, SCOPE_BIND))
					goto RETURN;
				/*
				 * Multiple-operator node now open.
				 */
				flags = parse_step(&pl) ?
					(HANDLE_DEFER | DEFERRED_STEP) :
					0;
				break;
			}
			/*
			 * Label reference (get and use value).
			 */
			if (pl.set_label != NULL) {
				SAU_Scanner_warning(sc, NULL,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.location = SDPL_IN_NONE;
			label = scan_label(sc, c);
			if (label != NULL) {
				SAU_ParseOpData *ref = label->data;
				if (!ref)
					SAU_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_node(&pl, ref, NL_REFER, false);
					flags = parse_step(&pl) ?
						(HANDLE_DEFER | DEFERRED_STEP) :
						0;
				}
			}
			break;
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(&pl, 0, pl.linktype, false);
			pl.operator->wave = wave;
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			flags = parse_settings(&pl) ?
				(HANDLE_DEFER | DEFERRED_SETTINGS) :
				0;
			break;
		case '[':
			if (parse_level(o, &pl, pl.linktype, SCOPE_NEST))
				goto RETURN;
			break;
		case '\\':
			if (pl.location == SDPL_IN_DEFAULTS ||
					((pl.pl_flags & SDPL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			parse_waittime(&pl);
			break;
		case ']':
			if (pl.scope == SCOPE_BIND) {
				endscope = true;
				goto RETURN;
			}
			if (pl.scope == SCOPE_NEST) {
				end_operator(&pl);
				endscope = true;
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '|':
			if (pl.location == SDPL_IN_DEFAULTS ||
					((pl.pl_flags & SDPL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			if (!pl.event) {
				SAU_Scanner_warning(sc, NULL,
"end of sequence before any parts given");
				break;
			}
			if (pl.group_from != NULL) {
				SAU_ParseEvData *group_to = (pl.composite) ?
					pl.composite :
					pl.event;
				group_to->groupfrom = pl.group_from;
				pl.group_from = NULL;
			}
			end_event(&pl);
			pl.location = SDPL_IN_NONE;
			break;
		case '}':
			warn_closing_without_opening(sc, '}', '{');
			break;
		default:
		INVALID:
			if (!handle_unknown_or_eof(sc, c)) goto FINISH;
			break;
		}
		/* Return to sub-parsing routines. */
		if (flags != 0 && !(flags & HANDLE_DEFER)) {
			uint8_t test = flags;
			flags = 0;
			if ((test & DEFERRED_STEP) != 0) {
				if (parse_step(&pl))
					flags = HANDLE_DEFER | DEFERRED_STEP;
			} else if ((test & DEFERRED_SETTINGS) != 0)
				if (parse_settings(&pl))
					flags = HANDLE_DEFER | DEFERRED_SETTINGS;
		}
		flags &= ~HANDLE_DEFER;
	}
FINISH:
	if (newscope == SCOPE_NEST || newscope == SCOPE_BIND)
		warn_eof_without_closing(sc, ']');
RETURN:
	end_scope(&pl);
	--o->call_level;
	/* Should return from calling scope if/when parent scope is ended. */
	return (endscope && pl.scope != newscope);
}

/*
 * Process file.
 *
 * \return name of script, or NULL on error preventing parse
 */
static const char *parse_file(SAU_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SAU_Scanner *sc = o->sc;
	const char *name;
	if (!SAU_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, NULL, NL_GRAPH, SCOPE_TOP);
	name = sc->f->path;
	SAU_Scanner_close(sc);
	return name;
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SAU_Parse* SAU_create_Parse(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SAU_Parser pr;
	SAU_Parse *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	o = calloc(1, sizeof(SAU_Parse));
	o->events = pr.events;
	o->name = name;
	o->sopt = pr.sl.sopt;
DONE:
	fini_Parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Parse(SAU_Parse *restrict o) {
	if (!o)
		return;
	SAU_ParseEvData *e;
	for (e = o->events; e != NULL; ) {
		SAU_ParseEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
