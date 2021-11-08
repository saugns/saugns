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
	const char *const*math_names;
	const char *const*ramp_names;
	const char *const*wave_names;
};

/*
 * Default script options, used until changed in a script.
 */
static const SGS_ScriptOptions def_sopt = {
	.set = 0,
	.ampmult = 1.f,
	.A4_freq = 440.f,
	.def_time_ms = 1000,
	.def_freq = 440.f,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		SGS_SymTab *restrict st) {
	o->sopt = def_sopt;
	if (!(o->math_names = SGS_SymTab_pool_stra(st,
			SGS_Math_names, SGS_MATH_FUNCTIONS)))
		return false;
	if (!(o->ramp_names = SGS_SymTab_pool_stra(st,
			SGS_Ramp_names, SGS_RAMP_TYPES)))
		return false;
	if (!(o->wave_names = SGS_SymTab_pool_stra(st,
			SGS_Wave_names, SGS_WAVE_TYPES)))
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

static bool scan_symafind(SGS_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SGS_ScanFrame sf_begin = o->sf;
	SGS_SymStr *s = NULL;
	SGS_Scanner_get_symstr(o, &s);
	if (!s) {
		SGS_Scanner_warning(o, NULL, "%s name missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SGS_Scanner_warning(o, &sf_begin,
			"invalid %s name '%s'; available are:",
			print_type, s->key);
	SGS_print_names(stra, "\t", stderr);
	return false;
}

static bool scan_mathfunc(SGS_Scanner *restrict o, size_t *restrict found_id) {
	struct ScanLookup *sl = o->data;
	if (!scan_symafind(o, sl->math_names, found_id, "math function"))
		return false;
	if (SGS_Scanner_tryc(o, '('))
		return true;
	SGS_Scanner_warning(o, NULL,
"expected '(' following math function name '%s'", SGS_Math_names[*found_id]);
	return false;
}

struct NumParser {
	SGS_Scanner *sc;
	SGS_ScanNumConst_f numconst_f;
	SGS_ScanFrame sf_start;
	bool has_nannum, has_infnum;
	bool after_rpar;
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
	uint8_t ws_level = sc->ws_level;
	double num;
	uint8_t c;
	if (level == 1 && ws_level != SGS_SCAN_WS_NONE)
		SGS_Scanner_setws_level(sc, SGS_SCAN_WS_NONE);
	c = SGS_Scanner_getc(sc);
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else if (c == '+' || c == '-') {
		num = scan_num_r(o, NUMEXP_ADT, level+1);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else {
		size_t func_id = 0, read_len = 0;
		SGS_Scanner_ungetc(sc);
		SGS_Scanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0) {
			if (IS_ALPHA(c) && scan_mathfunc(sc, &func_id)) {
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = SGS_Math_val_func[func_id](num);
			} else {
				goto REJECT; /* silent NaN (nothing was read) */
			}
		}
		if (isnan(num)) {
			o->has_nannum = true;
			goto REJECT;
		}
	}
	if (pri == NUMEXP_NUM) goto ACCEPT; /* defer all operations */
	for (;;) {
		bool rpar_mlt = false;
		if (isinf(num)) o->has_infnum = true;
		c = SGS_Scanner_getc(sc);
		if (pri < NUMEXP_MLT) {
			rpar_mlt = o->after_rpar;
			o->after_rpar = false;
		}
		switch (c) {
		case '(':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_SUB, level+1);
			break;
		case ')':
			if (pri != NUMEXP_SUB) goto DEFER;
			o->after_rpar = true;
			goto ACCEPT;
		case '^':
			if (pri > NUMEXP_POW) goto DEFER;
			num = pow(num, scan_num_r(o, NUMEXP_POW, level));
			break;
		case '*':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '/':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num /= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '%':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num = fmod(num, scan_num_r(o, NUMEXP_MLT, level));
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
			if (rpar_mlt &&
			    (c != SGS_SCAN_SPACE && c != SGS_SCAN_LNBRK)) {
				SGS_Scanner_ungetc(sc);
				double rval = scan_num_r(o, NUMEXP_MLT, level);
				if (isnan(rval)) goto ACCEPT;
				num *= rval;
				break;
			}
			if (pri == NUMEXP_SUB && level > 0) {
				SGS_Scanner_warning(sc, &o->sf_start,
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
	SGS_Scanner_ungetc(sc);
ACCEPT:
	if (0)
REJECT: {
		num = NAN;
	}
	if (ws_level != sc->ws_level)
		SGS_Scanner_setws_level(sc, ws_level);
	return num;
}
static sgsNoinline bool scan_num(SGS_Scanner *restrict o,
		SGS_ScanNumConst_f scan_numconst, float *restrict var) {
	struct NumParser np = {o, scan_numconst, o->sf, false, false, false};
	float num = scan_num_r(&np, NUMEXP_SUB, 0);
	if (np.has_nannum) {
		SGS_Scanner_warning(o, &np.sf_start,
				"discarding expression containing NaN value");
		return false;
	}
	if (isnan(num)) /* silent NaN (ignored blank expression) */
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

static size_t scan_chanmix_const(SGS_Scanner *restrict o,
		double *restrict val) {
	char c = SGS_File_GETC(o->f);
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
		SGS_File_DECP(o->f);
		return 0;
	}
}

#define OCTAVES 11
static size_t scan_note_const(SGS_Scanner *restrict o,
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
	SGS_File *f = o->f;
	struct ScanLookup *sl = o->data;
	size_t len = 0, num_len;
	uint8_t c;
	double freq;
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	c = SGS_File_GETC(f); ++len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = SGS_File_GETC(f); ++len;
	}
	if (c < 'A' || c > 'G') {
		SGS_File_UNGETN(f, len);
		return 0;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = SGS_File_GETC(f); ++len;
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else {
		SGS_File_DECP(f); --len;
	}
	SGS_Scanner_geti(o, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SGS_Scanner_warning(o, NULL,
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

static size_t scan_phase_const(SGS_Scanner *restrict o,
		double *restrict val) {
	char c = SGS_File_GETC(o->f);
	switch (c) {
	case 'G':
		*val = SGS_GLDA_1_2PI;
		return 1;
	default:
		SGS_File_DECP(o->f);
		return 0;
	}
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

static bool scan_wavetype(SGS_Scanner *restrict o, size_t *restrict found_id) {
	struct ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names, found_id, "wave type");
}

static bool scan_ramp_state(SGS_Scanner *restrict o,
		SGS_ScanNumConst_f scan_numconst,
		SGS_Ramp *restrict ramp, bool ratio) {
	if (!scan_num(o, scan_numconst, &ramp->v0))
		return false;
	if (ratio) {
		ramp->flags |= SGS_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SGS_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SGS_RAMPP_STATE;
	return true;
}

static bool scan_ramp_param(SGS_Scanner *restrict o,
		SGS_ScanNumConst_f scan_numconst,
		SGS_Ramp *restrict ramp, bool ratio) {
	bool state = scan_ramp_state(o, scan_numconst, ramp, ratio);
	if (!SGS_Scanner_tryc(o, '{'))
		return state;
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
	SGS_MemPool *mem, *tmp;
	uint32_t call_level;
	/* node state */
	struct ParseLevel *cur_pl;
	SGS_ScriptEvData *events, *last_event;
	SGS_ScriptEvData *group_start, *group_end;
} SGS_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SGS_Parser *restrict o) {
	SGS_destroy_Scanner(o->sc);
	SGS_destroy_SymTab(o->st);
	SGS_destroy_MemPool(o->mem);
	SGS_destroy_MemPool(o->tmp);
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
	SGS_MemPool *mem = SGS_create_MemPool(0);
	SGS_MemPool *tmp = SGS_create_MemPool(0);
	SGS_SymTab *st = SGS_create_SymTab(mem);
	SGS_Scanner *sc = SGS_create_Scanner(st);
	*o = (SGS_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mem = mem;
	o->tmp = tmp;
	if (!sc || !st || !mem || !tmp) goto ERROR;
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
	SCOPE_SAME = 0, // specially handled inner copy of parent scope
	SCOPE_GROUP,    // '<...>' or top scope
	SCOPE_BIND,     // '@{...}'
	SCOPE_NEST,     // '[...]'
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
	PL_NEW_EVENT_FORK = 1<<2,
	PL_ACTIVE_EV      = 1<<3,
	PL_ACTIVE_OP      = 1<<4,
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
	SGS_ScriptEvData *event;
	SGS_ScriptListData *nest_list;
	SGS_ScriptOpRef *nest_last_data;
	SGS_ScriptOpRef *ev_first_data, *ev_last_data;
	SGS_ScriptOpRef *operator;
	SGS_ScriptListData *last_mods_list;
	SGS_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SGS_ScriptEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t next_wait_ms; /* added for next event */
};

typedef struct SGS_ScriptEvBranch {
	SGS_ScriptEvData *events;
	struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

static SGS_Ramp *create_ramp(SGS_Parser *restrict o,
		bool mult, uint32_t par_flag) {
	struct ScanLookup *sl = &o->sl;
	SGS_Ramp *ramp = SGS_MemPool_alloc(o->mem, sizeof(SGS_Ramp));
	float v0 = 0.f;
	if (!ramp)
		return NULL;
	ramp->type = SGS_RAMP_LIN; // default if goal enabled
	switch (par_flag) {
	case SGS_PRAMP_PAN:
		v0 = sl->sopt.def_chanmix;
		break;
	case SGS_PRAMP_AMP:
		v0 = 1.0f; /* multiplied with sl->sopt.ampmult separately */
		break;
	case SGS_PRAMP_AMP2:
		v0 = 0.f;
		break;
	case SGS_PRAMP_FREQ:
		v0 = mult ?
			sl->sopt.def_relfreq :
			sl->sopt.def_freq;
		break;
	case SGS_PRAMP_FREQ2:
		v0 = 0.f;
		break;
	default:
		return NULL;
	}
	ramp->v0 = v0;
	ramp->flags |= SGS_RAMPP_STATE;
	if (mult) {
		ramp->flags |= SGS_RAMPP_STATE_RATIO;
	}
	return ramp;
}

static bool parse_ramp(SGS_Parser *restrict o,
		SGS_ScanNumConst_f scan_numconst,
		SGS_Ramp **restrict rampp, bool mult,
		uint32_t ramp_id) {
	if (!*rampp) {
		*rampp = create_ramp(o, mult, ramp_id);
		(*rampp)->flags &= ~SGS_RAMPP_STATE; // only set on parse
	}
	return scan_ramp_param(o->sc, scan_numconst, *rampp, mult);
}

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

static void end_operator(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~PL_ACTIVE_OP;
	SGS_ScriptOpRef *op = pl->operator;
	SGS_ProgramOpData *od = op->data;
	if (od->amp) {
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			od->amp->v0 *= o->sl.sopt.ampmult;
			od->amp->vt *= o->sl.sopt.ampmult;
		}
	}
	if (od->amp2) {
		if (!(op->op_flags & SGS_SDOP_NESTED)) {
			od->amp2->v0 *= o->sl.sopt.ampmult;
			od->amp2->vt *= o->sl.sopt.ampmult;
		}
	}
	SGS_ScriptOpRef *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		od->params = SGS_POP_PARAMS;
	}
	pl->operator = NULL;
}

static void end_event(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~PL_ACTIVE_EV;
	SGS_ScriptEvData *e = pl->event;
	end_operator(o);
	pl->event = NULL;
	pl->ev_first_data = NULL;
	pl->ev_last_data = NULL;
	SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
	if (!o->group_start)
		o->group_start = group_e;
	o->group_end = group_e;
}

static void begin_event(SGS_Parser *restrict o,
		SGS_ScriptOpRef *restrict prev_data,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e, *pve;
	end_event(o);
	e = SGS_MemPool_alloc(o->mem, sizeof(SGS_ScriptEvData));
	pl->event = e;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	if (prev_data != NULL) {
		if (prev_data->op_flags & SGS_SDOP_NESTED)
			e->ev_flags |= SGS_SDEV_IMPLICIT_TIME;
		pve = prev_data->event;
		e->root_ev = prev_data->obj->root_event;
		if (is_compstep) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				SGS_ScriptEvBranch *fork =
					SGS_MemPool_alloc(o->tmp,
						sizeof(SGS_ScriptEvBranch));
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

static void begin_operator(SGS_Parser *restrict o,
		SGS_ScriptOpRef *restrict pop,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e = pl->event;
	SGS_ScriptOpRef *op;
	SGS_ProgramOpData *od, *pod = (pop != NULL) ? pop->data : NULL;
	/*
	 * It is assumed that a valid event exists.
	 */
	end_operator(o);
	op = SGS_MemPool_alloc(o->mem, sizeof(SGS_ScriptOpRef));
	od = SGS_MemPool_alloc(o->mem, sizeof(SGS_ProgramOpData));
	op->data = od;
	pl->operator = op;
	pl->last_mods_list = NULL; /* now track for this node */
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	/*
	 * Initialize node.
	 */
	if (pop != NULL) {
		od->use_type = pod->use_type;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SGS_SDOP_NESTED | SGS_SDOP_MULTIPLE);
		od->time = (SGS_Time){pod->time.v_ms,
			(pod->time.flags & SGS_TIMEP_IMPLICIT)};
		od->wave = pod->wave;
		od->phase = pod->phase;
		op->obj = pop->obj;
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		od->use_type = pl->use_type;
		od->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
		if (od->use_type == SGS_POP_CARR) {
			od->pan = create_ramp(o, false, SGS_PRAMP_PAN);
			od->freq = create_ramp(o, false, SGS_PRAMP_FREQ);
		} else {
			op->op_flags |= SGS_SDOP_NESTED;
			od->freq = create_ramp(o, true, SGS_PRAMP_FREQ);
		}
		od->amp = create_ramp(o, false, SGS_PRAMP_AMP);
		op->obj = SGS_MemPool_alloc(o->mem, sizeof(SGS_ScriptOpObj));
		op->obj->root_event = e;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	if (pop != NULL || !pl->nest_list) {
		if (!e->main_refs.first_item)
			e->main_refs.first_item = op;
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
		SGS_ScriptOpRef *restrict previous,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	uint8_t use_type = (previous != NULL) ?
		previous->data->use_type :
		pl->use_type;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->sub_f != parse_in_event ||
			pl->next_wait_ms ||
			((previous != NULL || use_type == SGS_POP_CARR)
			 && pl->event->main_refs.first_item != NULL) ||
			is_compstep)
		begin_event(o, previous, is_compstep);
	begin_operator(o, previous, is_compstep);
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
			SGS_ScriptOpRef *parent_on = parent_pl->operator;
			pl->nest_list = SGS_MemPool_alloc(o->mem,
					sizeof(SGS_ScriptListData));
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
	if (pl->scope == SCOPE_GROUP) {
		if (pl->pl_flags & PL_ACTIVE_EV) {
			end_event(o);
			pl->parent->pl_flags |= PL_ACTIVE_EV;
			pl->parent->event = pl->event;
		}
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
				sl->sopt.set |= SGS_SOPT_AMPMULT;
			break;
		case 'c':
			if (scan_num(sc, scan_chanmix_const,
						&sl->sopt.def_chanmix))
				sl->sopt.set |= SGS_SOPT_DEF_CHANMIX;
			break;
		case 'f':
			if (scan_num(sc, scan_note_const, &sl->sopt.def_freq))
				sl->sopt.set |= SGS_SOPT_DEF_FREQ;
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
				sl->sopt.set |= SGS_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.set |= SGS_SOPT_DEF_RELFREQ;
			break;
		case 't':
			if (scan_time_val(sc, &sl->sopt.def_time_ms))
				sl->sopt.set |= SGS_SOPT_DEF_TIME;
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
	SGS_ScriptOpRef *op = pl->operator;
	SGS_ProgramOpData *od = op->data;
	parse_ramp(o, NULL, &od->amp, false, SGS_PRAMP_AMP);
	if (SGS_Scanner_tryc(sc, ',') && SGS_Scanner_tryc(sc, 'w')) {
		parse_ramp(o, NULL, &od->amp2, false, SGS_PRAMP_AMP2);
		if (SGS_Scanner_tryc(sc, '[')) {
			parse_level(o, SGS_POP_AMOD, SCOPE_NEST);
		}
	}
	return false;
}

static bool parse_ev_chanmix(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptOpRef *op = pl->operator;
	SGS_ProgramOpData *od = op->data;
	if (op->op_flags & SGS_SDOP_NESTED)
		return true; // reject
	parse_ramp(o, scan_chanmix_const, &od->pan, false, SGS_PRAMP_PAN);
	return false;
}

static bool parse_ev_freq(SGS_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	SGS_ScriptOpRef *op = pl->operator;
	SGS_ProgramOpData *od = op->data;
	if (rel_freq && !(op->op_flags & SGS_SDOP_NESTED))
		return true; // reject
	SGS_ScanNumConst_f numconst_f = rel_freq ? NULL : scan_note_const;
	parse_ramp(o, numconst_f, &od->freq, rel_freq, SGS_PRAMP_FREQ);
	if (SGS_Scanner_tryc(sc, ',') && SGS_Scanner_tryc(sc, 'w')) {
		parse_ramp(o, numconst_f, &od->freq2,
				rel_freq, SGS_PRAMP_FREQ2);
		if (SGS_Scanner_tryc(sc, '[')) {
			parse_level(o, SGS_POP_FMOD, SCOPE_NEST);
		}
	}
	return false;
}

static bool parse_ev_phase(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	SGS_ScriptOpRef *op = pl->operator;
	SGS_ProgramOpData *od = op->data;
	if (scan_num(sc, scan_phase_const, &od->phase)) {
		od->phase = fmod(od->phase, 1.f);
		if (od->phase < 0.f)
			od->phase += 1.f;
		od->params |= SGS_POPP_PHASE;
	}
	if (SGS_Scanner_tryc(sc, '[')) {
		parse_level(o, SGS_POP_PMOD, SCOPE_NEST);
	}
	if (SGS_Scanner_tryc(sc, ',')) {
		if (SGS_Scanner_tryc(sc, 'f') && SGS_Scanner_tryc(sc, '[')) {
			parse_level(o, SGS_POP_FPMOD, SCOPE_NEST);
		}
	}
	return false;
}

static void parse_in_event(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_Scanner *sc = o->sc;
	pl->sub_f = parse_in_event;
	for (;;) {
		SGS_ScriptOpRef *op = pl->operator;
		SGS_ProgramOpData *od = op->data;
		uint8_t c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
		case '/':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, false);
			}
			break;
		case '\\':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, true);
				pl->event->ev_flags |= SGS_SDEV_FROM_GAPSHIFT;
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
		case 't':
			if (SGS_Scanner_tryc(sc, 'd')) {
				od->time = (SGS_Time){o->sl.sopt.def_time_ms,
					0};
			} else if (SGS_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SGS_SDOP_NESTED)) {
					SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				od->time = (SGS_Time){o->sl.sopt.def_time_ms,
					SGS_TIMEP_SET | SGS_TIMEP_IMPLICIT};
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				od->time = (SGS_Time){time_ms, SGS_TIMEP_SET};
			}
			od->params |= SGS_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			od->wave = wave;
			od->params |= SGS_POPP_WAVE;
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
			if (!pl.parent) {
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
				SGS_Scanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			pl.set_label = label = scan_label(sc, c);
			break;
		case '/':
			if (pl.nest_list != NULL)
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
			if ((pl.operator->data->time.flags & // TODO: tidy...
			     (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT)) ==
			    (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
				SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' separator");
			begin_node(o, pl.operator, true);
			pl.event->ev_flags |= SGS_SDEV_WAIT_PREV_DUR;
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
				SGS_ScriptOpRef *ref = label->data;
				if (!ref)
					SGS_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_node(o, ref, false);
					parse_in_event(o);
				}
			}
			break;
		case 'O': {
			size_t wave;
			SGS_ProgramOpData *od;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(o, NULL, false);
			od = pl.operator->data;
			od->wave = wave;
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
	if (newscope > SCOPE_GROUP)
		warn_eof_without_closing(sc, ']');
	else if (pl.parent != NULL)
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
static const char *parse_file(SGS_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SGS_Scanner *sc = o->sc;
	const char *name;
	if (!SGS_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, SGS_POP_CARR, SCOPE_GROUP);
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
		for (SGS_ScriptOpRef *op = e->main_refs.first_item;
				op != NULL; op = op->next_item) {
			SGS_ProgramOpData *od = op->data;
			if (!(od->time.flags & SGS_TIMEP_SET)) {
				/* fill in sensible default time */
				od->time.v_ms = cur_longest + wait_sum;
				od->time.flags |= SGS_TIMEP_SET;
				if (e->dur_ms < od->time.v_ms)
					e->dur_ms = od->time.v_ms;
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
	if (!ramp)
		return;
	if (!(ramp->flags & SGS_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static uint32_t time_operator(SGS_ScriptOpRef *restrict op) {
	SGS_ProgramOpData *od = op->data;
	uint32_t dur_ms = od->time.v_ms;
	if (!(od->params & SGS_POPP_TIME))
		op->event->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
	if (!(od->time.flags & SGS_TIMEP_SET)) {
		od->time.flags |= SGS_TIMEP_DEFAULT;
		if (op->op_flags & SGS_SDOP_NESTED) {
			od->time.flags |= SGS_TIMEP_IMPLICIT;
			od->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
		}
	}
	if (!(od->time.flags & SGS_TIMEP_IMPLICIT)) {
		time_ramp(od->freq, dur_ms);
		time_ramp(od->freq2, dur_ms);
		time_ramp(od->amp, dur_ms);
		time_ramp(od->amp2, dur_ms);
		time_ramp(od->pan, dur_ms);
	}
	for (SGS_ScriptListData *list = op->mods;
			list != NULL; list = list->next_list) {
		for (SGS_ScriptOpRef *sub_op = list->first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			time_operator(sub_op);
		}
	}
	return dur_ms;
}

static uint32_t time_event(SGS_ScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	SGS_ScriptOpRef *sub_op;
	for (sub_op = e->main_refs.first_item;
			sub_op != NULL; sub_op = sub_op->next_item) {
		uint32_t sub_dur_ms = time_operator(sub_op);
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
		SGS_ScriptOpRef *ne_op = ne->main_refs.first_item,
			      *ne_op_prev = ne_op->on_prev, *e_op = ne_op_prev;
		SGS_ProgramOpData *e_od = e_op->data;
		uint32_t first_time_ms = e_od->time.v_ms;
		SGS_Time def_time = {
			e_od->time.v_ms,
			(e_od->time.flags & SGS_TIMEP_IMPLICIT)
		};
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
		for (;;) {
			SGS_ProgramOpData *ne_od = ne_op->data;
			SGS_ProgramOpData *ne_od_prev = ne_op_prev->data;
			wait_sum_ms += ne->wait_ms;
			if (!(ne_od->time.flags & SGS_TIMEP_SET)) {
				ne_od->time = def_time;
				if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT)
					ne_od->time.flags |= SGS_TIMEP_SET |
						SGS_TIMEP_DEFAULT;
			}
			time_event(ne);
			def_time = (SGS_Time){
				ne_od->time.v_ms,
				(ne_od->time.flags & SGS_TIMEP_IMPLICIT)
			};
			if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT) {
				if (ne_od_prev->time.flags & SGS_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SGS_SDEV_FROM_GAPSHIFT)) {
					ne_od_prev->time = (SGS_Time){ // gap
						0,
						SGS_TIMEP_SET|SGS_TIMEP_DEFAULT
					};
				}
			}
			if (ne->ev_flags & SGS_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_od_prev->time.v_ms;
				ne_od_prev->time.flags &= ~SGS_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_od->time.flags |= SGS_TIMEP_SET;
			ne_od->params |= SGS_POPP_TIME;
			ne_op_prev = ne_op;
			ne_prev = ne;
			ne = ne->next;
			if (!ne) break;
			ne_op = ne->main_refs.first_item;
		}
		if (dur_ms < first_time_ms)
			dur_ms = first_time_ms;
//		if (dur_ms < nest_dur_ms)
//			dur_ms = nest_dur_ms;
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
	SGS_ScriptEvData *ne = e->forks->events;
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
	e->forks = e->forks->prev;
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
		/*
		 * Track sequence of references and later use here.
		 */
		SGS_ScriptOpRef *sub_op;
		for (sub_op = e->main_refs.first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			SGS_ScriptOpRef *prev_ref = sub_op->obj->last_ref;
			if (prev_ref != NULL) {
				sub_op->on_prev = prev_ref;
				prev_ref->op_flags |= SGS_SDOP_LATER_USED;
				prev_ref->event->ev_flags |=
					SGS_SDEV_VOICE_LATER_USED;
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
SGS_Script* SGS_load_Script(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SGS_Parser pr;
	SGS_Script *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	postparse_passes(&pr);
	o = SGS_MemPool_alloc(pr.mem, sizeof(SGS_Script));
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
void SGS_discard_Script(SGS_Script *restrict o) {
	if (!o)
		return;
	SGS_destroy_MemPool(o->mem);
}
