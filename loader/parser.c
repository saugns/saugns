/* saugns: Script parser module.
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
	SAU_ScriptOptions sopt;
	const char *const*math_names;
	const char *const*ramp_names;
	const char *const*wave_names;
	struct SAU_Math_state math_state;
};

/*
 * Default script options, used until changed in a script.
 */
static const SAU_ScriptOptions def_sopt = {
	.set = 0,
	.ampmult = 1.f,
	.A4_freq = 440.f,
	.def_time_ms = 1000,
	.def_freq = 440.f,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		SAU_SymTab *restrict st) {
	o->sopt = def_sopt;
	if (!(o->math_names = SAU_SymTab_pool_stra(st,
			SAU_Math_names, SAU_MATH_SYMBOLS)))
		return false;
	if (!(o->ramp_names = SAU_SymTab_pool_stra(st,
			SAU_Ramp_names, SAU_RAMP_FILLS)))
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
		SAU_Scanner_warning(o, NULL, "%s name missing", print_type);
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
	if (!scan_symafind(o, sl->math_names, found_id, "math symbol"))
		return false;
	if (SAU_Math_params[*found_id] == SAU_MATH_NOARG_F)
		return true; // no param list, no parentheses
	if (SAU_Scanner_tryc(o, '('))
		return true;
	SAU_Scanner_warning(o, NULL,
"expected '(' following math function name '%s'", SAU_Math_names[*found_id]);
	return false;
}

struct NumParser {
	SAU_Scanner *sc;
	SAU_ScanNumConst_f numconst_f;
	SAU_ScanFrame sf_start;
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
	SAU_Scanner *sc = o->sc;
	struct ScanLookup *sl = sc->data;
	uint8_t ws_level = sc->ws_level;
	double num;
	uint8_t c;
	if (level == 1 && ws_level != SAU_SCAN_WS_NONE)
		SAU_Scanner_setws_level(sc, SAU_SCAN_WS_NONE);
	c = SAU_Scanner_getc(sc);
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else if (c == '+' || c == '-') {
		num = scan_num_r(o, NUMEXP_ADT, level+1);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else {
		size_t func_id = 0, read_len = 0;
		SAU_Scanner_ungetc(sc);
		SAU_Scanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0) {
			if (!IS_ALPHA(c) || !scan_mathfunc(sc, &func_id))
				goto REJECT; /* silent NaN (nothing was read) */
			switch (SAU_Math_params[func_id]) {
			case SAU_MATH_VAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = SAU_Math_symbols[func_id].val(num);
				break;
			case SAU_MATH_STATE_F:
				SAU_Scanner_skipws(sc);
				if (!SAU_Scanner_tryc(sc, ')')) {
					SAU_Scanner_warning(sc, NULL,
"math function '%s()' takes no arguments", sl->math_names[func_id]);
					goto REJECT;
				}
				num = SAU_Math_symbols[func_id]
					.state(&sl->math_state);
				break;
			case SAU_MATH_STATEVAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = SAU_Math_symbols[func_id]
					.stateval(&sl->math_state, num);
				break;
			case SAU_MATH_NOARG_F:
				num = SAU_Math_symbols[func_id].noarg();
				break;
			default:
				SAU_error("scan_num_r",
"math function '%s' has unimplemented parameter type",
						sl->math_names[func_id]);
				goto REJECT;
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
		c = SAU_Scanner_getc(sc);
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
			    (c != SAU_SCAN_SPACE && c != SAU_SCAN_LNBRK)) {
				SAU_Scanner_ungetc(sc);
				double rval = scan_num_r(o, NUMEXP_MLT, level);
				if (isnan(rval)) goto ACCEPT;
				num *= rval;
				break;
			}
			if (pri == NUMEXP_SUB && level > 0) {
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
ACCEPT:
	if (0)
REJECT: {
		num = NAN;
	}
	if (ws_level != sc->ws_level)
		SAU_Scanner_setws_level(sc, ws_level);
	return num;
}
static sauNoinline bool scan_num(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst, float *restrict var) {
	struct NumParser np = {o, scan_numconst, o->sf, false, false, false};
	float num = scan_num_r(&np, NUMEXP_SUB, 0);
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
	return scan_symafind(o, sl->wave_names, found_id, "wave type");
}

static bool scan_ramp_state(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool ratio) {
	if (!scan_num(o, scan_numconst, &ramp->v0))
		return false;
	ramp->flags |= SAU_RAMPP_STATE;
	if (ratio)
		ramp->flags |= SAU_RAMPP_STATE_RATIO;
	else
		ramp->flags &= ~SAU_RAMPP_STATE_RATIO;
	return true;
}

static bool scan_ramp_param(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool ratio) {
	bool state = scan_ramp_state(o, scan_numconst, ramp, ratio);
	if (!SAU_Scanner_tryc(o, '{'))
		return state;
	struct ScanLookup *sl = o->data;
	uint32_t time_ms = (ramp->flags & SAU_RAMPP_TIME) != 0 ?
		ramp->time_ms :
		sl->sopt.def_time_ms;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(o);
		switch (c) {
		case SAU_SCAN_SPACE:
		case SAU_SCAN_LNBRK:
			break;
		case 'g':
			if (scan_num(o, scan_numconst, &ramp->vt)) {
				ramp->flags |= SAU_RAMPP_GOAL;
				if (ratio)
					ramp->flags |= SAU_RAMPP_GOAL_RATIO;
				else
					ramp->flags &= ~SAU_RAMPP_GOAL_RATIO;
			}
			break;
		case 'r': {
			size_t id;
			if (scan_symafind(o, sl->ramp_names,
					&id, "ramp fill shape")) {
				ramp->fill_type = id;
				ramp->flags |= SAU_RAMPP_FILL_TYPE;
			}
			break; }
		case 't':
			if (scan_time_val(o, &time_ms))
				ramp->flags &= ~SAU_RAMPP_TIME_IF_NEW;
			break;
		case 'v':
			if (state) goto REJECT;
			scan_ramp_state(o, scan_numconst, ramp, ratio);
			break;
		case '}':
			goto RETURN;
		default:
		REJECT:
			if (!handle_unknown_or_eof(o, c)) {
				warn_eof_without_closing(o, '}');
				goto RETURN;
			}
			break;
		}
	}
RETURN:
	ramp->time_ms = time_ms;
	ramp->flags |= SAU_RAMPP_TIME;
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
	SAU_ScriptDurGroup *groups, *cur_group;
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
	SCOPE_SAME = 0, // specially handled inner copy of parent scope
	SCOPE_GROUP,    // '<...>' or top scope
	SCOPE_BIND,     // '@{...}'
	SCOPE_NEST,     // '[...]'
};

typedef void (*ParseLevel_sub_f)(SAU_Parser *restrict o);

static void parse_in_event(SAU_Parser *restrict o);
static void parse_in_settings(SAU_Parser *restrict o);

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
	SAU_ScriptEvData *event;
	SAU_ScriptListData *nest_list;
	SAU_ScriptOpRef *nest_last_data;
	SAU_ScriptOpRef *ev_first_data, *ev_last_data;
	SAU_ScriptOpRef *operator;
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

static SAU_Ramp *create_ramp(SAU_Parser *restrict o,
		bool mult, uint32_t par_flag) {
	struct ScanLookup *sl = &o->sl;
	SAU_Ramp *ramp = SAU_MemPool_alloc(o->mem, sizeof(SAU_Ramp));
	float v0 = 0.f;
	if (!ramp)
		return NULL;
	ramp->fill_type = SAU_RAMP_LIN; // default if goal enabled
	switch (par_flag) {
	case SAU_PRAMP_PAN:
		v0 = sl->sopt.def_chanmix;
		break;
	case SAU_PRAMP_AMP:
		v0 = 1.0f; /* multiplied with sl->sopt.ampmult separately */
		break;
	case SAU_PRAMP_AMP2:
		v0 = 0.f;
		break;
	case SAU_PRAMP_FREQ:
		v0 = mult ?
			sl->sopt.def_relfreq :
			sl->sopt.def_freq;
		break;
	case SAU_PRAMP_FREQ2:
		v0 = 0.f;
		break;
	default:
		return NULL;
	}
	ramp->v0 = v0;
	ramp->flags |= SAU_RAMPP_STATE |
		SAU_RAMPP_FILL_TYPE |
		SAU_RAMPP_TIME_IF_NEW; /* don't set main SAU_RAMPP_TIME here */
	if (mult) {
		ramp->flags |= SAU_RAMPP_STATE_RATIO;
	}
	return ramp;
}

static bool parse_ramp(SAU_Parser *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp **restrict rampp, bool mult,
		uint32_t ramp_id) {
	if (!*rampp) { /* create for updating, unparsed values kept unset */
		*rampp = create_ramp(o, mult, ramp_id);
		(*rampp)->flags &= ~(SAU_RAMPP_STATE | SAU_RAMPP_FILL_TYPE);
	}
	return scan_ramp_param(o->sc, scan_numconst, *rampp, mult);
}

static bool parse_waittime(SAU_Parser *restrict o) {
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

static void end_operator(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~PL_ACTIVE_OP;
	SAU_ScriptOpRef *op = pl->operator;
	SAU_ProgramOpData *od = op->data;
	if (od->amp) {
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			od->amp->v0 *= o->sl.sopt.ampmult;
			od->amp->vt *= o->sl.sopt.ampmult;
		}
	}
	if (od->amp2) {
		if (!(op->op_flags & SAU_SDOP_NESTED)) {
			od->amp2->v0 *= o->sl.sopt.ampmult;
			od->amp2->vt *= o->sl.sopt.ampmult;
		}
	}
	SAU_ScriptOpRef *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		od->params = SAU_POP_PARAMS;
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
	pl->event = NULL;
	pl->ev_first_data = NULL;
	pl->ev_last_data = NULL;
	SAU_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
	if (!o->cur_group->first)
		o->cur_group->first = group_e;
	o->cur_group->last = group_e;
}

static void begin_event(SAU_Parser *restrict o,
		SAU_ScriptOpRef *restrict prev_data,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptEvData *e, *pve;
	end_event(o);
	e = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptEvData));
	pl->event = e;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	if (prev_data != NULL) {
		if (prev_data->op_flags & SAU_SDOP_NESTED)
			e->ev_flags |= SAU_SDEV_IMPLICIT_TIME;
		pve = prev_data->event;
		e->root_ev = prev_data->obj->root_event;
		if (is_compstep) {
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

static void begin_operator(SAU_Parser *restrict o,
		SAU_ScriptOpRef *restrict pop,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptEvData *e = pl->event;
	SAU_ScriptOpRef *op;
	SAU_ProgramOpData *od, *pod = (pop != NULL) ? pop->data : NULL;
	/*
	 * It is assumed that a valid event exists.
	 */
	end_operator(o);
	op = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptOpRef));
	od = SAU_MemPool_alloc(o->mem, sizeof(SAU_ProgramOpData));
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
			(SAU_SDOP_NESTED | SAU_SDOP_MULTIPLE);
		od->time = (SAU_Time){pod->time.v_ms,
			(pod->time.flags & SAU_TIMEP_IMPLICIT)};
		od->wave = pod->wave;
		od->phase = pod->phase;
		op->obj = pop->obj;
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		od->use_type = pl->use_type;
		od->time = (SAU_Time){o->sl.sopt.def_time_ms, 0};
		if (od->use_type == SAU_POP_CARR) {
			od->pan = create_ramp(o, false, SAU_PRAMP_PAN);
			od->freq = create_ramp(o, false, SAU_PRAMP_FREQ);
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			od->freq = create_ramp(o, true, SAU_PRAMP_FREQ);
		}
		od->amp = create_ramp(o, false, SAU_PRAMP_AMP);
		op->obj = SAU_MemPool_alloc(o->mem, sizeof(SAU_ScriptOpObj));
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
static void begin_node(SAU_Parser *restrict o,
		SAU_ScriptOpRef *restrict previous,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	uint8_t use_type = (previous != NULL) ?
		previous->data->use_type :
		pl->use_type;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->sub_f != parse_in_event ||
			pl->next_wait_ms ||
			((previous != NULL || use_type == SAU_POP_CARR)
			 && pl->event->main_refs.first_item != NULL) ||
			is_compstep)
		begin_event(o, previous, is_compstep);
	begin_operator(o, previous, is_compstep);
}

static bool new_durgroup(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	pl->next_wait_ms = 0; /* does not cross boundaries */
	if (o->cur_group != NULL && !o->cur_group->first)
		return true; /* nothing to do */
	SAU_ScriptDurGroup *group = SAU_MemPool_alloc(o->tmp, sizeof(*group));
	if (!group)
		return false;
	if (!o->groups)
		o->groups = group;
	else
		o->cur_group->next_group = group;
	o->cur_group = group;
	return true;
}

static void time_durgroup(SAU_ScriptDurGroup *restrict g);

static void enter_level(SAU_Parser *restrict o,
		struct ParseLevel *restrict pl,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel *restrict parent_pl = o->cur_pl;
	++o->call_level;
	o->cur_pl = pl;
	*pl = (struct ParseLevel){0};
	pl->scope = newscope;
	if (!parent_pl) {
		new_durgroup(o);
	} else {
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
			SAU_ScriptOpRef *parent_on = parent_pl->operator;
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
		for (SAU_ScriptDurGroup *g = o->groups;
				g != NULL; g = g->next_group)
			time_durgroup(g);
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
	SAU_ScriptOpRef *op = pl->operator;
	SAU_ProgramOpData *od = op->data;
	parse_ramp(o, NULL, &od->amp, false, SAU_PRAMP_AMP);
	if (SAU_Scanner_tryc(sc, ',') && SAU_Scanner_tryc(sc, 'w')) {
		parse_ramp(o, NULL, &od->amp2, false, SAU_PRAMP_AMP2);
		if (SAU_Scanner_tryc(sc, '[')) {
			parse_level(o, SAU_POP_AMOD, SCOPE_NEST);
		}
	}
	return false;
}

static bool parse_ev_chanmix(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptOpRef *op = pl->operator;
	SAU_ProgramOpData *od = op->data;
	if (op->op_flags & SAU_SDOP_NESTED)
		return true; // reject
	parse_ramp(o, scan_chanmix_const, &od->pan, false, SAU_PRAMP_PAN);
	return false;
}

static bool parse_ev_freq(SAU_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpRef *op = pl->operator;
	SAU_ProgramOpData *od = op->data;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	SAU_ScanNumConst_f numconst_f = rel_freq ? NULL : scan_note_const;
	parse_ramp(o, numconst_f, &od->freq, rel_freq, SAU_PRAMP_FREQ);
	if (SAU_Scanner_tryc(sc, ',') && SAU_Scanner_tryc(sc, 'w')) {
		parse_ramp(o, numconst_f, &od->freq2,
				rel_freq, SAU_PRAMP_FREQ2);
		if (SAU_Scanner_tryc(sc, '[')) {
			parse_level(o, SAU_POP_FMOD, SCOPE_NEST);
		}
	}
	return false;
}

static bool parse_ev_phase(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ScriptOpRef *op = pl->operator;
	SAU_ProgramOpData *od = op->data;
	if (scan_num(sc, scan_phase_const, &od->phase)) {
		od->phase = fmod(od->phase, 1.f);
		if (od->phase < 0.f)
			od->phase += 1.f;
		od->params |= SAU_POPP_PHASE;
	}
	if (SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, SAU_POP_PMOD, SCOPE_NEST);
	}
	if (SAU_Scanner_tryc(sc, ',')) {
		if (SAU_Scanner_tryc(sc, 'f') && SAU_Scanner_tryc(sc, '[')) {
			parse_level(o, SAU_POP_FPMOD, SCOPE_NEST);
		}
	}
	return false;
}

static void parse_in_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	pl->sub_f = parse_in_event;
	for (;;) {
		SAU_ScriptOpRef *op = pl->operator;
		SAU_ProgramOpData *od = op->data;
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case '/':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, false);
			}
			break;
		case '\\':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, true);
				pl->event->ev_flags |= SAU_SDEV_FROM_GAPSHIFT;
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
			if (SAU_Scanner_tryc(sc, 'd')) {
				od->time = (SAU_Time){o->sl.sopt.def_time_ms,
					0};
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_SDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				od->time = (SAU_Time){o->sl.sopt.def_time_ms,
					SAU_TIMEP_SET | SAU_TIMEP_IMPLICIT};
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				od->time = (SAU_Time){time_ms, SAU_TIMEP_SET};
			}
			od->params |= SAU_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			od->wave = wave;
			od->params |= SAU_POPP_WAVE;
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
		 * May (also) happen in a new nested parse_level() call.
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
				SAU_Scanner_warning(sc, NULL,
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
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			if ((pl.operator->data->time.flags & // TODO: tidy...
			     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
			    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
				SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' separator");
			begin_node(o, pl.operator, true);
			pl.event->ev_flags |= SAU_SDEV_WAIT_PREV_DUR;
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
				SAU_ScriptOpRef *ref = label->data;
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
			SAU_ProgramOpData *od;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(o, NULL, false);
			od = pl.operator->data;
			od->wave = wave;
			parse_in_event(o);
			break; }
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
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			end_event(o);
			new_durgroup(o);
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
static const char *parse_file(SAU_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SAU_Scanner *sc = o->sc;
	const char *name;
	if (!SAU_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, SAU_POP_CARR, SCOPE_GROUP);
	name = sc->f->path;
	SAU_Scanner_close(sc);
	return name;
}

static uint32_t time_event(SAU_ScriptEvData *restrict e);

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SAU_ScriptDurGroup *restrict g) {
	SAU_ScriptEvData *e, *e_after = (g->last ? g->last->next : NULL);
	uint32_t cur_longest = 0, wait_sum = 0, wait_after = 0;
	for (e = g->first; e != e_after; ) {
		if (!(e->ev_flags & SAU_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_SDEV_VOICE_SET_DUR;
		time_event(e);
		if ((e->ev_flags & SAU_SDEV_VOICE_SET_DUR) != 0 &&
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
	for (e = g->first; e != e_after; ) {
		for (SAU_ScriptOpRef *op = e->main_refs.first_item;
				op != NULL; op = op->next_item) {
			SAU_ProgramOpData *od = op->data;
			if (!(od->time.flags & SAU_TIMEP_SET)) {
				/* fill in sensible default time */
				od->time.v_ms = cur_longest + wait_sum;
				od->time.flags |= SAU_TIMEP_SET;
				if (e->dur_ms < od->time.v_ms)
					e->dur_ms = od->time.v_ms;
			}
		}
		e = e->next;
		if (e != NULL) {
			wait_sum -= e->wait_ms;
		}
	}
	if (e_after != NULL)
		e_after->wait_ms += wait_after;
}

static inline void time_ramp(SAU_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!ramp)
		return;
	if (ramp->flags & SAU_RAMPP_TIME_IF_NEW) { /* update fallback value */
		ramp->time_ms = default_time_ms;
		ramp->flags |= SAU_RAMPP_TIME;
	}
}

static uint32_t time_operator(SAU_ScriptOpRef *restrict op) {
	SAU_ProgramOpData *od = op->data;
	uint32_t dur_ms = od->time.v_ms;
	if (!(od->params & SAU_POPP_TIME))
		op->event->ev_flags &= ~SAU_SDEV_VOICE_SET_DUR;
	if (!(od->time.flags & SAU_TIMEP_SET)) {
		od->time.flags |= SAU_TIMEP_DEFAULT;
		if (op->op_flags & SAU_SDOP_NESTED) {
			od->time.flags |= SAU_TIMEP_IMPLICIT;
			od->time.flags |= SAU_TIMEP_SET; /* no durgroup yet */
		}
	}
	if (!(od->time.flags & SAU_TIMEP_IMPLICIT)) {
		time_ramp(od->freq, dur_ms);
		time_ramp(od->freq2, dur_ms);
		time_ramp(od->amp, dur_ms);
		time_ramp(od->amp2, dur_ms);
		time_ramp(od->pan, dur_ms);
	}
	for (SAU_ScriptListData *list = op->mods;
			list != NULL; list = list->next_list) {
		for (SAU_ScriptOpRef *sub_op = list->first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			time_operator(sub_op);
		}
	}
	return dur_ms;
}

static uint32_t time_event(SAU_ScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	SAU_ScriptOpRef *sub_op;
	for (sub_op = e->main_refs.first_item;
			sub_op != NULL; sub_op = sub_op->next_item) {
		uint32_t sub_dur_ms = time_operator(sub_op);
		if (dur_ms < sub_dur_ms)
			dur_ms = sub_dur_ms;
	}
	/*
	 * Timing for sub-events - done before event list flattened.
	 */
	SAU_ScriptEvBranch *fork = e->forks;
	while (fork != NULL) {
		uint32_t nest_dur_ms = 0, wait_sum_ms = 0;
		SAU_ScriptEvData *ne = fork->events, *ne_prev = e;
		SAU_ScriptOpRef *ne_op = ne->main_refs.first_item,
			      *ne_op_prev = ne_op->on_prev, *e_op = ne_op_prev;
		SAU_ProgramOpData *e_od = e_op->data;
		uint32_t first_time_ms = e_od->time.v_ms;
		SAU_Time def_time = {
			e_od->time.v_ms,
			(e_od->time.flags & SAU_TIMEP_IMPLICIT)
		};
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SAU_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_SDEV_VOICE_SET_DUR;
		for (;;) {
			SAU_ProgramOpData *ne_od = ne_op->data;
			SAU_ProgramOpData *ne_od_prev = ne_op_prev->data;
			wait_sum_ms += ne->wait_ms;
			if (!(ne_od->time.flags & SAU_TIMEP_SET)) {
				ne_od->time = def_time;
				if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT)
					ne_od->time.flags |= SAU_TIMEP_SET |
						SAU_TIMEP_DEFAULT;
			}
			time_event(ne);
			def_time = (SAU_Time){
				ne_od->time.v_ms,
				(ne_od->time.flags & SAU_TIMEP_IMPLICIT)
			};
			if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT) {
				if (ne_od_prev->time.flags & SAU_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SAU_SDEV_FROM_GAPSHIFT)) {
					ne_od_prev->time = (SAU_Time){ // gap
						0,
						SAU_TIMEP_SET|SAU_TIMEP_DEFAULT
					};
				}
			}
			if (ne->ev_flags & SAU_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_od_prev->time.v_ms;
				ne_od_prev->time.flags &= ~SAU_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_od->time.flags |= SAU_TIMEP_SET;
			ne_od->params |= SAU_POPP_TIME;
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
static void flatten_events(SAU_ScriptEvData *restrict e) {
	SAU_ScriptEvData *ne = e->forks->events;
	SAU_ScriptEvData *fe = e->next, *fe_prev = e;
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
		SAU_ScriptEvData *ne_next = ne->next;
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
			SAU_ScriptEvData *fe_next = fe->next;
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
static void postparse_passes(SAU_Parser *restrict o) {
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (SAU_ScriptEvData *e = o->events; e != NULL; e = e->next) {
		while (e->forks != NULL) flatten_events(e);
		/*
		 * Track sequence of references and later use here.
		 */
		SAU_ScriptOpRef *sub_op;
		for (sub_op = e->main_refs.first_item;
				sub_op != NULL; sub_op = sub_op->next_item) {
			SAU_ScriptOpRef *prev_ref = sub_op->obj->last_ref;
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
