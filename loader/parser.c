/* sgensys: Script parser module.
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

#include "scanner.h"
#include "../script.h"
#include "../help.h"
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

struct ScanLookup {
	SGS_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*ramp_names;
};

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

static bool init_ScanLookup(struct ScanLookup *restrict o,
		SGS_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SGS_SymTab_pool_stra(st,
			SGS_Wave_names, SGS_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->ramp_names = SGS_SymTab_pool_stra(st,
			SGS_Ramp_names, SGS_RAMP_TYPES);
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
static bool handle_unknown_or_eof(SGS_Scanner *restrict o, uint8_t c) {
	if (c == 0)
		return false;
	if (SGS_IS_ASCIIVISIBLE(c)) {
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
 * Print warning for scope-opening character in disallowed place.
 */
static void warn_opening_disallowed(SGS_Scanner *restrict o,
		uint8_t open_c) {
	SGS_Scanner_warning(o, NULL, "opening '%c' out of place",
			open_c);
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

struct NumParser {
	SGS_Scanner *sc;
	NumSym_f numsym_f;
	SGS_ScanFrame sf_start;
	bool has_infnum;
};
enum {
	NUMEXP_SUB = 0,
	NUMEXP_ADT,
	NUMEXP_MLT,
	NUMEXP_POW,
	NUMEXP_NUM,
};
static double scan_num_r(struct NumParser *restrict o,
		uint8_t pri, uint32_t level) {
	SGS_Scanner *sc = o->sc;
	double num;
	bool minus = false;
	uint8_t c;
	if (level == 1) SGS_Scanner_setws_level(sc, SGS_SCAN_WS_NONE);
	c = SGS_Scanner_getc(sc);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SGS_Scanner_getc(sc);
	}
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else if (o->numsym_f && IS_ALPHA(c)) {
		SGS_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (isnan(num))
			return NAN;
	} else {
		size_t read_len;
		SGS_Scanner_ungetc(sc);
		SGS_Scanner_getd(sc, &num, false, &read_len);
		if (read_len == 0)
			return NAN;
	}
	if (minus) num = -num;
	if (level == 0 || pri == NUMEXP_NUM)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		c = SGS_Scanner_getc(sc);
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
static sgsNoinline bool scan_num(SGS_Scanner *restrict o,
		NumSym_f scan_numsym, float *restrict var) {
	struct NumParser np = {o, scan_numsym, o->sf, false};
	uint8_t ws_level = o->ws_level;
	float num = scan_num_r(&np, NUMEXP_NUM, 0);
	SGS_Scanner_setws_level(o, ws_level); // restore if changed
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

static sgsNoinline bool scan_time_val(SGS_Scanner *restrict o,
		uint32_t *restrict val) {
	SGS_ScanFrame sf = o->sf;
	float val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		SGS_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = lrint(val_s * 1000.f);
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
	struct ScanLookup *sl = o->data;
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
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static SGS_SymStr *scan_label(SGS_Scanner *restrict o,
		char op) {
	SGS_SymStr *s = NULL;
	SGS_Scanner_get_symstr(o, &s);
	if (!s) {
		SGS_Scanner_warning(o, NULL,
				"ignoring %c without label name", op);
	}
	return s;
}

static bool scan_symafind(SGS_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SGS_ScanFrame sf_begin = o->sf;
	SGS_SymStr *s = NULL;
	SGS_Scanner_get_symstr(o, &s);
	if (!s) {
		SGS_Scanner_warning(o, NULL, "%s value missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SGS_Scanner_warning(o, &sf_begin,
			"invalid %s value; available are:", print_type);
	SGS_print_names(stra, "\t", stderr);
	return false;
}

static bool scan_wavetype(SGS_Scanner *restrict o, size_t *restrict found_id) {
	struct ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names, found_id, "wave type");
}

static bool scan_ramp_state(SGS_Scanner *restrict o,
		NumSym_f scan_numsym,
		SGS_Ramp *restrict ramp, bool ratio) {
	if (!scan_num(o, scan_numsym, &ramp->v0))
		return false;
	if (ratio) {
		ramp->flags |= SGS_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SGS_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SGS_RAMPP_STATE;
	return true;
}

static bool scan_ramp(SGS_Scanner *restrict o, NumSym_f scan_numsym,
		SGS_Ramp *restrict ramp, bool ratio) {
	if (!SGS_Scanner_tryc(o, '{')) {
		return scan_ramp_state(o, scan_numsym, ramp, ratio);
	}
	struct ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SGS_RAMPP_TIME) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t type = ramp->type; // has default
	if ((ramp->flags & SGS_RAMPP_GOAL) != 0) {
		// allow partial change
		if (((ramp->flags & SGS_RAMPP_GOAL_RATIO) != 0) == ratio) {
			goal = true;
			vt = ramp->vt;
		}
		time_ms = ramp->time_ms;
	}
	for (;;) {
		uint8_t c = SGS_Scanner_getc(o);
		switch (c) {
		case SGS_SCAN_SPACE:
		case SGS_SCAN_LNBRK:
			break;
		case 'c': {
			size_t id;
			if (scan_symafind(o, sl->ramp_names,
					&id, "ramp type")) {
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
		SGS_Scanner_warning(o, NULL,
				"ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->type = type;
	ramp->flags |= SGS_RAMPP_GOAL;
	if (ratio)
		ramp->flags |= SGS_RAMPP_GOAL_RATIO;
	else
		ramp->flags &= ~SGS_RAMPP_GOAL_RATIO;
	if (time_set)
		ramp->flags |= SGS_RAMPP_TIME;
	else
		ramp->flags &= ~SGS_RAMPP_TIME;
	return true;
}

/*
 * Parser
 */

typedef struct SGS_Parser {
	struct ScanLookup sl;
	SGS_Scanner *sc;
	SGS_SymTab *st;
	SGS_MemPool *mp;
	uint32_t call_level;
	/* node state */
	struct ParseLevel *cur_pl;
	SGS_ScriptEvData *events;
	SGS_ScriptEvData *last_event;
	SGS_ScriptEvData *group_start, *group_end;
} SGS_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SGS_Parser *restrict o) {
	SGS_destroy_Scanner(o->sc);
	SGS_destroy_SymTab(o->st);
	SGS_destroy_MemPool(o->mp);
}

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 *
 * \return true, or false on allocation failure
 */
static bool init_Parser(SGS_Parser *restrict o) {
	SGS_MemPool *mp = SGS_create_MemPool(0);
	SGS_SymTab *st = SGS_create_SymTab(mp);
	SGS_Scanner *sc = SGS_create_Scanner(st);
	*o = (SGS_Parser){0};
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
	SCOPE_SAME = 0,
	SCOPE_TOP = 1,
	SCOPE_BIND = '@',
	SCOPE_NEST = '[',
};

typedef void (*ParseLevel_sub_f)(SGS_Parser *restrict o);

static void parse_in_event(SGS_Parser *restrict o);
static void parse_in_settings(SGS_Parser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_DEFERRED_SUB   = 1<<0, // \a sub_f exited to attempt handling above
	PL_BIND_MULTIPLE  = 1<<1, // previous node interpreted as set of nodes
	PL_NESTED_SCOPE   = 1<<2,
	PL_NEW_EVENT_FORK = 1<<3,
	PL_ACTIVE_EV      = 1<<4,
	PL_ACTIVE_OP      = 1<<5,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
struct ParseLevel {
	struct ParseLevel *parent;
	ParseLevel_sub_f sub_f; // identifies "location" and implicit context
	uint32_t pl_flags;
	uint8_t scope;
	uint8_t use_type;
	uint8_t last_use_type; /* FIXME: kludge */
	SGS_ScriptEvData *event;
	SGS_ScriptOpData *operator, *first_operator, *last_operator;
	SGS_ScriptOpData *parent_on, *on_prev;
	SGS_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
};

typedef struct SGS_ScriptEvBranch {
	SGS_ScriptEvData *events;
	struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

static bool parse_waittime(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	uint32_t wait_ms;
	if (!scan_time_val(o->sc, &wait_ms))
		return false;
	pl->next_wait_ms += wait_ms;
	return true;
}

/*
 * Node- and scope-handling functions
 */

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SGS_ScriptOpData *restrict op) {
	SGS_PtrArr_clear(&op->on_next);
	size_t i;
	SGS_ScriptOpData **ops;
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrArr_clear(&op->fmods);
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrArr_clear(&op->pmods);
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrArr_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SGS_ScriptEvData *restrict e) {
	size_t i;
	SGS_ScriptOpData **ops;
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SGS_PtrArr_clear(&e->operators);
	SGS_PtrArr_clear(&e->op_graph);
	free(e);
}

static void end_operator(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~PL_ACTIVE_OP;
	SGS_ScriptOpData *op = pl->operator;
	if (SGS_Ramp_ENABLED(&op->freq))
		op->op_params |= SGS_POPP_FREQ;
	if (SGS_Ramp_ENABLED(&op->freq2))
		op->op_params |= SGS_POPP_FREQ2;
	if (SGS_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SGS_POPP_AMP;
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			op->amp.v0 *= o->sl.sopt.ampmult;
			op->amp.vt *= o->sl.sopt.ampmult;
		}
	}
	if (SGS_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SGS_POPP_AMP2;
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			op->amp2.v0 *= o->sl.sopt.ampmult;
			op->amp2.vt *= o->sl.sopt.ampmult;
		}
	}
	SGS_ScriptOpData *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params = SGS_POP_PARAMS;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SGS_POPP_WAVE;
		/* SGS_TIME set when time set */
		/* SGS_PHASE set when phase set */
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~PL_ACTIVE_EV;
	SGS_ScriptEvData *e = pl->event;
	end_operator(o);
	if (SGS_Ramp_ENABLED(&e->pan))
		e->vo_params |= SGS_PVOP_PAN;
	SGS_ScriptEvData *pve = e->voice_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
		e->vo_params = SGS_PVO_PARAMS & ~SGS_PVOP_GRAPH;
	}
	pl->event = NULL;
	SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
	if (!o->group_start)
		o->group_start = group_e;
	o->group_end = group_e;
}

static void begin_event(SGS_Parser *restrict o, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e, *pve;
	end_event(o);
	pl->event = calloc(1, sizeof(SGS_ScriptEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SGS_Ramp_reset(&e->pan);
	if (pl->on_prev != NULL) {
		SGS_ScriptEvBranch *fork;
		if (pl->on_prev->op_flags & SGS_SDOP_NESTED)
			e->ev_flags |= SGS_SDEV_IMPLICIT_TIME;
		pve = pl->on_prev->event;
		pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
		fork = pve->forks;
		if (is_compstep) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				if (!pl->main_ev)
					pl->main_ev = pve;
				else
					fork = pl->main_ev->forks;
				pl->main_ev->forks = calloc(1,
						sizeof(SGS_ScriptEvBranch));
				pl->main_ev->forks->events = e;
				pl->main_ev->forks->prev = fork;
				pl->pl_flags &= ~PL_NEW_EVENT_FORK;
			} else {
				pve->next = e;
			}
		} else while (fork != NULL) {
			SGS_ScriptEvData *last_ce;
			for (last_ce = fork->events; last_ce->next;
					last_ce = last_ce->next) ;
			last_ce->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
			fork = fork->prev;
		}
		e->voice_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SGS_RAMPP_STATE;
	}
	if (!is_compstep) {
		if (!o->events)
			o->events = e;
		else
			o->last_event->next = e;
		o->last_event = e;
		pl->main_ev = NULL;
	}
	pl->pl_flags |= PL_ACTIVE_EV;
}

static void begin_operator(SGS_Parser *restrict o, uint8_t use_type,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e = pl->event;
	SGS_ScriptOpData *op, *pop = pl->on_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(o);
	pl->operator = calloc(1, sizeof(SGS_ScriptOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_compstep && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	/*
	 * Initialize node.
	 */
	SGS_Ramp_reset(&op->freq);
	SGS_Ramp_reset(&op->freq2);
	SGS_Ramp_reset(&op->amp);
	SGS_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		pop->op_flags |= SGS_SDOP_LATER_USED;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SGS_SDOP_NESTED | SGS_SDOP_MULTIPLE);
		op->time = (SGS_Time){pop->time.v_ms,
			(pop->time.flags & SGS_TIMEP_IMPLICIT)};
		op->wave = pop->wave;
		op->phase = pop->phase;
		SGS_PtrArr_soft_copy(&op->fmods, &pop->fmods);
		SGS_PtrArr_soft_copy(&op->pmods, &pop->pmods);
		SGS_PtrArr_soft_copy(&op->amods, &pop->amods);
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			SGS_ScriptOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
				SGS_PtrArr_add(&mpop->on_next, op);
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SGS_SDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~PL_BIND_MULTIPLE;
		} else {
			SGS_PtrArr_add(&pop->on_next, op);
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
		if (!(pl->pl_flags & PL_NESTED_SCOPE)) {
			op->freq.v0 = o->sl.sopt.def_freq;
		} else {
			op->op_flags |= SGS_SDOP_NESTED;
			op->freq.v0 = o->sl.sopt.def_relfreq;
			op->freq.flags |= SGS_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SGS_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SGS_RAMPP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	if (pop != NULL || use_type == SGS_POP_CARR) {
		SGS_PtrArr_add(&e->operators, op);
		if (!pop) {
			e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
			SGS_PtrArr_add(&e->op_graph, op);
		}
	} else {
		SGS_PtrArr *list = NULL;
		switch (use_type) {
		case SGS_POP_FMOD:
			list = &pl->parent_on->fmods;
			break;
		case SGS_POP_PMOD:
			list = &pl->parent_on->pmods;
			break;
		case SGS_POP_AMOD:
			list = &pl->parent_on->amods;
			break;
		}
		SGS_PtrArr_add(list, op);
	}
	/*
	 * Assign label. If no new label but previous node
	 * (for a non-sub event) has one, update label to
	 * point to new node, but keep pointer in previous node.
	 */
	if (pl->set_label != NULL) {
		op->label = pl->set_label;
		op->label->data = op;
		pl->set_label = NULL;
	} else if (!is_compstep && pop != NULL && pop->label != NULL) {
		op->label = pop->label;
		op->label->data = op;
	}
	pl->pl_flags |= PL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(SGS_Parser *restrict o,
		SGS_ScriptOpData *restrict previous,
		uint8_t use_type, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	pl->on_prev = previous;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->sub_f != parse_in_event ||
			pl->next_wait_ms ||
			is_compstep)
		begin_event(o, is_compstep);
	begin_operator(o, use_type, is_compstep);
	pl->last_use_type = use_type; /* FIXME: kludge */
}

static void flush_durgroup(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	pl->next_wait_ms = 0; /* does not cross boundaries */
	if (o->group_start != NULL) {
		o->group_end->group_backref = o->group_start;
		o->group_start = o->group_end = NULL;
	}
}

static void enter_level(SGS_Parser *restrict o,
		struct ParseLevel *restrict pl,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel *restrict parent_pl = o->cur_pl;
	++o->call_level;
	o->cur_pl = pl;
	*pl = (struct ParseLevel){0};
	pl->scope = newscope;
	if (parent_pl != NULL) {
		pl->parent = parent_pl;
		pl->sub_f = parent_pl->sub_f;
		pl->pl_flags = parent_pl->pl_flags &
			(PL_NESTED_SCOPE | PL_BIND_MULTIPLE);
		if (newscope == SCOPE_SAME)
			pl->scope = parent_pl->scope;
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		pl->parent_on = parent_pl->parent_on;
		if (newscope == SCOPE_NEST) {
			pl->pl_flags |= PL_NESTED_SCOPE;
			pl->parent_on = parent_pl->operator;
		}
	}
	pl->use_type = use_type;
}

static void leave_level(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_operator(o);
	if (pl->set_label != NULL) {
		SGS_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
	if (!pl->parent) {
		/*
		 * At end of top scope (i.e. at end of script),
		 * end last event and adjust timing.
		 */
		end_event(o);
		flush_durgroup(o);
	}
	--o->call_level;
	o->cur_pl = pl->parent;
	if (pl->scope == SCOPE_BIND) {
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= PL_BIND_MULTIPLE;
			begin_node(o, pl->first_operator,
					pl->parent->last_use_type, false);
		}
	}
}

/*
 * Main parser functions
 */

static void parse_in_settings(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	struct ScanLookup *sl = &o->sl;
	SGS_Scanner *sc = o->sc;
	pl->sub_f = parse_in_settings;
	for (;;) {
		uint8_t c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
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
				sl->sopt.changed |= SGS_SOPT_DEF_RELFREQ;
			break;
		case 't':
			if (scan_time_val(sc, &sl->sopt.def_time_ms))
				sl->sopt.changed |= SGS_SOPT_DEF_TIME;
			break;
		default:
			goto DEFER;
		}
	}
	return;
DEFER:
	SGS_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static bool parse_level(SGS_Parser *restrict o,
		uint8_t use_type, uint8_t newscope);

static bool parse_ev_amp(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	SGS_ScriptOpData *op = pl->operator;
	scan_ramp(sc, NULL, &op->amp, false);
	if (SGS_Scanner_tryc(sc, ',')) {
		scan_ramp(sc, NULL, &op->amp2, false);
	}
	if (SGS_Scanner_tryc(sc, '~') && SGS_Scanner_tryc(sc, '[')) {
		op->mods_set |= (1<<SGS_POP_AMOD);
		SGS_PtrArr_clear(&op->amods);
		parse_level(o, SGS_POP_AMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_freq(SGS_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	SGS_ScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SGS_SDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	scan_ramp(sc, numsym_f, &op->freq, rel_freq);
	if (SGS_Scanner_tryc(sc, ',')) {
		scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
	}
	if (SGS_Scanner_tryc(sc, '~') && SGS_Scanner_tryc(sc, '[')) {
		op->mods_set |= (1<<SGS_POP_FMOD);
		SGS_PtrArr_clear(&op->fmods);
		parse_level(o, SGS_POP_FMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	SGS_ScriptOpData *op = pl->operator;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SGS_POPP_PHASE;
	}
	if (SGS_Scanner_tryc(sc, '+') && SGS_Scanner_tryc(sc, '[')) {
		op->mods_set |= (1<<SGS_POP_PMOD);
		SGS_PtrArr_clear(&op->pmods);
		parse_level(o, SGS_POP_PMOD, SCOPE_NEST);
	}
	return false;
}

static void parse_in_event(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	pl->sub_f = parse_in_event;
	for (;;) {
		SGS_ScriptEvData *e = pl->event;
		SGS_ScriptOpData *op = pl->operator;
		uint8_t c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
		case 'P':
			if ((pl->pl_flags & PL_NESTED_SCOPE) != 0)
				goto DEFER;
			scan_ramp(sc, NULL, &e->pan, false);
			break;
		case '/':
			if (parse_waittime(o)) {
				// FIXME: Buggy update node handling
				// for carriers etc. if enabled.
				//begin_node(o, pl->operator, 0, false);
			}
			break;
		case '\\':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, 0, true);
				pl->event->ev_flags |= SGS_SDEV_FROM_GAPSHIFT;
			}
			break;
		case 'a':
			if (parse_ev_amp(o)) goto DEFER;
			break;
		case 'f':
			if (parse_ev_freq(o, false)) goto DEFER;
			break;
		case 'p':
			if (parse_ev_phase(o)) goto DEFER;
			break;
		case 'r':
			if (parse_ev_freq(o, true)) goto DEFER;
			break;
		case 't':
			if (SGS_Scanner_tryc(sc, '*')) {
				op->time = (SGS_Time){o->sl.sopt.def_time_ms,
					0};
			} else if (SGS_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SGS_SDOP_NESTED)) {
					SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				op->time = (SGS_Time){o->sl.sopt.def_time_ms,
					SGS_TIMEP_SET | SGS_TIMEP_IMPLICIT};
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				op->time = (SGS_Time){time_ms, SGS_TIMEP_SET};
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
			goto DEFER;
		}
	}
	return;
DEFER:
	SGS_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static bool parse_level(SGS_Parser *restrict o,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel pl;
	SGS_SymStr *label;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope);
	SGS_Scanner *sc = o->sc;
	for (;;) {
		/*
		 * Return to any sub-parsing routine.
		 *
		 * May (also) happen in a new nested parse_level() call.
		 */
		if (pl.sub_f != NULL && !(pl.pl_flags & PL_DEFERRED_SUB))
			pl.sub_f(o);
		pl.pl_flags &= ~PL_DEFERRED_SUB;
		/*
		 * Parse main tokens.
		 */
		uint8_t c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
		case SGS_SCAN_LNBRK:
			if (pl.scope == SCOPE_TOP) {
				/*
				 * On top level of script,
				 * each line has a new "subscope".
				 */
				if (o->call_level > 1)
					goto RETURN;
				pl.sub_f = NULL;
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
			pl.set_label = label = scan_label(sc, c);
			break;
		case '/':
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					 && pl.event != NULL))
				goto INVALID;
			parse_waittime(o);
			break;
		case ';':
			if (newscope == SCOPE_SAME) {
				SGS_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			if ((pl.operator->time.flags &
			     (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT)) ==
			    (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
				SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' separator");
			begin_node(o, pl.operator, 0, true);
			pl.event->ev_flags |= SGS_SDEV_WAIT_PREV_DUR;
			parse_in_event(o);
			break;
		case '@':
			if (SGS_Scanner_tryc(sc, '[')) {
				end_operator(o);
				if (parse_level(o, pl.use_type, SCOPE_BIND))
					goto RETURN;
				/*
				 * Multiple-operator node now open.
				 */
				parse_in_event(o);
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
			pl.sub_f = NULL;
			label = scan_label(sc, c);
			if (label != NULL) {
				SGS_ScriptOpData *ref = label->data;
				if (!ref)
					SGS_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_node(o, ref, 0, false);
					parse_in_event(o);
				}
			}
			break;
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(o, 0, pl.use_type, false);
			pl.operator->wave = wave;
			parse_in_event(o);
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			parse_in_settings(o);
			break;
		case '[':
			warn_opening_disallowed(sc, '[');
			break;
		case ']':
			if (pl.scope == SCOPE_BIND) {
				endscope = true;
				goto RETURN;
			}
			if (pl.scope == SCOPE_NEST) {
				end_operator(o);
				endscope = true;
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '{':
			warn_opening_disallowed(sc, '{');
			break;
		case '|':
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					 && pl.event != NULL))
				goto INVALID;
			if (newscope == SCOPE_SAME) {
				SGS_Scanner_ungetc(sc);
				goto RETURN;
			}
			end_event(o);
			flush_durgroup(o);
			pl.sub_f = NULL;
			break;
		case '}':
			warn_closing_without_opening(sc, '}', '{');
			break;
		default:
		INVALID:
			if (!handle_unknown_or_eof(sc, c)) goto FINISH;
			break;
		}
	}
FINISH:
	if (newscope == SCOPE_NEST || newscope == SCOPE_BIND)
		warn_eof_without_closing(sc, ']');
RETURN:
	leave_level(o);
	/*
	 * Should return from the calling scope
	 * if/when the parent scope is ended.
	 */
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
	parse_level(o, SGS_POP_CARR, SCOPE_TOP);
	name = sc->f->path;
	SGS_Scanner_close(sc);
	return name;
}

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SGS_ScriptEvData *restrict e_last) {
	SGS_ScriptEvData *e, *e_after = e_last->next;
	size_t i;
	uint32_t cur_longest = 0, wait_sum = 0, wait_after = 0;
	for (e = e_last->group_backref; e != e_after; ) {
		if ((e->ev_flags & SGS_SDEV_VOICE_SET_DUR) != 0 &&
		    cur_longest < e->dur_ms)
			cur_longest = e->dur_ms;
		wait_after = cur_longest;
		e = e->next;
		if (e != NULL) {
			if (cur_longest > e->wait_ms)
				cur_longest -= e->wait_ms;
			else
				cur_longest = 0;
			wait_sum += e->wait_ms;
		}
	}
	for (e = e_last->group_backref; e != e_after; ) {
		SGS_ScriptOpData **ops;
		ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SGS_ScriptOpData *op = ops[i];
			if (!(op->time.flags & SGS_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = cur_longest + wait_sum;
				op->time.flags |= SGS_TIMEP_SET;
				if (e->dur_ms < op->time.v_ms)
					e->dur_ms = op->time.v_ms;
			}
		}
		e = e->next;
		if (e != NULL) {
			wait_sum -= e->wait_ms;
		}
	}
	e_last->group_backref = NULL;
	if (e_after != NULL)
		e_after->wait_ms += wait_after;
}

static inline void time_ramp(SGS_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SGS_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static uint32_t time_operator(SGS_ScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	if (!(op->op_params & SGS_POPP_TIME))
		op->event->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
	if (!(op->time.flags & SGS_TIMEP_SET)) {
		op->time.flags |= SGS_TIMEP_DEFAULT;
		if (op->op_flags & SGS_SDOP_NESTED) {
			op->time.flags |= SGS_TIMEP_IMPLICIT;
			op->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
		}
	}
	if (!(op->time.flags & SGS_TIMEP_IMPLICIT)) {
		time_ramp(&op->freq, op->time.v_ms);
		time_ramp(&op->freq2, op->time.v_ms);
		time_ramp(&op->amp, op->time.v_ms);
		time_ramp(&op->amp2, op->time.v_ms);
	}
	size_t i;
	SGS_ScriptOpData **ops;
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		time_operator(ops[i]);
	}
	return dur_ms;
}

static uint32_t time_event(SGS_ScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	// e->pan.flags |= SGS_RAMPP_TIME; // TODO: revisit semantics
	size_t i;
	SGS_ScriptOpData **ops;
	ops = (SGS_ScriptOpData**) SGS_PtrArr_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		uint32_t sub_dur_ms = time_operator(ops[i]);
		if (dur_ms < sub_dur_ms)
			dur_ms = sub_dur_ms;
	}
	/*
	 * Timing for sub-events - done before event list flattened.
	 */
	SGS_ScriptEvBranch *fork = e->forks;
	while (fork != NULL) {
		uint32_t nest_dur_ms = 0, wait_sum_ms = 0;
		SGS_ScriptEvData *ne = fork->events, *ne_prev = e;
		SGS_ScriptOpData *ne_op =
			(SGS_ScriptOpData*) SGS_PtrArr_GET(&ne->operators, 0),
			*ne_op_prev = ne_op->on_prev, *e_op = ne_op_prev;
		uint32_t first_time_ms = e_op->time.v_ms;
		SGS_Time def_time = {
			e_op->time.v_ms,
			(e_op->time.flags & SGS_TIMEP_IMPLICIT)
		};
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
		for (;;) {
			wait_sum_ms += ne->wait_ms;
			if (!(ne_op->time.flags & SGS_TIMEP_SET)) {
				ne_op->time = def_time;
				if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT)
					ne_op->time.flags |= SGS_TIMEP_SET |
						SGS_TIMEP_DEFAULT;
			}
			time_event(ne);
			if (ne_op->op_params & SGS_POPP_TIME) {
				def_time = (SGS_Time){
					ne_op->time.v_ms,
					(ne_op->time.flags & SGS_TIMEP_IMPLICIT)
				};
			}
			if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT) {
				if (ne_op_prev->time.flags & SGS_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SGS_SDEV_FROM_GAPSHIFT)) {
					ne_op_prev->time = (SGS_Time){ // gap
						0,
						SGS_TIMEP_SET|SGS_TIMEP_DEFAULT
					};
				}
			}
			if (ne->ev_flags & SGS_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_prev->dur_ms;
				ne_op_prev->time.flags &= ~SGS_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_op->time.flags |= SGS_TIMEP_SET;
			ne_op->op_params |= SGS_POPP_TIME;
			ne_op_prev = ne_op;
			ne_prev = ne;
			ne = ne->next;
			if (!ne) break;
			ne_op = (SGS_ScriptOpData*)
				SGS_PtrArr_GET(&ne->operators, 0);
		}
		if (nest_dur_ms < first_time_ms)
			nest_dur_ms = first_time_ms;
		if (dur_ms < nest_dur_ms)
			dur_ms = nest_dur_ms;
		fork = fork->prev;
	}
	e->dur_ms = dur_ms; /* unfinished estimate used to adjust timing */
	return dur_ms;
}

/*
 * Deals with events that are "sub-events" (attached to a main event as
 * nested sequence rather than part of the main linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SGS_ScriptEvData *restrict e) {
	SGS_ScriptEvBranch *fork = e->forks;
	SGS_ScriptEvData *ne = fork->events;
	SGS_ScriptEvData *fe = e->next, *fe_prev = e;
	while (ne != NULL) {
		if (!fe) {
			/*
			 * No more events in the flat sequence,
			 * so append all sub-events.
			 */
			fe_prev->next = fe = ne;
			break;
		}
		/*
		 * Insert next sub-event before or after
		 * the next events of the flat sequence.
		 */
		SGS_ScriptEvData *ne_next = ne->next;
		if (fe->wait_ms >= ne->wait_ms) {
			fe->wait_ms -= ne->wait_ms;
			fe_prev->next = ne;
			ne->next = fe;
		} else {
			ne->wait_ms -= fe->wait_ms;
			/*
			 * If several events should pass in the flat sequence
			 * before the next sub-event is inserted, skip ahead.
			 */
			while (fe->next && fe->next->wait_ms <= ne->wait_ms) {
				fe_prev = fe;
				fe = fe->next;
				ne->wait_ms -= fe->wait_ms;
			}
			SGS_ScriptEvData *fe_next = fe->next;
			fe->next = ne;
			ne->next = fe_next;
			fe = fe_next;
			if (fe)
				fe->wait_ms -= ne->wait_ms;
		}
		fe_prev = ne;
		ne = ne_next;
	}
	e->forks = fork->prev;
	free(fork);
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SGS_Parser *restrict o) {
	SGS_ScriptEvData *e;
	for (e = o->events; e != NULL; e = e->next) {
		if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
		time_event(e);
		if (e->group_backref != NULL) time_durgroup(e);
	}
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (e = o->events; e != NULL; e = e->next) {
		while (e->forks != NULL) flatten_events(e);
	}
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SGS_Script* SGS_load_Script(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SGS_Parser pr;
	SGS_Script *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	postparse_passes(&pr);
	o = calloc(1, sizeof(SGS_Script));
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
void SGS_discard_Script(SGS_Script *restrict o) {
	if (!o)
		return;
	SGS_ScriptEvData *e;
	for (e = o->events; e != NULL; ) {
		SGS_ScriptEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
