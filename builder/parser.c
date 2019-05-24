/* sgensys: Script file parser.
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
#include "symtab.h"
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
	SGS_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*rac_names;
} ScanLookup;

/*
 * Default script options, used until changed in a script.
 */
static const SGS_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
};

static bool init_ScanLookup(ScanLookup *restrict o, SGS_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SGS_SymTab_pool_stra(st,
			SGS_Wave_names, SGS_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->rac_names = SGS_SymTab_pool_stra(st,
			SGS_RampCurve_names, SGS_RAC_TYPES);
	if (!o->rac_names)
		return false;
	return true;
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool handle_unknown_or_eof(SGS_Scanner *restrict o, uint8_t c) {
	if (c == 0)
		return false;
	if (IS_VISIBLE(c)) {
		SGS_Scanner_warning(o, NULL,
				"invalid character '%c'", c);
	} else {
		SGS_Scanner_warning(o, NULL,
				"invalid character (value 0x%02hhX)", c);
	}
	return true;
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_eof_without_closing(SGS_Scanner *restrict o, uint8_t c) {
	SGS_Scanner_warning(o, NULL, "end of file without closing '%c'", c);
}

/*
 * Print warning for scope-closing character without scope-opening character.
 */
static void warn_closing_without_opening(SGS_Scanner *restrict o,
		uint8_t close_c, uint8_t open_c) {
	SGS_Scanner_warning(o, NULL, "closing '%c' without opening '%c'",
			close_c, open_c);
}

typedef float (*NumSym_f)(SGS_Scanner *restrict o);

typedef struct NumParser {
	SGS_Scanner *sc;
	NumSym_f numsym_f;
	SGS_ScanFrame sf_start;
	bool has_infnum;
} NumParser;
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
	SGS_Scanner *sc = o->sc;
	double num;
	bool minus = false;
	uint8_t c;
	if (level > 0) SGS_Scanner_skipws(sc);
	c = SGS_Scanner_getc(sc);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		SGS_Scanner_skipws(sc);
		c = SGS_Scanner_getc(sc);
	}
	if (c == '(') {
		num = scan_num_r(o, 255, level+1);
		if (minus) num = -num;
		if (level == 0)
			return num;
		goto EVAL;
	}
	if (o->numsym_f && IS_ALPHA(c)) {
		SGS_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (isnan(num))
			return NAN;
		if (minus) num = -num;
	} else {
		size_t read_len;
		SGS_Scanner_ungetc(sc);
		SGS_Scanner_getd(sc, &num, false, &read_len);
		if (read_len == 0)
			return NAN;
		if (minus) num = -num;
	}
EVAL:
	if (pri == 0)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		if (level > 0) SGS_Scanner_skipws(sc);
		c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
		case SGS_SCAN_LNBRK:
			break;
		case '(':
			num *= scan_num_r(o, 255, level+1);
			break;
		case ')':
			if (pri < 255) goto DEFER;
			return num;
		case '^':
			num = exp(log(num) * scan_num_r(o, 0, level));
			break;
		case '*':
			num *= scan_num_r(o, 1, level);
			break;
		case '/':
			num /= scan_num_r(o, 1, level);
			break;
		case '+':
			if (pri <= 2) goto DEFER;
			num += scan_num_r(o, 2, level);
			break;
		case '-':
			if (pri <= 2) goto DEFER;
			num -= scan_num_r(o, 2, level);
			break;
		default:
			if (pri == 255) {
				SGS_Scanner_warning(sc, &o->sf_start,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SGS_Scanner_ungetc(sc);
	return num;
}
static bool SGS__noinline scan_num(SGS_Scanner *restrict o,
		NumSym_f scan_numsym, float *restrict var) {
	NumParser np = {o, scan_numsym, o->sf, false};
	float num = scan_num_r(&np, 0, 0);
	if (isnan(num))
		return false;
	if (isinf(num)) np.has_infnum = true;
	if (np.has_infnum) {
		SGS_Scanner_warning(o, &np.sf_start,
				"discarding expression with infinite number");
		return false;
	}
	*var = num;
	return true;
}

static bool scan_time(SGS_Scanner *restrict o, float *restrict var) {
	SGS_ScanFrame sf = o->sf;
	float num;
	if (!scan_num(o, NULL, &num))
		return false;
	if (num < 0.f) {
		SGS_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*var = num;
	return true;
}

#define OCTAVES 11
static float scan_note(SGS_Scanner *restrict o) {
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
	uint8_t c = SGS_Scanner_getc(o);
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	size_t read_len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = SGS_Scanner_getc(o);
	}
	if (c < 'A' || c > 'G') {
		SGS_Scanner_warning(o, NULL,
"invalid note specified - should be C, D, E, F, G, A or B");
		return NAN;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = SGS_Scanner_getc(o);
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else
		SGS_Scanner_ungetc(o);
	SGS_Scanner_geti(o, &octave, false, &read_len);
	if (read_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SGS_Scanner_warning(o, NULL,
"invalid octave specified for note - valid range 0-10");
		octave = 4;
	}
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f +
			(notes[semitone][note+1] / notes[semitone][note] -
				1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static const char *scan_label(SGS_Scanner *restrict o,
		size_t *restrict lenp, char op) {
	const void *s = NULL;
	SGS_Scanner_getsymstr(o, &s, lenp);
	if (*lenp == 0) {
		SGS_Scanner_warning(o, NULL,
				"ignoring %c without label name", op);
	}
	return s;
}

static bool scan_symafind(SGS_Scanner *restrict o,
		const char *const*restrict stra, size_t n,
		size_t *restrict found_i, const char *restrict print_type) {
	SGS_ScanFrame sf_begin = o->sf;
	const void *key = NULL;
	size_t len;
	SGS_Scanner_getsymstr(o, &key, &len);
	if (len == 0) {
		SGS_Scanner_warning(o, NULL, "%s missing", print_type);
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		if (stra[i] == key) {
			*found_i = i;
			return true;
		}
	}
	SGS_Scanner_warning(o, &sf_begin,
			"invalid %s; available types are:", print_type);
	fprintf(stderr, "\t%s", stra[0]);
	for (size_t i = 1; i < n; ++i) {
		fprintf(stderr, ", %s", stra[i]);
	}
	putc('\n', stderr);
	return false;
}

static bool scan_wavetype(SGS_Scanner *restrict o, size_t *restrict found_id) {
	ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names, SGS_WAVE_TYPES,
			found_id, "wave type");
}

static bool scan_ramp_state(SGS_Scanner *restrict o, NumSym_f scan_numsym,
		SGS_Ramp *restrict ramp, bool mult) {
	if (!scan_num(o, scan_numsym, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SGS_RAMP_STATE_RATIO;
	} else {
		ramp->flags &= ~SGS_RAMP_STATE_RATIO;
	}
	ramp->flags |= SGS_RAMP_STATE;
	return true;
}

static bool scan_ramp(SGS_Scanner *restrict o, NumSym_f scan_numsym,
		SGS_Ramp *restrict ramp, bool mult) {
	ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SGS_RAMP_TIME_SET) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t curve = ramp->curve; // has default
	if ((ramp->flags & SGS_RAMP_CURVE) != 0) {
		// allow partial change
		if (((ramp->flags & SGS_RAMP_CURVE_RATIO) != 0) == mult) {
			goal = true;
			vt = ramp->vt;
		}
		time_ms = ramp->time_ms;
	}
	for (;;) {
		uint8_t c = SGS_Scanner_getc_nospace(o);
		switch (c) {
		case SGS_SCAN_LNBRK:
			break;
		case 'c': {
			size_t type;
			if (scan_symafind(o, sl->rac_names, SGS_RAC_TYPES,
					&type, "curve type")) {
				curve = type;
			}
			break; }
		case 't': {
			float time;
			if (scan_time(o, &time)) {
				time_ms = lrint(time * 1000.f);
				time_set = true;
			}
			break; }
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
		SGS_Scanner_warning(o, NULL,
				"ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->curve = curve;
	ramp->flags |= SGS_RAMP_CURVE;
	if (mult)
		ramp->flags |= SGS_RAMP_CURVE_RATIO;
	else
		ramp->flags &= ~SGS_RAMP_CURVE_RATIO;
	if (time_set)
		ramp->flags |= SGS_RAMP_TIME_SET;
	else
		ramp->flags &= ~SGS_RAMP_TIME_SET;
	return true;
}

/*
 * Parser
 */

typedef struct SGS_Parser {
	ScanLookup sl;
	SGS_Scanner *sc;
	SGS_SymTab *st;
	uint32_t call_level;
	/* node state */
	SGS_ParseEvData *events;
	SGS_ParseEvData *last_event;
} SGS_Parser;

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static void init_Parser(SGS_Parser *restrict o) {
	*o = (SGS_Parser){0};
	o->st = SGS_create_SymTab();
	init_ScanLookup(&o->sl, o->st);
	o->sc = SGS_create_Scanner(o->st);
	o->sc->data = &o->sl;
}

/*
 * Finalize parser instance.
 */
static void fini_Parser(SGS_Parser *restrict o) {
	SGS_destroy_Scanner(o->sc);
	SGS_destroy_SymTab(o->st);
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
	SGS_Parser *o;
	struct ParseLevel *parent;
	uint32_t pl_flags;
	uint8_t location;
	uint8_t scope;
	SGS_ParseEvData *event, *last_event;
	SGS_ParseOpData *operator, *first_operator, *last_operator;
	SGS_ParseOpData *parent_op, *op_prev;
	uint8_t linktype;
	uint8_t last_linktype; /* FIXME: kludge */
	const char *set_label; /* label assigned to next node */
	/* timing/delay */
	SGS_ParseEvData *group_from; /* where to begin for group_events() */
	SGS_ParseEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

static bool parse_waittime(ParseLevel *restrict pl) {
	SGS_Parser *o = pl->o;
	SGS_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SGS_Scanner_tryc(sc, 't')) {
		if (!pl->last_operator) {
			SGS_Scanner_warning(sc, NULL,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SGS_SDEV_ADD_WAIT_DURATION;
	} else {
		float wait;
		uint32_t wait_ms;
		if (scan_time(sc, &wait)) {
			wait_ms = lrint(wait * 1000.f);
			pl->next_wait_ms += wait_ms;
		}
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
static void destroy_operator(SGS_ParseOpData *restrict op) {
	size_t i;
	SGS_ParseOpData **ops;
	ops = (SGS_ParseOpData**) SGS_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrList_clear(&op->fmods);
	ops = (SGS_ParseOpData**) SGS_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrList_clear(&op->pmods);
	ops = (SGS_ParseOpData**) SGS_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SGS_ParseEvData *restrict e) {
	size_t i;
	SGS_ParseOpData **ops;
	ops = (SGS_ParseOpData**) SGS_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrList_clear(&e->operators);
	free(e);
}

static void end_operator(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_OP;
	SGS_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SGS_ParseOpData *op = pl->operator;
	if (SGS_Ramp_ENABLED(&op->freq))
		op->op_params |= SGS_POPP_FREQ;
	if (SGS_Ramp_ENABLED(&op->freq2))
		op->op_params |= SGS_POPP_FREQ2;
	if (SGS_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SGS_POPP_AMP;
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SGS_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SGS_POPP_AMP2;
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			op->amp2.v0 *= sl->sopt.ampmult;
			op->amp2.vt *= sl->sopt.ampmult;
		}
	}
	SGS_ParseOpData *pop = op->op_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params |= SGS_POPP_ADJCS |
			SGS_POPP_WAVE |
			SGS_POPP_TIME |
			SGS_POPP_SILENCE |
			SGS_POPP_FREQ |
			SGS_POPP_FREQ2 |
			SGS_POPP_PHASE |
			SGS_POPP_AMP |
			SGS_POPP_AMP2;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SGS_POPP_WAVE;
		/* SGS_TIME set when time set */
		if (op->silence_ms != 0)
			op->op_params |= SGS_POPP_SILENCE;
		/* SGS_PHASE set when phase set */
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_EV;
	SGS_ParseEvData *e = pl->event;
	end_operator(pl);
	if (SGS_Ramp_ENABLED(&e->pan))
		e->vo_params |= SGS_PVOP_PAN;
	SGS_ParseEvData *pve = e->vo_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
		e->vo_params |= SGS_PVOP_PAN;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SGS_Parser *o = pl->o;
	SGS_ParseEvData *e, *pve;
	end_event(pl);
	pl->event = calloc(1, sizeof(SGS_ParseEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SGS_Ramp_reset(&e->pan);
	if (pl->op_prev != NULL) {
		pve = pl->op_prev->event;
		pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
		if (pve->composite != NULL && !is_composite) {
			SGS_ParseEvData *last_ce = pve->composite;
			while (last_ce->next != NULL)
				last_ce = last_ce->next;
			last_ce->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
		}
		e->vo_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SGS_RAMP_STATE;
	}
	if (!pl->group_from)
		pl->group_from = e;
	if (is_composite) {
		if (!pl->composite) {
			pve->composite = e;
			pl->composite = pve;
		} else {
			pve->next = e;
		}
	} else {
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
	SGS_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SGS_ParseEvData *e = pl->event;
	SGS_ParseOpData *op, *pop = pl->op_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(pl);
	pl->operator = calloc(1, sizeof(SGS_ParseOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	SGS_Ramp_reset(&op->freq);
	SGS_Ramp_reset(&op->freq2);
	SGS_Ramp_reset(&op->amp);
	SGS_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		pop->op_flags |= SGS_SDOP_LATER_USED;
		op->op_prev = pop;
		op->op_flags = pop->op_flags &
			(SGS_SDOP_NESTED | SGS_SDOP_MULTIPLE);
		if (is_composite) {
			/*
			 * Context-sensitive time default.
			 */
			pop->op_flags |= SGS_SDOP_HAS_COMPOSITE;
			op->op_flags |= SGS_SDOP_TIME_DEFAULT;
		}
		op->time_ms = pop->time_ms;
		op->wave = pop->wave;
		op->phase = pop->phase;
		SGS_PtrList_soft_copy(&op->fmods, &pop->fmods);
		SGS_PtrList_soft_copy(&op->pmods, &pop->pmods);
		SGS_PtrList_soft_copy(&op->amods, &pop->amods);
		if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
			SGS_ParseOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time_ms)
					max_time = mpop->time_ms;
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SGS_SDOP_MULTIPLE;
			op->time_ms = max_time;
			pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 *
		 * time default: depends on context
		 */
		op->op_flags = SGS_SDOP_TIME_DEFAULT;
		op->time_ms = sl->sopt.def_time_ms;
		if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SGS_SDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SGS_RAMP_STATE_RATIO;
		}
		op->freq.flags |= SGS_RAMP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SGS_RAMP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either to the
	 * current event node, or to an operator node (ordinary or multiple)
	 * in the case of operator linking/nesting.
	 */
	if (linktype == NL_REFER || linktype == NL_GRAPH) {
		SGS_PtrList_add(&e->operators, op);
		if (linktype == NL_GRAPH) {
			e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
			op->op_flags |= SGS_SDOP_NEW_CARRIER;
		}
	} else {
		SGS_PtrList *list = NULL;
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
		pl->parent_op->op_params |= SGS_POPP_ADJCS;
		SGS_PtrList_add(list, op);
	}
	/*
	 * Assign label. If no new label but previous node (for a non-composite)
	 * has one, update label to point to new node, but keep pointer in
	 * previous node.
	 */
	if (pl->set_label != NULL) {
		SGS_SymTab_set(o->st, pl->set_label, strlen(pl->set_label), op);
		op->label = pl->set_label;
		pl->set_label = NULL;
	} else if (!is_composite && pop != NULL && pop->label != NULL) {
		SGS_SymTab_set(o->st, pop->label, strlen(pop->label), op);
		op->label = pop->label;
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
		SGS_ParseOpData *restrict previous,
		uint8_t linktype, bool is_composite) {
	pl->op_prev = previous;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->location != SDPL_IN_EVENT ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, linktype, is_composite);
	begin_operator(pl, linktype, is_composite);
	pl->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SGS_Parser *restrict o, ParseLevel *restrict pl,
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
	SGS_Parser *o = pl->o;
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
		 * At end of top scope (ie. at end of script),
		 * end last event and adjust timing.
		 */
		SGS_ParseEvData *group_to;
		end_event(pl);
		group_to = (pl->composite) ? pl->composite : pl->last_event;
		if (group_to)
			group_to->groupfrom = pl->group_from;
	}
	if (pl->set_label != NULL) {
		SGS_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
	SGS_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SGS_Scanner *sc = o->sc;
	pl->location = SDPL_IN_DEFAULTS;
	for (;;) {
		uint8_t c = SGS_Scanner_getc_nospace(sc);
		switch (c) {
		case 'a':
			if (scan_num(sc, NULL, &sl->sopt.ampmult))
				sl->sopt.changed |= SGS_SOPT_AMPMULT;
			break;
		case 'f':
			if (scan_num(sc, scan_note, &sl->sopt.def_freq))
				sl->sopt.changed |= SGS_SOPT_DEF_FREQ;
			break;
		case 'n': {
			float freq;
			if (scan_num(sc, NULL, &freq)) {
				if (freq < 1.f) {
					SGS_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
					break;
				}
				sl->sopt.A4_freq = freq;
				sl->sopt.changed |= SGS_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.changed |= SGS_SOPT_DEF_RATIO;
			break;
		case 't': {
			float time;
			if (scan_time(sc, &time)) {
				sl->sopt.def_time_ms = lrint(time * 1000.f);
				sl->sopt.changed |= SGS_SOPT_DEF_TIME;
			}
			break; }
		default:
			SGS_Scanner_ungetc(sc);
			return true; /* let parse_level() take care of it */
		}
	}
	return false;
}

static bool parse_level(SGS_Parser *restrict o, ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
	SGS_Parser *o = pl->o;
	SGS_Scanner *sc = o->sc;
	SGS_ParseOpData *op = pl->operator;
	if (SGS_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, NULL, &op->amp, false);
	} else {
		scan_ramp_state(sc, NULL, &op->amp, false);
	}
	if (SGS_Scanner_tryc(sc, ',')) {
		if (SGS_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, NULL, &op->amp2, false);
		} else {
			scan_ramp_state(sc, NULL, &op->amp2, false);
		}
	}
	if (SGS_Scanner_tryc(sc, '~') && SGS_Scanner_tryc(sc, '[')) {
		if (op->amods.count > 0) {
			op->op_params |= SGS_POPP_ADJCS;
			SGS_PtrList_clear(&op->amods);
		}
		parse_level(o, pl, NL_AMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
	SGS_Parser *o = pl->o;
	SGS_Scanner *sc = o->sc;
	SGS_ParseOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SGS_SDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	if (SGS_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, numsym_f, &op->freq, rel_freq);
	} else {
		scan_ramp_state(sc, numsym_f, &op->freq, rel_freq);
	}
	if (SGS_Scanner_tryc(sc, ',')) {
		if (SGS_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
		} else {
			scan_ramp_state(sc, numsym_f, &op->freq2, rel_freq);
		}
	}
	if (SGS_Scanner_tryc(sc, '~') && SGS_Scanner_tryc(sc, '[')) {
		if (op->fmods.count > 0) {
			op->op_params |= SGS_POPP_ADJCS;
			SGS_PtrList_clear(&op->fmods);
		}
		parse_level(o, pl, NL_FMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
	SGS_Parser *o = pl->o;
	SGS_Scanner *sc = o->sc;
	SGS_ParseOpData *op = pl->operator;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SGS_POPP_PHASE;
	}
	if (SGS_Scanner_tryc(sc, '+') && SGS_Scanner_tryc(sc, '[')) {
		if (op->pmods.count > 0) {
			op->op_params |= SGS_POPP_ADJCS;
			SGS_PtrList_clear(&op->pmods);
		}
		parse_level(o, pl, NL_PMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_step(ParseLevel *restrict pl) {
	SGS_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SGS_Scanner *sc = o->sc;
	SGS_ParseEvData *e = pl->event;
	SGS_ParseOpData *op = pl->operator;
	pl->location = SDPL_IN_EVENT;
	for (;;) {
		uint8_t c = SGS_Scanner_getc_nospace(sc);
		switch (c) {
		case 'P':
			if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
				goto UNKNOWN;
			if (SGS_Scanner_tryc(sc, '{')) {
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
		case 's': {
			float silence;
			if (scan_time(sc, &silence)) {
				op->silence_ms = lrint(silence * 1000.f);
			}
			break; }
		case 't':
			if (SGS_Scanner_tryc(sc, '*')) {
				/* later fitted or kept at default value */
				op->op_flags |= SGS_SDOP_TIME_DEFAULT;
				op->time_ms = sl->sopt.def_time_ms;
			} else if (SGS_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SGS_SDOP_NESTED)) {
					SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
				op->time_ms = SGS_TIME_INF;
			} else {
				float time;
				if (scan_time(sc, &time)) {
					op->op_flags &= ~SGS_SDOP_TIME_DEFAULT;
					op->time_ms = lrint(time * 1000.f);
				}
			}
			op->op_params |= SGS_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			break; }
		default:
		UNKNOWN:
			SGS_Scanner_ungetc(sc);
			return true; /* let parse_level() take care of it */
		}
	}
	return false;
}

enum {
	HANDLE_DEFER = 1<<1,
	DEFERRED_STEP = 1<<2,
	DEFERRED_SETTINGS = 1<<4
};
static bool parse_level(SGS_Parser *restrict o, ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	ParseLevel pl;
	const char *label;
	size_t label_len;
	uint8_t flags = 0;
	bool endscope = false;
	begin_scope(o, &pl, parent_pl, linktype, newscope);
	++o->call_level;
	SGS_Scanner *sc = o->sc;
	for (;;) {
		uint8_t c = SGS_Scanner_getc_nospace(sc);
		switch (c) {
		case SGS_SCAN_LNBRK:
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
				SGS_Scanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			label = scan_label(sc, &label_len, c);
			pl.set_label = label;
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
			if (SGS_Scanner_tryc(sc, '[')) {
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
				SGS_Scanner_warning(sc, NULL,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.location = SDPL_IN_NONE;
			label = scan_label(sc, &label_len, c);
			if (label_len > 0) {
				SGS_ParseOpData *ref;
				ref = SGS_SymTab_get(o->st, label, label_len);
				if (!ref)
					SGS_Scanner_warning(sc, NULL,
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
((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
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
((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
				goto INVALID;
			if (!pl.event) {
				SGS_Scanner_warning(sc, NULL,
"end of sequence before any parts given");
				break;
			}
			if (pl.group_from != NULL) {
				SGS_ParseEvData *group_to = (pl.composite) ?
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
static const char *parse_file(SGS_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SGS_Scanner *sc = o->sc;
	const char *name;
	if (!SGS_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, NULL, NL_GRAPH, SCOPE_TOP);
	name = sc->f->path;
	SGS_Scanner_close(sc);
	return name;
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SGS_Parse* SGS_create_Parse(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SGS_Parser pr;
	SGS_Parse *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	o = calloc(1, sizeof(SGS_Parse));
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
void SGS_destroy_Parse(SGS_Parse *restrict o) {
	if (!o)
		return;
	SGS_ParseEvData *e;
	for (e = o->events; e != NULL; ) {
		SGS_ParseEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
