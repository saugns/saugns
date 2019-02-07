/* saugns: Script file parser.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Default script options, used until changed in a script.
 */
static const SAU_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_ratio = 1.f,
};

/**
 * Destroy instance.
 */
void SAU_destroy_ParseData(SAU_ParseData *o) {
	if (o->st) SAU_destroy_SymTab(o->st);
	free(o);
}

/**
 * Create instance.
 */
SAU_ParseData *SAU_create_ParseData(void) {
	SAU_ParseData *o = calloc(1, sizeof(SAU_ParseData));
	if (!o) return NULL;
	o->sopt = def_sopt;
	SAU_SymTab *st = SAU_create_SymTab();
	if (!st) goto ERROR;
	o->st = st;
	o->wave_names = SAU_SymTab_pool_stra(st, SAU_Wave_names, SAU_WAVE_TYPES);
	if (!o->wave_names) goto ERROR;
	o->slope_names = SAU_SymTab_pool_stra(st, SAU_Slope_names, SAU_SLOPE_TYPES);
	if (!o->slope_names) goto ERROR;
	return o;

ERROR:
	SAU_destroy_ParseData(o);
	return NULL;
}

/*
 * Read identifier string. If a valid symbol string was read,
 * the copy set to \p strp will be the unique copy stored
 * in the symbol table. If no string was read,
 * \p strp will be set to NULL. \p lenp will be set to
 * the length of the string, or 0 if none.
 *
 * \return true if string not truncated
 */
bool scan_syms(SAU_Scanner *restrict o,
		const void **restrict strp, size_t *restrict lenp) {
	SAU_ParseData *pd = o->data;
	bool truncated = !SAU_Scanner_getsyms(o, pd->strbuf,
				(SAU_PD_STRBUF_LEN - 1), lenp);
	if (*lenp == 0) {
		*strp = NULL;
		return true;
	}
	const char *pool_str = SAU_SymTab_pool_str(pd->st, pd->strbuf, *lenp);
	if (pool_str == NULL) {
		SAU_Scanner_error(o, NULL,
			"failed to register string '%s'",
			pd->strbuf);
	}
	*strp = pool_str;
	return !truncated;
}

static int32_t scan_symafind(SAU_Scanner *restrict o,
		const char *const*restrict stra, size_t n,
		const char *restrict print_type) {
	SAU_ScanFrame sf_begin = o->sf;
	const void *key = NULL;
	size_t len;
	scan_syms(o, &key, &len);
	if (len == 0) {
		SAU_Scanner_warning(o, NULL, "%s missing", print_type);
		return -1;
	}
	for (size_t i = 0; i < n; ++i) {
		if (stra[i] == key) {
			return i;
		}
	}
	SAU_Scanner_warning(o, &sf_begin,
		"invalid %s; available types are:", print_type);
	fprintf(stderr, "\t%s", stra[0]);
	for (size_t i = 1; i < n; ++i) {
		fprintf(stderr, ", %s", stra[i]);
	}
	putc('\n', stderr);
	return -1;
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
		SAU_Scanner_warning(o, NULL, "invalid character '%c'", c);
	} else {
		SAU_Scanner_warning(o, NULL, "invalid character (value 0x%02hhX)", c);
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

typedef float (*NumSym_f)(SAU_Scanner *o);

typedef struct NumParser {
	SAU_Scanner *sc;
	NumSym_f numsym_f;
	SAU_ScanFrame sf_start;
} NumParser;
static double parse_num_r(NumParser *restrict o,
		uint8_t pri, uint32_t level) {
	SAU_Scanner *sc = o->sc;
	double num;
	bool minus = false;
	uint8_t c;
	if (level > 0) SAU_Scanner_skipws(sc);
	c = SAU_Scanner_getc(sc);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		SAU_Scanner_skipws(sc);
		c = SAU_Scanner_getc(sc);
	}
	if (c == '(') {
		num = parse_num_r(o, 255, level+1);
		if (minus) num = -num;
		if (level == 0) return num;
		goto EVAL;
	}
	if (o->numsym_f && IS_ALPHA(c)) {
		SAU_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (num != num)
			return NAN;
		if (minus) num = -num;
	} else {
		size_t read_len;
		SAU_Scanner_ungetc(sc);
		SAU_Scanner_getd(sc, &num, false, &read_len);
		if (read_len == 0)
			return NAN;
		if (minus) num = -num;
	}
EVAL:
	if (pri == 0)
		return num; /* defer all */
	for (;;) {
		if (level > 0) SAU_Scanner_skipws(sc);
		c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
		case SAU_SCAN_LNBRK:
			break;
		case '(':
			num *= parse_num_r(o, 255, level+1);
			break;
		case ')':
			if (pri < 255) goto DEFER;
			return num;
		case '^':
			num = exp(log(num) * parse_num_r(o, 0, level));
			break;
		case '*':
			num *= parse_num_r(o, 1, level);
			break;
		case '/':
			num /= parse_num_r(o, 1, level);
			break;
		case '+':
			if (pri < 2) goto DEFER;
			num += parse_num_r(o, 2, level);
			break;
		case '-':
			if (pri < 2) goto DEFER;
			num -= parse_num_r(o, 2, level);
			break;
		default:
			if (pri == 255) {
				SAU_Scanner_warning(sc, &o->sf_start,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (num != num) goto DEFER;
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	return num;
}
static bool scan_num(SAU_Scanner *restrict o, NumSym_f scan_numsym,
		float *restrict var, bool mul_inv) {
	NumParser np = {o, scan_numsym, o->sf};
	float num = parse_num_r(&np, 0, 0);
	if (num != num)
		return false;
	if (mul_inv) num = 1.f / num;
	if (fabs(num) == INFINITY) {
		SAU_Scanner_warning(o, &np.sf_start,
			"discarding infinite number");
		return false;
	}
	*var = num;
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
	SAU_ParseData *pd = o->data;
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
	freq = pd->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f +
			(notes[semitone][note+1] / notes[semitone][note] -
				1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static bool scan_time(SAU_Scanner *restrict o, float *restrict var) {
	SAU_ScanFrame sf = o->sf;
	float num;
	if (!scan_num(o, NULL, &num, false))
		return false;
	if (num < 0.f) {
		SAU_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*var = num;
	return true;
}

static const char *scan_label(SAU_Scanner *restrict o,
		size_t *restrict len, char op) {
	const void *s = NULL;
	scan_syms(o, &s, len);
	if (*len == 0) {
		SAU_Scanner_warning(o, NULL,
			"ignoring %c without label name", op);
	}
	return s;
}

static int32_t scan_wavetype(SAU_Scanner *restrict o) {
	SAU_ParseData *pd = o->data;
	int32_t wave = scan_symafind(o, pd->wave_names, SAU_WAVE_TYPES,
		"wave type");
	return wave;
}

static bool scan_tpar_state(SAU_Scanner *restrict o,
		NumSym_f scan_numsym,
		SAU_TimedParam *restrict tpar, bool ratio) {
	if (!scan_num(o, scan_numsym, &tpar->v0, ratio))
		return false;
	if (ratio) {
		tpar->flags |= SAU_TPAR_STATE_RATIO;
	} else {
		tpar->flags &= ~SAU_TPAR_STATE_RATIO;
	}
	tpar->flags |= SAU_TPAR_STATE;
	return true;
}

static bool scan_tpar_slope(SAU_Scanner *restrict o,
		NumSym_f scan_numsym,
		SAU_TimedParam *restrict tpar, bool ratio) {
	SAU_ParseData *pd = o->data;
	bool goal = false;
	float vt;
	uint32_t time_ms = SAU_TIME_DEFAULT;
	uint8_t slope = tpar->slope; // has default
	if ((tpar->flags & SAU_TPAR_SLOPE) != 0) {
		// allow partial change
		if (((tpar->flags & SAU_TPAR_SLOPE_RATIO) != 0) == ratio) {
			goal = true;
			vt = tpar->vt;
		}
		time_ms = tpar->time_ms;
	}
	for (;;) {
		uint8_t c = SAU_Scanner_getc_nospace(o);
		switch (c) {
		case SAU_SCAN_LNBRK:
			break;
		case 'c': {
			int32_t type = scan_symafind(o, pd->slope_names,
				SAU_SLOPE_TYPES, "slope change type");
			if (type >= 0) {
				slope = type;
			}
			break; }
		case 't': {
			float time;
			if (scan_time(o, &time)) {
				time_ms = lrint(time * 1000.f);
			}
			break; }
		case 'v':
			if (scan_num(o, scan_numsym, &vt, ratio))
				goal = true;
			break;
		case ']':
			goto RETURN;
		default:
			if (!handle_unknown_or_eof(o, c)) {
				warn_eof_without_closing(o, ']');
				goto RETURN;
			}
			break;
		}
	}
RETURN:
	if (!goal) {
		SAU_Scanner_warning(o, NULL,
			"ignoring value slope with no target value");
		return false;
	}
	tpar->vt = vt;
	tpar->time_ms = time_ms;
	tpar->slope = slope;
	if (ratio) {
		tpar->flags |= SAU_TPAR_SLOPE_RATIO;
	} else {
		tpar->flags &= ~SAU_TPAR_SLOPE_RATIO;
	}
	tpar->flags |= SAU_TPAR_SLOPE;
	return true;
}

/*
 * Parser
 */

typedef struct SAU_Parser {
	SAU_ParseData *pd;
	SAU_Scanner *sc;
	uint32_t call_level;
	/* node state */
	SAU_ScriptEvData *events;
	SAU_ScriptEvData *last_event;
} SAU_Parser;

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static void init_Parser(SAU_Parser *restrict o) {
	*o = (SAU_Parser){0};
	o->pd = SAU_create_ParseData();
	o->sc = SAU_create_Scanner();
	o->sc->data = o->pd;
}

/*
 * Finalize parser instance.
 */
static void fini_Parser(SAU_Parser *restrict o) {
	SAU_destroy_ParseData(o->pd);
	SAU_destroy_Scanner(o->sc);
}

/*
 * Scope values.
 */
enum {
	SCOPE_SAME = 0,
	SCOPE_TOP = 1,
	SCOPE_BIND = '{',
	SCOPE_NEST = '<',
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
	SDPL_NESTED_SCOPE = 1<<0,
	SDPL_BIND_MULTIPLE = 1<<1, /* previous node interpreted as set of nodes */
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
	SAU_ScriptEvData *event, *last_event;
	SAU_ScriptOpData *operator, *first_operator, *last_operator;
	SAU_ScriptOpData *parent_on, *on_prev;
	uint8_t linktype;
	uint8_t last_linktype; /* FIXME: kludge */
	const char *set_label; /* label assigned to next node */
	/* timing/delay */
	SAU_ScriptEvData *group_from; /* where to begin for group_events() */
	SAU_ScriptEvData *composite; /* grouping of events for a voice and/or operator */
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
static void destroy_operator(SAU_ScriptOpData *restrict op) {
	SAU_PtrList_clear(&op->on_next);
	size_t i;
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrList_clear(&op->fmods);
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrList_clear(&op->pmods);
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SAU_ScriptEvData *restrict e) {
	size_t i;
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrList_clear(&e->operators);
	SAU_PtrList_clear(&e->op_graph);
	free(e);
}

static void end_operator(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_ParseData *pd = o->pd;
	SAU_ScriptOpData *op = pl->operator;
	if (!op)
		return; /* nothing to do */
	if (SAU_TimedParam_ENABLED(&op->freq))
		op->op_params |= SAU_POPP_FREQ;
	if (SAU_TimedParam_ENABLED(&op->amp)) {
		op->op_params |= SAU_POPP_AMP;
		if (!(pl->pl_flags & SDPL_NESTED_SCOPE))
			op->amp.v0 *= pd->sopt.ampmult;
	}
	SAU_ScriptOpData *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset remaining operator state for initial event.
		 */
		op->op_params |= SAU_POPP_ADJCS |
				SAU_POPP_WAVE |
				SAU_POPP_TIME |
				SAU_POPP_SILENCE |
				SAU_POPP_DYNFREQ |
				SAU_POPP_PHASE |
				SAU_POPP_DYNAMP;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SAU_POPP_WAVE;
		/* SAU_TIME set when time set */
		if (op->silence_ms != 0)
			op->op_params |= SAU_POPP_SILENCE;
		if (op->dynfreq != pop->dynfreq)
			op->op_params |= SAU_POPP_DYNFREQ;
		/* SAU_PHASE set when phase set */
		if (op->dynamp != pop->dynamp)
			op->op_params |= SAU_POPP_DYNAMP;
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
	SAU_ScriptEvData *e = pl->event;
	if (!e)
		return; /* nothing to do */
	end_operator(pl);
	if (SAU_TimedParam_ENABLED(&e->pan))
		e->vo_params |= SAU_PVOP_PAN;
	SAU_ScriptEvData *pve = e->voice_prev;
	if (!pve) {
		/*
		 * Reset remaining voice state for initial event.
		 */
		e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SAU_Parser *o = pl->o;
	SAU_ScriptEvData *e, *pve;
	end_event(pl);
	pl->event = calloc(1, sizeof(SAU_ScriptEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SAU_TimedParam_reset(&e->pan);
	if (pl->on_prev != NULL) {
		pve = pl->on_prev->event;
		pve->ev_flags |= SAU_SDEV_VOICE_LATER_USED;
		if (pve->composite != NULL && !is_composite) {
			SAU_ScriptEvData *last_ce;
			for (last_ce = pve->composite; last_ce->next; last_ce = last_ce->next) ;
			last_ce->ev_flags |= SAU_SDEV_VOICE_LATER_USED;
		}
		e->voice_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SAU_TPAR_STATE;
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
}

static void begin_operator(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SAU_Parser *o = pl->o;
	SAU_ParseData *pd = o->pd;
	SAU_ScriptEvData *e = pl->event;
	SAU_ScriptOpData *op, *pop = pl->on_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(pl);
	pl->operator = calloc(1, sizeof(SAU_ScriptOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	SAU_TimedParam_reset(&op->freq);
	SAU_TimedParam_reset(&op->amp);
	if (pop != NULL) {
		pop->op_flags |= SAU_SDOP_LATER_USED;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SAU_SDOP_NESTED | SAU_SDOP_MULTIPLE);
		if (is_composite)
			op->op_flags |= SAU_SDOP_TIME_DEFAULT; /* prev or inf */
		op->time_ms = pop->time_ms;
		op->wave = pop->wave;
		op->phase = pop->phase;
		op->dynfreq = pop->dynfreq;
		op->dynamp = pop->dynamp;
		SAU_PtrList_soft_copy(&op->fmods, &pop->fmods);
		SAU_PtrList_soft_copy(&op->pmods, &pop->pmods);
		SAU_PtrList_soft_copy(&op->amods, &pop->amods);
		if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
			SAU_ScriptOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time_ms) max_time = mpop->time_ms;
				SAU_PtrList_add(&mpop->on_next, op);
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SAU_SDOP_MULTIPLE;
			op->time_ms = max_time;
			pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
		} else {
			SAU_PtrList_add(&pop->on_next, op);
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->op_flags = SAU_SDOP_TIME_DEFAULT; /* default: depends on context */
		op->time_ms = pd->sopt.def_time_ms;
		if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
			op->freq.v0 = pd->sopt.def_freq;
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			op->freq.v0 = pd->sopt.def_ratio;
			op->freq.flags |= SAU_TPAR_STATE_RATIO;
		}
		op->freq.flags |= SAU_TPAR_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SAU_TPAR_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (for one or multiple carriers) in the
	 * case of operator linking/nesting.
	 */
	if (linktype == NL_REFER ||
			linktype == NL_GRAPH) {
		SAU_PtrList_add(&e->operators, op);
		if (linktype == NL_GRAPH) {
			e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
			SAU_PtrList_add(&e->op_graph, op);
		}
	} else {
		SAU_PtrList *list = NULL;
		switch (linktype) {
		case NL_FMODS:
			list = &pl->parent_on->fmods;
			break;
		case NL_PMODS:
			list = &pl->parent_on->pmods;
			break;
		case NL_AMODS:
			list = &pl->parent_on->amods;
			break;
		}
		pl->parent_on->op_params |= SAU_POPP_ADJCS;
		SAU_PtrList_add(list, op);
	}
	/*
	 * Assign label. If no new label but previous node (for a non-composite)
	 * has one, update label to point to new node, but also keep pointer in
	 * previous node.
	 */
	if (pl->set_label != NULL) {
		SAU_SymTab_set(pd->st, pl->set_label, strlen(pl->set_label), op);
		op->label = pl->set_label;
		pl->set_label = NULL;
	} else if (!is_composite && pop != NULL && pop->label != NULL) {
		SAU_SymTab_set(pd->st, pop->label, strlen(pop->label), op);
		op->label = pop->label;
	}
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
		SAU_ScriptOpData *restrict previous,
		uint8_t linktype, bool is_composite) {
	pl->on_prev = previous;
	if (!pl->event ||
			pl->location != SDPL_IN_EVENT /* previous event implicitly ended */ ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, linktype, is_composite);
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
		pl->pl_flags = parent_pl->pl_flags;
		pl->location = parent_pl->location;
		if (newscope == SCOPE_SAME)
			pl->scope = parent_pl->scope;
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		pl->parent_on = parent_pl->parent_on;
		if (newscope == SCOPE_BIND)
			pl->group_from = parent_pl->group_from;
		if (newscope == SCOPE_NEST) {
			pl->pl_flags |= SDPL_NESTED_SCOPE;
			pl->parent_on = parent_pl->operator;
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
		 * for any operator nodes in this scope.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
			begin_node(pl->parent, pl->first_operator, pl->parent->last_linktype, false);
		}
	} else if (!pl->parent) {
		/*
		 * At end of top scope, ie. at end of script,
		 * end the last event and adjust timing.
		 */
		SAU_ScriptEvData *group_to;
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
	SAU_ParseData *pd = o->pd;
	SAU_Scanner *sc = o->sc;
	pl->location = SDPL_IN_DEFAULTS;
	for (;;) {
		uint8_t c = SAU_Scanner_getc_nospace(sc);
		switch (c) {
		case 'a':
			if (scan_num(sc, NULL, &pd->sopt.ampmult, false)) {
				pd->sopt.changed |= SAU_SOPT_AMPMULT;
			}
			break;
		case 'f':
			if (scan_num(sc, scan_note, &pd->sopt.def_freq, false)) {
				pd->sopt.changed |= SAU_SOPT_DEF_FREQ;
			}
			break;
		case 'n': {
			float freq;
			if (scan_num(sc, NULL, &freq, false)) {
				if (freq < 1.f) {
					SAU_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
					break;
				}
				pd->sopt.A4_freq = freq;
				pd->sopt.changed |= SAU_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &pd->sopt.def_ratio, true)) {
				pd->sopt.changed |= SAU_SOPT_DEF_RATIO;
			}
			break;
		case 't': {
			float time;
			if (scan_time(sc, &time)) {
				pd->sopt.def_time_ms = lrint(time * 1000.f);
				pd->sopt.changed |= SAU_SOPT_DEF_TIME;
			}
			break; }
		default:
			SAU_Scanner_ungetc(sc);
			return true; /* let parse_level() take care of it */
		}
	}
	return false;
}

static bool parse_level(SAU_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope);

static bool parse_step(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_ParseData *pd = o->pd;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptEvData *e = pl->event;
	SAU_ScriptOpData *op = pl->operator;
	pl->location = SDPL_IN_EVENT;
	for (;;) {
		uint8_t c = SAU_Scanner_getc_nospace(sc);
		switch (c) {
		case 'P':
			if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
				goto UNKNOWN;
			if (SAU_Scanner_tryc(sc, '[')) {
				scan_tpar_slope(sc, NULL, &e->pan, false);
			} else {
				scan_tpar_state(sc, NULL, &e->pan, false);
			}
			break;
		case '\\':
			if (parse_waittime(pl)) {
				begin_node(pl, pl->operator, NL_REFER, false);
			}
			break;
		case 'a':
			if (SAU_Scanner_tryc(sc, '!')) {
				if (!SAU_File_TESTC(sc->f, '<')) {
					scan_num(sc, NULL, &op->dynamp, false);
				}
				if (SAU_Scanner_tryc(sc, '<')) {
					if (op->amods.count > 0) {
						op->op_params |= SAU_POPP_ADJCS;
						SAU_PtrList_clear(&op->amods);
					}
					parse_level(o, pl, NL_AMODS, SCOPE_NEST);
				}
			} else if (SAU_Scanner_tryc(sc, '[')) {
				scan_tpar_slope(sc, NULL, &op->amp, false);
			} else {
				scan_tpar_state(sc, NULL, &op->amp, false);
			}
			break;
		case 'f':
			if (SAU_Scanner_tryc(sc, '!')) {
				if (!SAU_File_TESTC(sc->f, '<')) {
					scan_num(sc, NULL, &op->dynfreq, false);
				}
				if (SAU_Scanner_tryc(sc, '<')) {
					if (op->fmods.count > 0) {
						op->op_params |= SAU_POPP_ADJCS;
						SAU_PtrList_clear(&op->fmods);
					}
					parse_level(o, pl, NL_FMODS, SCOPE_NEST);
				}
			} else if (SAU_Scanner_tryc(sc, '[')) {
				scan_tpar_slope(sc, scan_note, &op->freq, false);
			} else {
				scan_tpar_state(sc, scan_note, &op->freq, false);
			}
			break;
		case 'p':
			if (SAU_Scanner_tryc(sc, '+')) {
				if (SAU_Scanner_tryc(sc, '<')) {
					if (op->pmods.count > 0) {
						op->op_params |= SAU_POPP_ADJCS;
						SAU_PtrList_clear(&op->pmods);
					}
					parse_level(o, pl, NL_PMODS, SCOPE_NEST);
				} else {
					SAU_Scanner_ungetc(sc);
					goto UNKNOWN;
				}
			} else if (scan_num(sc, NULL, &op->phase, false)) {
				op->phase = fmod(op->phase, 1.f);
				if (op->phase < 0.f)
					op->phase += 1.f;
				op->op_params |= SAU_POPP_PHASE;
			}
			break;
		case 'r':
			if (!(pl->pl_flags & SDPL_NESTED_SCOPE))
				goto UNKNOWN;
			if (SAU_Scanner_tryc(sc, '!')) {
				if (!SAU_File_TESTC(sc->f, '<')) {
					scan_num(sc, NULL, &op->dynfreq, true);
				}
				if (SAU_Scanner_tryc(sc, '<')) {
					if (op->fmods.count > 0) {
						op->op_params |= SAU_POPP_ADJCS;
						SAU_PtrList_clear(&op->fmods);
					}
					parse_level(o, pl, NL_FMODS, SCOPE_NEST);
				}
			} else if (SAU_Scanner_tryc(sc, '[')) {
				scan_tpar_slope(sc, NULL, &op->freq, true);
			} else {
				scan_tpar_state(sc, NULL, &op->freq, true);
			}
			break;
		case 's': {
			float silence;
			if (scan_time(sc, &silence)) {
				op->silence_ms = lrint(silence * 1000.f);
			}
			break; }
		case 't':
			if (SAU_Scanner_tryc(sc, '*')) {
				op->op_flags |= SAU_SDOP_TIME_DEFAULT; /* later fitted or kept to default */
				op->time_ms = pd->sopt.def_time_ms;
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				op->time_ms = SAU_TIME_INF;
			} else {
				float time;
				if (scan_time(sc, &time)) {
					op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
					op->time_ms = lrint(time * 1000.f);
				}
			}
			op->op_params |= SAU_POPP_TIME;
			break;
		case 'w': {
			int32_t wave = scan_wavetype(sc);
			if (wave < 0)
				break;
			op->wave = wave;
			break; }
		default:
		UNKNOWN:
			SAU_Scanner_ungetc(sc);
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
static bool parse_level(SAU_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	ParseLevel pl;
	SAU_ParseData *pd = o->pd;
	const char *label;
	size_t label_len;
	uint8_t flags = 0;
	bool endscope = false;
	begin_scope(o, &pl, parent_pl, linktype, newscope);
	++o->call_level;
	SAU_Scanner *sc = o->sc;
	for (;;) {
		uint8_t c = SAU_Scanner_getc_nospace(sc);
		switch (c) {
		case SAU_SCAN_LNBRK:
			if (pl.scope == SCOPE_TOP) {
				/*
				 * On top level of script,
				 * each line is a new "subscope".
				 */
				if (o->call_level > 1)
					goto RETURN;
				flags = 0;
				pl.location = SDPL_IN_NONE;
				pl.first_operator = NULL;
			}
			break;
		case ':':
			if (pl.set_label != NULL) {
				SAU_Scanner_warning(sc, NULL,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.location = SDPL_IN_NONE;
			label = scan_label(sc, &label_len, ':');
			if (label_len > 0) {
				SAU_ScriptOpData *ref = SAU_SymTab_get(pd->st, label, label_len);
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
		case ';':
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
				goto INVALID;
			begin_node(&pl, pl.operator, NL_REFER, true);
			flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
			break;
		case '<':
			if (parse_level(o, &pl, pl.linktype, '<'))
				goto RETURN;
			break;
		case '>':
			if (pl.scope != SCOPE_NEST) {
				warn_closing_without_opening(sc, '>', '<');
				break;
			}
			end_operator(&pl);
			endscope = true;
			goto RETURN;
		case 'O': {
			int32_t wave = scan_wavetype(sc);
			if (wave < 0)
				break;
			begin_node(&pl, 0, pl.linktype, false);
			pl.operator->wave = wave;
			flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			flags = parse_settings(&pl) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
			break;
		case '\\':
			if (pl.location == SDPL_IN_DEFAULTS ||
				((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 &&
					pl.event != NULL))
				goto INVALID;
			parse_waittime(&pl);
			break;
		case '\'':
			if (pl.set_label != NULL) {
				SAU_Scanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			label = scan_label(sc, &label_len, '\'');
			pl.set_label = label;
			break;
		case '{':
			end_operator(&pl);
			if (parse_level(o, &pl, pl.linktype, SCOPE_BIND))
				goto RETURN;
			flags = parse_step(&pl) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
			break;
		case '|':
			if (pl.location == SDPL_IN_DEFAULTS ||
				((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 &&
					pl.event != NULL))
				goto INVALID;
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (!pl.event) {
				SAU_Scanner_warning(sc, NULL,
"end of sequence before any parts given");
				break;
			}
			if (pl.group_from != NULL) {
				SAU_ScriptEvData *group_to = (pl.composite) ?
								pl.composite :
								pl.event;
				group_to->groupfrom = pl.group_from;
				pl.group_from = NULL;
			}
			end_event(&pl);
			pl.location = SDPL_IN_NONE;
			break;
		case '}':
			if (pl.scope != SCOPE_BIND) {
				warn_closing_without_opening(sc, '}', '{');
				break;
			}
			endscope = true;
			goto RETURN;
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
	if (newscope == SCOPE_NEST)
		warn_eof_without_closing(sc, '>');
	else if (newscope == SCOPE_BIND)
		warn_eof_without_closing(sc, '}');
RETURN:
	end_scope(&pl);
	--o->call_level;
	/* Should return from the calling scope if/when the parent scope is ended. */
	return (endscope && pl.scope != newscope);
}

/*
 * Process file.
 *
 * \return true if completed, false on error preventing parse
 */
static bool parse_file(SAU_Parser *restrict o, const char *restrict fname) {
	SAU_Scanner *sc = o->sc;
	if (!SAU_Scanner_fopenrb(sc, fname)) {
		return false;
	}
	parse_level(o, NULL, NL_GRAPH, SCOPE_TOP);
	SAU_Scanner_close(sc);
	return true;
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SAU_ScriptEvData *restrict to) {
	SAU_ScriptEvData *e, *e_after = to->next;
	size_t i;
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SAU_ScriptOpData **ops;
		ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SAU_ScriptOpData *op = ops[i];
			if (e->next == e_after &&
				i == (e->operators.count - 1) &&
				(op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) /* default for last node in group */
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
			if (wait < op->time_ms)
				wait = op->time_ms;
		}
		e = e->next;
		if (e != NULL) {
			/*wait -= e->wait_ms;*/
			waitcount += e->wait_ms;
		}
	}
	for (e = to->groupfrom; e != e_after; ) {
		SAU_ScriptOpData **ops;
		ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SAU_ScriptOpData *op = ops[i];
			if ((op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				op->time_ms = wait + waitcount; /* fill in sensible default time */
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	to->groupfrom = NULL;
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static void time_operator(SAU_ScriptOpData *restrict op) {
	SAU_ScriptEvData *e = op->event;
	if (op->freq.time_ms == SAU_TIME_DEFAULT)
		op->freq.time_ms = op->time_ms;
	if (op->amp.time_ms == SAU_TIME_DEFAULT)
		op->amp.time_ms = op->time_ms;
	if ((op->op_flags & (SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) ==
		(SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) {
		op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		op->time_ms = SAU_TIME_INF;
	}
	if (op->time_ms != SAU_TIME_INF &&
		!(op->op_flags & SAU_SDOP_SILENCE_ADDED)) {
		op->time_ms += op->silence_ms;
		op->op_flags |= SAU_SDOP_SILENCE_ADDED;
	}
	if ((e->ev_flags & SAU_SDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			((SAU_ScriptEvData*)e->next)->wait_ms += op->time_ms;
		e->ev_flags &= ~SAU_SDEV_ADD_WAIT_DURATION;
	}
	size_t i;
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		time_operator(ops[i]);
	}
}

static void time_event(SAU_ScriptEvData *restrict e) {
	/*
	 * Fill in blank slope durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	if (e->pan.time_ms == SAU_TIME_DEFAULT)
		e->pan.time_ms = 1000; /* FIXME! */
	size_t i;
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		time_operator(ops[i]);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SAU_ScriptEvData *ce = e->composite;
		SAU_ScriptOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = (SAU_ScriptOpData*) SAU_PtrList_GET(&ce->operators, 0),
		ce_op_prev = ce_op->on_prev,
		e_op = ce_op_prev;
		if ((e_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0)
			e_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		for (;;) {
			ce->wait_ms += ce_op_prev->time_ms;
			if ((ce_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				ce_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				ce_op->time_ms = ((ce_op->op_flags & SAU_SDOP_NESTED) != 0 &&
					!ce->next) ?
					SAU_TIME_INF :
					ce_op_prev->time_ms - ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time_ms == SAU_TIME_INF)
				e_op->time_ms = SAU_TIME_INF;
			else if (e_op->time_ms != SAU_TIME_INF)
				e_op->time_ms += ce_op->time_ms +
					(ce->wait_ms - ce_op_prev->time_ms);
			ce_op->op_params &= ~SAU_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = (SAU_ScriptOpData*) SAU_PtrList_GET(&ce->operators, 0);
		}
	}
}

/*
 * Deals with events that are "composite" (attached to a main event as
 * successive "sub-events" rather than part of the big, linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SAU_ScriptEvData *restrict e) {
	SAU_ScriptEvData *ce = e->composite;
	SAU_ScriptEvData *se = e->next, *se_prev = e;
	int32_t wait_ms = 0;
	int32_t added_wait_ms = 0;
	while (ce != NULL) {
		if (!se) {
			/*
			 * No more events in the ordinary sequence,
			 * so append all composites.
			 */
			se_prev->next = ce;
			break;
		}
		/*
		 * If several events should pass in the ordinary sequence
		 * before the next composite is inserted, skip ahead.
		 */
		wait_ms += se->wait_ms;
		if (se->next &&
				(wait_ms + se->next->wait_ms) <= (ce->wait_ms + added_wait_ms)) {
			se_prev = se;
			se = se->next;
			continue;
		}
		/*
		 * Insert next composite before or after the
		 * next event of the ordinary sequence.
		 */
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			SAU_ScriptEvData *ce_next = ce->next;
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
			ce = ce_next;
		} else {
			SAU_ScriptEvData *se_next, *ce_next;
			se_next = se->next;
			ce_next = ce->next;
			ce->wait_ms -= wait_ms;
			added_wait_ms += ce->wait_ms;
			wait_ms = 0;
			se->next = ce;
			ce->next = se_next;
			se_prev = ce;
			se = se_next;
			ce = ce_next;
		}
	}
	e->composite = NULL;
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SAU_Parser *restrict o) {
	SAU_ScriptEvData *e;
	for (e = o->events; e; e = e->next) {
		time_event(e);
		if (e->groupfrom != NULL) group_events(e);
	}
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (e = o->events; e; e = e->next) {
		if (e->composite != NULL) flatten_events(e);
	}
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SAU_Script* SAU_load_Script(const char *restrict fname) {
	SAU_Parser pr;
	init_Parser(&pr);
	SAU_Script *o = NULL;
	if (!parse_file(&pr, fname)) {
		goto DONE;
	}

	postparse_passes(&pr);
	o = calloc(1, sizeof(SAU_Script));
	o->events = pr.events;
	o->name = fname;
	o->sopt = pr.pd->sopt;

DONE:
	fini_Parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SAU_discard_Script(SAU_Script *restrict o) {
	SAU_ScriptEvData *e;
	for (e = o->events; e; ) {
		SAU_ScriptEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
