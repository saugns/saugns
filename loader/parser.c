/* saugns: Script parser module.
 * Copyright (c) 2011-2012, 2017-2021 Joel K. Pettersson
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
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))

struct ScanLookup {
	SAU_ScriptOptions sopt;
	const char *const*math_names;
	const char *const*ramp_names;
	const char *const*wave_names;
};

/*
 * Default script options, used until changed in a script.
 */
static const SAU_ScriptOptions def_sopt = {
	.set = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		SAU_SymTab *restrict st) {
	o->sopt = def_sopt;
	if (!(o->math_names = SAU_SymTab_pool_stra(st,
			SAU_Math_names, SAU_MATH_FUNCTIONS)))
		return false;
	if (!(o->ramp_names = SAU_SymTab_pool_stra(st,
			SAU_Ramp_names, SAU_RAMP_TYPES)))
		return false;
	if (!(o->wave_names = SAU_SymTab_pool_stra(st,
			SAU_Wave_names, SAU_WAVE_TYPES)))
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
	if (SAU_IS_ASCIIVISIBLE(c)) {
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
 * Print warning for scope-opening character in disallowed place.
 */
static void warn_opening_disallowed(SAU_Scanner *restrict o,
		uint8_t open_c) {
	SAU_Scanner_warning(o, NULL, "opening '%c' out of place",
			open_c);
}

/*
 * Print warning for scope-closing character without scope-opening character.
 */
static void warn_closing_without_opening(SAU_Scanner *restrict o,
		uint8_t close_c, uint8_t open_c) {
	SAU_Scanner_warning(o, NULL, "closing '%c' without opening '%c'",
			close_c, open_c);
}

/*
 * Handle '#'-commands.
 */
static uint8_t scan_filter_hashcommands(SAU_Scanner *restrict o, uint8_t c) {
	SAU_File *f = o->f;
	uint8_t next_c = SAU_File_GETC(f);
	if (next_c == '!') {
		++o->sf.char_num;
		return SAU_Scanner_filter_linecomment(o, next_c);
	}
	if (next_c == 'Q') {
		SAU_File_DECP(f);
		SAU_Scanner_close(o);
		return SAU_SCAN_EOF;
	}
	SAU_File_DECP(f);
	return c;
}

static bool scan_symafind(SAU_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SAU_ScanFrame sf_begin = o->sf;
	SAU_SymStr *s = NULL;
	SAU_Scanner_get_symstr(o, &s);
	if (!s) {
		SAU_Scanner_warning(o, NULL,
				"%s name missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SAU_Scanner_warning(o, &sf_begin,
			"invalid %s name '%s'; available are:",
			print_type, s->key);
	SAU_print_names(stra, "\t", stderr);
	return false;
}

static bool scan_mathfunc(SAU_Scanner *restrict o, size_t *restrict found_id) {
	struct ScanLookup *sl = o->data;
	if (!scan_symafind(o, sl->math_names, found_id, "math function"))
		return false;
	uint8_t c;
	c = SAU_Scanner_getc(o);
	if (c == '(')
		return true;
	SAU_Scanner_ungetc(o);
	SAU_Scanner_warning(o, NULL,
"expected '(' following math function name '%s'", SAU_Math_names[*found_id]);
	return false;
}

struct NumParser {
	SAU_Scanner *sc;
	SAU_ScanNumConst_f numconst_f;
	SAU_ScanFrame sf_start;
	bool has_nannum, has_infnum;
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
	} else {
		size_t func_id = 0, read_len = 0;
		SAU_Scanner_ungetc(sc);
		SAU_Scanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0) {
			if (IS_ALPHA(c) && scan_mathfunc(sc, &func_id)) {
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = SAU_Math_val_func[func_id](num);
			} else {
				return NAN; /* silent NaN (nothing was read) */
			}
		}
		if (isnan(num)) {
			o->has_nannum = true;
			return NAN;
		}
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
		if (isnan(num)) {
			o->has_nannum = true;
			goto DEFER;
		}
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	return num;
}
static sauNoinline bool scan_num(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst, float *restrict var) {
	struct NumParser np = {o, scan_numconst, o->sf, false, false};
	uint8_t ws_level = o->ws_level;
	float num = scan_num_r(&np, NUMEXP_NUM, 0);
	SAU_Scanner_setws_level(o, ws_level); // restore if changed
	if (np.has_nannum) {
		SAU_Scanner_warning(o, &np.sf_start,
				"discarding expression containing NaN value");
		return false;
	}
	if (isnan(num)) /* silent NaN (ignored blank expression) */
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

static size_t scan_chanmix_const(SAU_Scanner *restrict o,
		double *restrict val) {
	char c = SAU_File_GETC(o->f);
	switch (c) {
	case 'C':
		*val = 0.f;
		return 1;
	case 'L':
		*val = -1.f;
		return 1;
	case 'R':
		*val = 1.f;
		return 1;
	default:
		SAU_File_DECP(o->f);
		return 0;
	}
}

#define OCTAVES 11
static size_t scan_note_const(SAU_Scanner *restrict o,
		double *restrict val) {
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
	SAU_File *f = o->f;
	struct ScanLookup *sl = o->data;
	size_t len = 0, num_len;
	uint8_t c;
	double freq;
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	c = SAU_File_GETC(f); ++len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = SAU_File_GETC(f); ++len;
	}
	if (c < 'A' || c > 'G') {
		SAU_File_UNGETN(f, len);
		return 0;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = SAU_File_GETC(f); ++len;
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else {
		SAU_File_DECP(f); --len;
	}
	SAU_Scanner_geti(o, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SAU_Scanner_warning(o, NULL,
"invalid note octave number, using 4 (valid range 0-10)");
		octave = 4;
	}
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	*val = (double) freq;
	return len;
}

static size_t scan_phase_const(SAU_Scanner *restrict o,
		double *restrict val) {
	char c = SAU_File_GETC(o->f);
	switch (c) {
	case 'G':
		*val = SAU_GLDA_1_2PI;
		return 1;
	default:
		SAU_File_DECP(o->f);
		return 0;
	}
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

static bool scan_wavetype(SAU_Scanner *restrict o, size_t *restrict found_id) {
	struct ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names,
			found_id, "wave type");
}

static bool scan_ramp_state(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool mult,
		uint32_t *restrict params, uint32_t par_flag) {
	if (!scan_num(o, scan_numconst, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SAU_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SAU_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SAU_RAMPP_STATE;
	*params |= par_flag;
	return true;
}

static bool scan_ramp_param(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool mult,
		uint32_t *restrict params, uint32_t par_flag) {
	if (!SAU_Scanner_tryc(o, '{')) {
		return scan_ramp_state(o, scan_numconst, ramp, mult,
				params, par_flag);
	}
	struct ScanLookup *sl = o->data;
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
					&id, "ramp type")) {
				type = id;
			}
			break; }
		case 't':
			if (scan_time_val(o, &time_ms))
				time_set = true;
			break;
		case 'v':
			if (scan_num(o, scan_numconst, &vt))
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
	*params |= par_flag;
	return true;
}

/*
 * Parser
 */

typedef struct SAU_Parser {
	struct ScanLookup sl;
	SAU_Scanner *sc;
	SAU_SymTab *st;
	SAU_MemPool *mem, *tmp;
	uint32_t call_level;
	/* node state */
	struct ParseLevel *cur_pl;
	SAU_ScriptEvData *events, *last_event;
	SAU_ScriptEvData *group_start, *group_end;
} SAU_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SAU_Parser *restrict o) {
	SAU_destroy_Scanner(o->sc);
	SAU_destroy_SymTab(o->st);
	SAU_destroy_MemPool(o->mem);
	SAU_destroy_MemPool(o->tmp);
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
	SAU_MemPool *mem = SAU_create_MemPool(0);
	SAU_MemPool *tmp = SAU_create_MemPool(0);
	SAU_SymTab *st = SAU_create_SymTab(mem);
	SAU_Scanner *sc = SAU_create_Scanner(st);
	*o = (SAU_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mem = mem;
	o->tmp = tmp;
	if (!sc || !st || !mem || !tmp) goto ERROR;
	if (!init_ScanLookup(&o->sl, st)) goto ERROR;
	sc->filters['#'] = scan_filter_hashcommands;
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
	SCOPE_GROUP = '<',
	SCOPE_BIND = '@',
	SCOPE_NEST = '[',
};

typedef void (*ParseLevel_sub_f)(SAU_Parser *restrict o);

static void parse_in_event(SAU_Parser *restrict o);
static void parse_in_settings(SAU_Parser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_DEFERRED_SUB  = 1<<0, // \a sub_f exited to attempt handling above
	PL_BIND_MULTIPLE = 1<<1, // previous node interpreted as set of nodes
	PL_NEW_EVENT_FORK = 1<<2,
	PL_ACTIVE_EV = 1<<3,
	PL_ACTIVE_OP = 1<<4,
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
	SAU_ScriptEvData *event, *last_event;
	SAU_ScriptListData *nest_list;
	SAU_ScriptOpData *nest_last_data;
	SAU_ScriptOpData *ev_first_data, *ev_last_data;
	SAU_ScriptOpData *operator;
	SAU_ScriptListData *last_mods_list;
	SAU_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SAU_ScriptEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t next_wait_ms; /* added for next event */
};

typedef struct SAU_ScriptEvBranch {
	SAU_ScriptEvData *events;
	struct SAU_ScriptEvBranch *prev;
} SAU_ScriptEvBranch;

static bool parse_waittime(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SAU_Scanner_tryc(sc, 't')) {
		if (!pl->ev_last_data) {
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

static void end_operator(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~PL_ACTIVE_OP;
	struct ScanLookup *sl = &o->sl;
	SAU_ScriptOpData *op = pl->operator;
	if (SAU_Ramp_ENABLED(&op->amp)) {
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SAU_Ramp_ENABLED(&op->amp2)) {
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			op->amp2.v0 *= sl->sopt.ampmult;
			op->amp2.vt *= sl->sopt.ampmult;
		}
	}
	SAU_ScriptOpData *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->params |= SAU_POP_PARAMS;
	}
	pl->operator = NULL;
}

static void end_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~PL_ACTIVE_EV;
	SAU_ScriptEvData *e = pl->event;
	end_operator(o);
	pl->last_event = e;
	pl->event = NULL;
	pl->ev_first_data = NULL;
	pl->ev_last_data = NULL;
	SAU_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
	if (!o->group_start)
		o->group_start = group_e;
	o->group_end = group_e;
}

static void begin_event(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict prev_data,
		bool is_composite) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptEvData *e, *pve;
	end_event(o);
	e = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptEvData));
	pl->event = e;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	if (prev_data != NULL) {
		pve = prev_data->event;
		e->root_ev = prev_data->obj->root_event;
		if (is_composite) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				SAU_ScriptEvBranch *fork =
					SAU_MemPool_alloc(o->tmp,
						sizeof(SAU_ScriptEvBranch));
				fork->events = e;
				if (!pl->main_ev)
					pl->main_ev = pve;
				fork->prev = pl->main_ev->forks;
				pl->main_ev->forks = fork;
				pl->pl_flags &= ~PL_NEW_EVENT_FORK;
			} else {
				pve->next = e;
			}
		}
	}
	if (!is_composite) {
		if (!o->events)
			o->events = e;
		else
			o->last_event->next = e;
		o->last_event = e;
		pl->main_ev = NULL;
	}
	pl->pl_flags |= PL_ACTIVE_EV;
}

static void begin_operator(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict pop,
		bool is_composite) {
	struct ParseLevel *pl = o->cur_pl;
	struct ScanLookup *sl = &o->sl;
	SAU_ScriptEvData *e = pl->event;
	SAU_ScriptOpData *op;
	/*
	 * It is assumed that a valid event exists.
	 */
	end_operator(o);
	op = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptOpData));
	pl->operator = op;
	pl->last_mods_list = NULL; /* now track for this node */
	if (!is_composite)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	/*
	 * Initialize node.
	 */
	SAU_Ramp_reset(&op->freq);
	SAU_Ramp_reset(&op->freq2);
	SAU_Ramp_reset(&op->amp);
	SAU_Ramp_reset(&op->amp2);
	SAU_Ramp_reset(&op->pan);
	if (pop != NULL) {
		op->use_type = pop->use_type;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SAU_SDOP_NESTED | SAU_SDOP_MULTIPLE);
		if (is_composite) {
			pop->op_flags |= SAU_SDOP_HAS_COMPOSITE;
			op->op_flags |= SAU_SDOP_TIME_DEFAULT;
			/* default: previous or infinite time */
		}
		op->time_ms = pop->time_ms;
		op->wave = pop->wave;
		op->phase = pop->phase;
		op->obj = pop->obj;
	} else {
		/*
		 * New operator with initial parameter values.
		 *
		 * Time default: depends on context.
		 */
		op->op_flags = SAU_SDOP_TIME_DEFAULT;
		op->time_ms = sl->sopt.def_time_ms;
		op->use_type = pl->use_type;
		if (op->use_type == SAU_POP_CARR) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SAU_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SAU_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SAU_RAMPP_STATE;
		op->pan.v0 = sl->sopt.def_chanmix;
		op->pan.flags |= SAU_RAMPP_STATE;
		op->obj = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptOpObj));
		op->obj->root_event = e;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either to the
	 * current event node, or to an operator node (ordinary or multiple)
	 * in the case of operator linking/nesting.
	 */
	if (pop != NULL || !pl->nest_list) {
		if (!e->op_objs.first_item)
			e->op_objs.first_item = op;
		else
			pl->ev_last_data->next_item = op;
		pl->ev_last_data = op;
	} else {
		if (!pl->nest_list->first_item)
			pl->nest_list->first_item = op;
		if (pl->nest_last_data != NULL)
			pl->nest_last_data->next_item = op;
		pl->nest_last_data = op;
	}
	if (!pl->ev_first_data) /* design placeholder leftover */
		pl->ev_first_data = op;
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
	pl->pl_flags |= PL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict previous,
		bool is_composite) {
	struct ParseLevel *pl = o->cur_pl;
	uint8_t use_type = (previous != NULL) ?
		previous->use_type :
		pl->use_type;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->sub_f != parse_in_event ||
			pl->next_wait_ms ||
			((previous != NULL || use_type == SAU_POP_CARR)
			 && pl->event->op_objs.first_item != NULL) ||
			is_composite)
		begin_event(o, previous, is_composite);
	begin_operator(o, previous, is_composite);
}

static void flush_durgroup(SAU_Parser *restrict o) {
	if (o->group_start != NULL) {
		o->group_end->group_backref = o->group_start;
		o->group_start = o->group_end = NULL;
	}
}

static void enter_level(SAU_Parser *restrict o,
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
		pl->pl_flags = parent_pl->pl_flags & (PL_BIND_MULTIPLE);
		if (newscope == SCOPE_SAME) {
			pl->scope = parent_pl->scope;
			pl->nest_list = parent_pl->nest_list;
		}
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		if (newscope == SCOPE_GROUP) {
			pl->nest_list = parent_pl->nest_list;
		}
		if (newscope == SCOPE_NEST) {
			SAU_ScriptOpData *parent_on = parent_pl->operator;
			pl->nest_list = SAU_MemPool_alloc(o->mem,
					sizeof(SAU_ScriptListData));
			pl->nest_list->use_type = use_type;
			if (!parent_on->mods)
				parent_on->mods = pl->nest_list;
			else
				parent_pl->last_mods_list->next_list =
					pl->nest_list;
			parent_pl->last_mods_list = pl->nest_list;
		}
	}
	pl->use_type = use_type;
}

static void leave_level(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_operator(o);
	if (pl->set_label != NULL) {
		SAU_Scanner_warning(o->sc, NULL,
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
	if (pl->scope == SCOPE_GROUP) {
		if (pl->pl_flags & PL_ACTIVE_EV) {
			end_event(o);
			pl->parent->pl_flags |= PL_ACTIVE_EV;
			pl->parent->event = pl->event;
		}
		if (pl->last_event != NULL)
			pl->parent->last_event = pl->last_event;
	}
	if (pl->scope == SCOPE_BIND) {
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->ev_first_data != NULL) {
			pl->parent->pl_flags |= PL_BIND_MULTIPLE;
			begin_node(o, pl->ev_first_data, false);
		}
	}
}

/*
 * Main parser functions
 */

static void parse_in_settings(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	struct ScanLookup *sl = &o->sl;
	SAU_Scanner *sc = o->sc;
	pl->sub_f = parse_in_settings;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case 'a':
			if (scan_num(sc, NULL, &sl->sopt.ampmult))
				sl->sopt.set |= SAU_SOPT_AMPMULT;
			break;
		case 'c':
			if (scan_num(sc, scan_chanmix_const,
						&sl->sopt.def_chanmix))
				sl->sopt.set |= SAU_SOPT_DEF_CHANMIX;
			break;
		case 'f':
			if (scan_num(sc, scan_note_const, &sl->sopt.def_freq))
				sl->sopt.set |= SAU_SOPT_DEF_FREQ;
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
				sl->sopt.set |= SAU_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.set |= SAU_SOPT_DEF_RELFREQ;
			break;
		case 't':
			if (scan_time_val(sc, &sl->sopt.def_time_ms))
				sl->sopt.set |= SAU_SOPT_DEF_TIME;
			break;
		default:
			goto DEFER;
		}
	}
	return;
DEFER:
	SAU_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static bool parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope);

static bool parse_ev_amp(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpData *op = pl->operator;
	scan_ramp_param(sc, NULL, &op->amp, false,
			&op->params, SAU_POPP_AMP);
	if (SAU_Scanner_tryc(sc, ',')) {
		scan_ramp_param(sc, NULL, &op->amp2, false,
				&op->params, SAU_POPP_AMP2);
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, SAU_POP_AMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_chanmix(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpData *op = pl->operator;
	if (op->op_flags & SAU_SDOP_NESTED)
		return true; // reject
	scan_ramp_param(sc, scan_chanmix_const, &op->pan, false,
			&op->params, SAU_POPP_PAN);
	return false;
}

static bool parse_ev_freq(SAU_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	SAU_ScanNumConst_f numconst_f = rel_freq ? NULL : scan_note_const;
	scan_ramp_param(sc, numconst_f, &op->freq, rel_freq,
			&op->params, SAU_POPP_FREQ);
	if (SAU_Scanner_tryc(sc, ',')) {
		scan_ramp_param(sc, numconst_f, &op->freq2, rel_freq,
			&op->params, SAU_POPP_FREQ2);
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, SAU_POP_FMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpData *op = pl->operator;
	if (scan_num(sc, scan_phase_const, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->params |= SAU_POPP_PHASE;
	}
	if (SAU_Scanner_tryc(sc, '+') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, SAU_POP_PMOD, SCOPE_NEST);
	}
	return false;
}

static void parse_in_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	pl->sub_f = parse_in_event;
	for (;;) {
		SAU_ScriptOpData *op = pl->operator;
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case '\\':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, false);
			}
			break;
		case 'a':
			if (parse_ev_amp(o)) goto DEFER;
			break;
		case 'c':
			if (parse_ev_chanmix(o)) goto DEFER;
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
		case 's':
			if (scan_time_val(sc, &op->silence_ms))
				op->params |= SAU_POPP_SILENCE;
			break;
		case 't':
			if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_SDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				op->time_ms = SAU_TIME_INF;
			} else {
				if (!scan_time_val(sc, &op->time_ms))
					break;
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
			}
			op->params |= SAU_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			op->params |= SAU_POPP_WAVE;
			break; }
		default:
			goto DEFER;
		}
	}
	return;
DEFER:
	SAU_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static bool parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel pl;
	SAU_SymStr *label;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope);
	SAU_Scanner *sc = o->sc;
	for (;;) {
		/*
		 * Return to any sub-parsing routine.
		 *
		 * May happen in a new nested parse_level() call.
		 */
		if (pl.sub_f != NULL && !(pl.pl_flags & PL_DEFERRED_SUB))
			pl.sub_f(o);
		pl.pl_flags &= ~PL_DEFERRED_SUB;
		/*
		 * Parse main tokens.
		 */
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
				pl.sub_f = NULL;
				pl.ev_first_data = NULL;
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
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			begin_node(o, pl.operator, true);
			parse_in_event(o);
			break;
		case '<':
			if (parse_level(o, pl.use_type, SCOPE_GROUP))
				goto RETURN;
			break;
		case '>':
			if (pl.scope == SCOPE_GROUP) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@':
			if (SAU_Scanner_tryc(sc, '[')) {
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
				SAU_Scanner_warning(sc, NULL,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.sub_f = NULL;
			label = scan_label(sc, c);
			if (label != NULL) {
				SAU_ScriptOpData *ref = label->data;
				if (!ref)
					SAU_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_node(o, ref, false);
					parse_in_event(o);
				}
			}
			break;
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(o, NULL, false);
			pl.operator->wave = wave;
			parse_in_event(o);
			break; }
		case 'S':
			parse_in_settings(o);
			break;
		case '[':
			warn_opening_disallowed(sc, '[');
			break;
		case '\\':
			if (pl.nest_list != NULL)
				goto INVALID;
			parse_waittime(o);
			break;
		case ']':
			if (pl.scope == SCOPE_NEST) {
				end_operator(o);
			}
			if (pl.scope > SCOPE_GROUP) {
				endscope = true;
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '{':
			warn_opening_disallowed(sc, '{');
			break;
		case '|':
			if (pl.nest_list != NULL)
				goto INVALID;
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			end_event(o);
			if (!o->group_start) {
				SAU_Scanner_warning(sc, NULL,
"no sounds precede time separator");
				break;
			}
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
	if (newscope > SCOPE_GROUP)
		warn_eof_without_closing(sc, ']');
	else if (newscope == SCOPE_GROUP)
		warn_eof_without_closing(sc, '>');
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
static const char *parse_file(SAU_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SAU_Scanner *sc = o->sc;
	const char *name;
	if (!SAU_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, SAU_POP_CARR, SCOPE_TOP);
	name = sc->f->path;
	SAU_Scanner_close(sc);
	return name;
}

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SAU_ScriptEvData *restrict e_last) {
	SAU_ScriptEvData *e, *e_after = e_last->next;
	uint32_t wait = 0, waitcount = 0;
	for (e = e_last->group_backref; e != e_after; ) {
		for (SAU_ScriptOpData *op = e->op_objs.first_item;
				op != NULL; op = op->next_item) {
			if (wait < op->time_ms)
				wait = op->time_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = e_last->group_backref; e != e_after; ) {
		for (SAU_ScriptOpData *op = e->op_objs.first_item;
				op != NULL; op = op->next_item) {
			if ((op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				/* fill in sensible default time */
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				op->time_ms = wait + waitcount;
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	e_last->group_backref = NULL;
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static inline void time_ramp(SAU_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SAU_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static void time_operator(SAU_ScriptOpData *restrict op) {
	SAU_ScriptEvData *e = op->event;
	if ((op->op_flags & (SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) ==
			(SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) {
		op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		if (!(op->op_flags & SAU_SDOP_HAS_COMPOSITE))
			op->time_ms = SAU_TIME_INF;
	}
	if (op->time_ms != SAU_TIME_INF) {
		time_ramp(&op->freq, op->time_ms);
		time_ramp(&op->freq2, op->time_ms);
		time_ramp(&op->amp, op->time_ms);
		time_ramp(&op->amp2, op->time_ms);
		time_ramp(&op->pan, op->time_ms);
		if (!(op->op_flags & SAU_SDOP_SILENCE_ADDED)) {
			op->time_ms += op->silence_ms;
			op->op_flags |= SAU_SDOP_SILENCE_ADDED;
		}
	}
	if ((e->ev_flags & SAU_SDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			e->next->wait_ms += op->time_ms;
		e->ev_flags &= ~SAU_SDEV_ADD_WAIT_DURATION;
	}
	for (SAU_ScriptListData *list = op->mods;
			list != NULL; list = list->next_list) {
		for (SAU_ScriptOpData *sub_op = list->first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			time_operator(sub_op);
		}
	}
}

static void time_event(SAU_ScriptEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	SAU_ScriptOpData *sub_op;
	for (sub_op = e->op_objs.first_item;
			sub_op != NULL; sub_op = sub_op->next_item) {
		time_operator(sub_op);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	SAU_ScriptEvBranch *fork = e->forks;
	while (fork != NULL) {
		SAU_ScriptEvData *ce = fork->events;
		SAU_ScriptOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = ce->op_objs.first_item;
		ce_op_prev = ce_op->on_prev;
		e_op = ce_op_prev;
		if ((e_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0)
			e_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		for (;;) {
			ce->wait_ms += ce_op_prev->time_ms;
			if ((ce_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				ce_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				if ((ce_op->op_flags & (SAU_SDOP_NESTED | SAU_SDOP_HAS_COMPOSITE)) == SAU_SDOP_NESTED)
					ce_op->time_ms = SAU_TIME_INF;
				else
					ce_op->time_ms = ce_op_prev->time_ms
						- ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time_ms == SAU_TIME_INF)
				e_op->time_ms = SAU_TIME_INF;
			else if (e_op->time_ms != SAU_TIME_INF)
				e_op->time_ms += ce_op->time_ms +
					(ce->wait_ms - ce_op_prev->time_ms);
			ce_op->params &= ~SAU_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = ce->op_objs.first_item;
		}
		fork = fork->prev;
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
	SAU_ScriptEvData *ce = e->forks->events;
	SAU_ScriptEvData *se = e->next, *se_prev = e;
	uint32_t wait_ms = 0;
	uint32_t added_wait_ms = 0;
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
		if (se->next && (wait_ms + se->next->wait_ms)
				<= (ce->wait_ms + added_wait_ms)) {
			se_prev = se;
			se = se->next;
			continue;
		}
		/*
		 * Insert next composite before or after
		 * the next event of the ordinary sequence.
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
	e->forks = e->forks->prev;
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SAU_Parser *restrict o) {
	SAU_ScriptEvData *e;
	for (e = o->events; e != NULL; e = e->next) {
		time_event(e);
		if (e->group_backref != NULL) time_durgroup(e);
	}
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (e = o->events; e != NULL; e = e->next) {
		while (e->forks != NULL) flatten_events(e);
		/*
		 * Track sequence of references and later use here.
		 */
		SAU_ScriptOpData *sub_op;
		for (sub_op = e->op_objs.first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			SAU_ScriptOpData *prev_ref = sub_op->obj->last_ref;
			if (prev_ref != NULL) {
				sub_op->on_prev = prev_ref;
				prev_ref->op_flags |= SAU_SDOP_LATER_USED;
				prev_ref->event->ev_flags |=
					SAU_SDEV_VOICE_LATER_USED;
			}
			sub_op->obj->last_ref = sub_op;
		}
	}
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SAU_Script* SAU_load_Script(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SAU_Parser pr;
	SAU_Script *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	postparse_passes(&pr);
	o = SAU_MemPool_alloc(pr.mem, sizeof(SAU_Script));
	o->events = pr.events;
	o->name = name;
	o->sopt = pr.sl.sopt;
	o->mem = pr.mem;
	pr.mem = NULL; // keep in result
DONE:
	fini_Parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SAU_discard_Script(SAU_Script *restrict o) {
	if (!o)
		return;
	SAU_destroy_MemPool(o->mem);
}
