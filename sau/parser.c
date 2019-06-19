/* SAU library: Script parser module.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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

#include <sau/scanner.h>
#include <sau/script.h>
#include <sau/help.h>
#include <sau/math.h>
#include <sau/arrtype.h>
#include <string.h>
#include <stdio.h>
#include "parser/parseconv.h"

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))

enum {
	SAU_SYM_VAR = 0,
	SAU_SYM_MATH_ID,
	SAU_SYM_RAMP_ID,
	SAU_SYM_WAVE_ID,
	SAU_SYM_TYPES
};

static const char *const scan_sym_labels[SAU_SYM_TYPES] = {
	"variable",
	"math symbol",
	"ramp fill shape",
	"wave type",
};

struct ScanLookup {
	SAU_ScriptOptions sopt;
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
		const SAU_ScriptArg *restrict arg,
		SAU_Symtab *restrict st) {
	o->sopt = def_sopt;
	if (!SAU_Symtab_add_stra(st, SAU_Math_names, SAU_MATH_NAMED,
			SAU_SYM_MATH_ID) ||
	    !SAU_Symtab_add_stra(st, SAU_Ramp_names, SAU_RAMP_NAMED,
			SAU_SYM_RAMP_ID) ||
	    !SAU_Symtab_add_stra(st, SAU_Wave_names, SAU_WAVE_NAMED,
			SAU_SYM_WAVE_ID))
		return false;
	o->math_state.no_time = arg->no_time;
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
	const char *warn_str = SAU_IS_ASCIIVISIBLE(c) ?
		(IS_UPPER(c) ?
		"invalid or misplaced typename '%c'" :
		(IS_LOWER(c) ?
		"invalid or misplaced subname '%c'" :
		"misplaced or unrecognized '%c'")) :
		"invalid character (value 0x%02hhX)";
	SAU_Scanner_warning(o, NULL, warn_str, c);
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
 * Print warning for missing whitespace before character.
 */
static void warn_missing_whitespace(SAU_Scanner *restrict o,
		SAU_ScanFrame *sf, uint8_t next_c) {
	SAU_Scanner_warning(o, sf, "missing whitespace before '%c'", next_c);
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

static SAU_Symitem *scan_sym(SAU_Scanner *restrict o, uint32_t type_id,
		const char *const*restrict help_stra) {
	const char *type_label = scan_sym_labels[type_id];
	SAU_ScanFrame sf_begin = o->sf;
	SAU_Symstr *s = NULL;
	SAU_Scanner_get_symstr(o, &s);
	if (!s) {
		SAU_Scanner_warning(o, NULL, "%s name missing", type_label);
		return NULL;
	}
	SAU_Symitem *item = SAU_Symtab_find_item(o->symtab, s, type_id);
	if (!item && type_id == SAU_SYM_VAR)
		item = SAU_Symtab_add_item(o->symtab, s, SAU_SYM_VAR);
	if (!item && help_stra != NULL) {
		SAU_Scanner_warning(o, &sf_begin,
				"invalid %s name '%s'; available are:",
				type_label, s->key);
		SAU_print_names(help_stra, "\t", stderr);
		return NULL;
	}
	return item;
}

static bool scan_mathfunc(SAU_Scanner *restrict o, size_t *restrict found_id) {
	SAU_Symitem *sym = scan_sym(o, SAU_SYM_MATH_ID, SAU_Math_names);
	if (!sym)
		return false;
	if (SAU_Math_params[sym->data.id] == SAU_MATH_NOARG_F // no parentheses
	    || SAU_Scanner_tryc(o, '(')) {
		*found_id = sym->data.id;
		return true;
	}
	SAU_Scanner_warning(o, NULL,
"expected '(' following math function name '%s'", SAU_Math_names[sym->data.id]);
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
		num = scan_num_r(o, NUMEXP_ADT, level);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else if (c == '$') {
		SAU_Symitem *var = scan_sym(sc, SAU_SYM_VAR, NULL);
		if (!var) goto REJECT;
		if (var->data_use != SAU_SYM_DATA_NUM) {
			SAU_Scanner_warning(sc, NULL,
"variable '$%s' in numerical expression doesn't hold a number", var->sstr->key);
			goto REJECT;
		}
		num = var->data.num;
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
"math function '%s()' takes no arguments", SAU_Math_names[func_id]);
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
						SAU_Math_names[func_id]);
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
			if (pri != NUMEXP_SUB || level == 0) goto DEFER;
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
		SAU_ScanNumConst_f scan_numconst, double *restrict var) {
	struct NumParser np = {o, scan_numconst, o->sf, false, false, false};
	double num = scan_num_r(&np, NUMEXP_SUB, 0);
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
	double val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		SAU_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = SAU_ui32rint(val_s * 1000.f);
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
	SAU_File_geti(f, &octave, false, &num_len);
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

static bool scan_wavetype(SAU_Scanner *restrict o, size_t *restrict found_id) {
	SAU_Symitem *sym = scan_sym(o, SAU_SYM_WAVE_ID, SAU_Wave_names);
	if (!sym)
		return false;
	*found_id = sym->data.id;
	return true;
}

static bool scan_ramp_state(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool ratio) {
	double v0;
	if (!scan_num(o, scan_numconst, &v0))
		return false;
	ramp->v0 = v0;
	ramp->flags |= SAU_RAMPP_STATE;
	if (ratio)
		ramp->flags |= SAU_RAMPP_STATE_RATIO;
	else
		ramp->flags &= ~SAU_RAMPP_STATE_RATIO;
	return true;
}

/*
 * Parser
 */

struct NestScope {
	SAU_ScriptListData *list, *last_mods;
	SAU_ScriptOpData *last_item;
	SAU_ScriptOptions sopt_save; /* save/restore on nesting */
	/* values passed for outer parameter */
	SAU_Ramp *op_sweep;
	SAU_ScanNumConst_f numconst_f;
	bool num_ratio;
};

sauArrType(NestArr, struct NestScope, )

typedef struct SAU_Parser {
	struct ScanLookup sl;
	SAU_Scanner *sc;
	SAU_Symtab *st;
	SAU_Mempool *mp, *tmp_mp, *prg_mp;
	NestArr nest;
	/* node state */
	struct ParseLevel *cur_pl;
	SAU_ScriptEvData *events, *last_event, *group_event;
} SAU_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SAU_Parser *restrict o) {
	SAU_destroy_Scanner(o->sc);
	SAU_destroy_Mempool(o->tmp_mp);
	SAU_destroy_Mempool(o->prg_mp);
	SAU_destroy_Mempool(o->mp);
	NestArr_clear(&o->nest);
}

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 *
 * \return true, or false on allocation failure
 */
static bool init_Parser(SAU_Parser *restrict o,
		const SAU_ScriptArg *restrict script_arg) {
	SAU_Mempool *mp = SAU_create_Mempool(0),
		    *tmp_mp = SAU_create_Mempool(0),
		    *prg_mp = SAU_create_Mempool(0);
	SAU_Symtab *st = SAU_create_Symtab(mp);
	SAU_Scanner *sc = SAU_create_Scanner(st);
	*o = (SAU_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mp = mp;
	o->tmp_mp = tmp_mp;
	o->prg_mp = prg_mp;
	if (!sc || !tmp_mp || !prg_mp) goto ERROR;
	if (!init_ScanLookup(&o->sl, script_arg, st)) goto ERROR;
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
	SCOPE_SAME = 0, // specially handled inner copy of parent scope (unused)
	SCOPE_GROUP,    // '{...}' or top scope
	SCOPE_BIND,     // '@[...]'
	SCOPE_NEST,     // '[...]'
};

typedef void (*ParseLevel_sub_f)(SAU_Parser *restrict o);
static void parse_in_settings(SAU_Parser *restrict o);
static void parse_in_op_step(SAU_Parser *restrict o);
static void parse_in_par_sweep(SAU_Parser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_BIND_MULTIPLE  = 1<<0, // previous node interpreted as set of nodes
	PL_NEW_EVENT_FORK = 1<<1,
	PL_OWN_EV         = 1<<2,
	PL_OWN_OP         = 1<<3,
	PL_WARN_NOSPACE   = 1<<4,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
struct ParseLevel {
	struct ParseLevel *parent;
	ParseLevel_sub_f sub_f;
	uint8_t pl_flags;
	uint8_t scope, close_c;
	uint8_t use_type;
	SAU_ScriptEvData *event;
	SAU_ScriptOpData *operator, *ev_last;
	SAU_Symitem *set_var;
	/* timing/delay */
	SAU_ScriptEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t add_wait_ms, carry_wait_ms; /* added for next event */
	float used_ampmult; /* update on node creation */
};

typedef struct SAU_ScriptEvBranch {
	SAU_ScriptEvData *events;
	struct SAU_ScriptEvBranch *prev;
} SAU_ScriptEvBranch;

static SAU_Ramp *create_ramp(SAU_Parser *restrict o,
		bool mult, uint32_t par_flag) {
	struct ScanLookup *sl = &o->sl;
	SAU_Ramp *ramp = SAU_mpalloc(o->prg_mp, sizeof(SAU_Ramp));
	float v0 = 0.f;
	if (!ramp)
		return NULL;
	ramp->fill_type = SAU_RAMP_N_lin; // default if goal enabled
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

static bool parse_waittime(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	uint32_t wait_ms;
	if (!scan_time_val(o->sc, &wait_ms))
		return false;
	pl->add_wait_ms += wait_ms;
	return true;
}

/*
 * Node- and scope-handling functions
 */

static void end_operator(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_OP))
		return;
	pl->pl_flags &= ~PL_OWN_OP;
	SAU_ScriptOpData *op = pl->operator;
	if (op->amp) {
		op->amp->v0 *= pl->used_ampmult;
		op->amp->vt *= pl->used_ampmult;
	}
	if (op->amp2) {
		op->amp2->v0 *= pl->used_ampmult;
		op->amp2->vt *= pl->used_ampmult;
	}
	SAU_ScriptOpData *pop = op->prev_ref;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->params = SAU_POP_PARAMS;
	}
	pl->operator = NULL;
}

static void end_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_EV))
		return;
	pl->pl_flags &= ~PL_OWN_EV;
	end_operator(o);
	pl->ev_last = NULL;
	pl->event = NULL;
}

static void begin_event(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict prev_data,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptEvData *e;
	end_event(o);
	pl->event = SAU_mpalloc(o->mp, sizeof(SAU_ScriptEvData));
	e = pl->event;
	e->wait_ms = pl->add_wait_ms + pl->carry_wait_ms;
	pl->add_wait_ms = pl->carry_wait_ms = 0;
	if (prev_data != NULL) {
		SAU_ScriptEvData *pve = prev_data->event;
		if (prev_data->op_flags & SAU_SDOP_NESTED)
			e->ev_flags |= SAU_SDEV_IMPLICIT_TIME;
		e->root_ev = prev_data->info->root_event;
		if (is_compstep) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				SAU_ScriptEvBranch *fork =
					SAU_mpalloc(o->tmp_mp, sizeof(*fork));
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
	if (!o->group_event)
		o->group_event = (pl->main_ev != NULL) ? pl->main_ev : e;
	pl->pl_flags |= PL_OWN_EV;
}

static void begin_operator(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict pop, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptEvData *e = pl->event;
	SAU_ScriptOpData *op;
	/*
	 * It is assumed that a valid event exists.
	 */
	end_operator(o);
	pl->operator = op = SAU_mpalloc(o->mp, sizeof(SAU_ScriptOpData));
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	pl->used_ampmult = o->sl.sopt.ampmult;
	/*
	 * Initialize node.
	 */
	if (pop != NULL) {
		op->prev_ref = pop;
		op->op_flags = pop->op_flags &
			(SAU_SDOP_NESTED | SAU_SDOP_MULTIPLE);
		op->time = (SAU_Time){pop->time.v_ms,
			(pop->time.flags & SAU_TIMEP_IMPLICIT)};
		op->wave = pop->wave;
		op->phase = pop->phase;
		op->info = pop->info;
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			SAU_ScriptOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
			} while ((mpop = mpop->next) != NULL);
			op->op_flags |= SAU_SDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~PL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->time = (SAU_Time){o->sl.sopt.def_time_ms, 0};
		if (pl->use_type == SAU_POP_CARR) {
			op->pan = create_ramp(o, false, SAU_PRAMP_PAN);
			op->freq = create_ramp(o, false, SAU_PRAMP_FREQ);
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			op->freq = create_ramp(o, true, SAU_PRAMP_FREQ);
		}
		op->amp = create_ramp(o, false, SAU_PRAMP_AMP);
		op->info = SAU_mpalloc(o->mp, sizeof(SAU_ScriptObjInfo));
		op->info->root_event = e;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	struct NestScope *nest = NestArr_tip(&o->nest);
	if (pop || !nest) {
		if (!e->objs.first_item) {
			e->objs.first_item = op;
			++e->objs.count;
		} else
			pl->ev_last->next = op;
		pl->ev_last = op;
	} else {
		if (!nest->list->first_item)
			nest->list->first_item = op;
		else
			nest->last_item->next = op;
		nest->last_item = op;
		++nest->list->count;
	}
	/*
	 * Assign to variable?
	 */
	if (pl->set_var != NULL) {
		pl->set_var->data_use = SAU_SYM_DATA_OBJ;
		pl->set_var->data.obj = op;
		pl->set_var = NULL;
	}
	pl->pl_flags |= PL_OWN_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(SAU_Parser *restrict o,
		SAU_ScriptOpData *restrict previous, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	if (!pl->event || pl->add_wait_ms > 0 ||
			/* previous event implicitly ended */
			((previous || pl->use_type == SAU_POP_CARR)
			 && pl->event->objs.first_item) ||
			is_compstep)
		begin_event(o, previous, is_compstep);
	begin_operator(o, previous, is_compstep);
}

static SAU_ScriptEvData *time_durgroup(SAU_ScriptEvData *restrict e_from,
		uint32_t *restrict wait_after);

static void finish_durgroup(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	pl->add_wait_ms = 0; /* reset by each '|' boundary */
	if (!o->group_event)
		return; /* nothing to do */
	o->last_event = time_durgroup(o->group_event, &pl->carry_wait_ms);
	o->group_event = NULL;
}

static void enter_level(SAU_Parser *restrict o,
		struct ParseLevel *restrict pl,
		uint8_t use_type, uint8_t newscope, uint8_t close_c) {
	struct ParseLevel *restrict parent_pl = o->cur_pl;
	*pl = (struct ParseLevel){
		.scope = newscope,
		.close_c = close_c,
	};
	o->cur_pl = pl;
	if (parent_pl != NULL) {
		pl->parent = parent_pl;
		pl->sub_f = parent_pl->sub_f;
		if (newscope == SCOPE_SAME)
			pl->scope = parent_pl->scope;
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		if (newscope == SCOPE_BIND) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = SAU_mpalloc(o->mp, sizeof(*nest->list));
			pl->sub_f = NULL;
		} else if (newscope == SCOPE_NEST) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = SAU_mpalloc(o->mp, sizeof(*nest->list));
			pl->sub_f = nest->op_sweep ? parse_in_par_sweep : NULL;
			SAU_ScriptOpData *parent_on = parent_pl->operator;
			nest->list->use_type = use_type;
			if (!parent_on->mods)
				parent_on->mods = nest->list;
			else
				nest->last_mods->next = nest->list;
			nest->last_mods = nest->list;
			/*
			 * Push script options, and prepare for a new context.
			 *
			 * The amplitude multiplier is reset each list, unless
			 * an AMOD list (where the value builds on the outer).
			 */
			nest->sopt_save = o->sl.sopt;
			o->sl.sopt.set = 0;
			if (use_type != SAU_POP_AMOD)
				o->sl.sopt.ampmult = def_sopt.ampmult;
		}
	}
	pl->use_type = use_type;
}

static void leave_level(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_operator(o);
	if (pl->set_var != NULL) {
		SAU_Scanner_warning(o->sc, NULL,
				"ignoring variable assignment without object");
	}
	if (!pl->parent) {
		/*
		 * At end of top scope (i.e. at end of script),
		 * end last event and adjust timing.
		 */
		end_event(o);
		finish_durgroup(o);
	}
	if (pl->scope == SCOPE_GROUP) {
		end_event(o);
	} else if (pl->scope == SCOPE_BIND) {
	} else if (pl->scope == SCOPE_NEST) {
		struct NestScope *nest = NestArr_tip(&o->nest);
		/*
		 * Pop script options.
		 */
		o->sl.sopt = nest->sopt_save;
	}
	o->cur_pl = pl->parent;
}

/*
 * Main parser functions
 */

#define PARSE_IN__HEAD(Name, GuardCond) \
	struct ParseLevel *pl = o->cur_pl; \
	SAU_Scanner *sc = o->sc; \
	if (!(GuardCond)) { pl->sub_f = NULL; return; } \
	pl->sub_f = (Name); \
	for (;;) { \
		uint8_t c = SAU_Scanner_getc(sc); \
		SAU_ScanFrame sf_first = sc->sf; \
		/* switch (c) { ... default: ... goto DEFER; } */

#define PARSE_IN__TAIL() \
		/* switch (c) { ... default: ... goto DEFER; } */ \
		if (pl->pl_flags & PL_WARN_NOSPACE) \
			warn_missing_whitespace(sc, &sf_first, c); \
		pl->pl_flags |= PL_WARN_NOSPACE; \
	} \
	return; \
DEFER: \
	SAU_Scanner_ungetc(sc); /* let parse_level() take care of it */

static void parse_in_settings(SAU_Parser *restrict o) {
	PARSE_IN__HEAD(parse_in_settings, true)
		double val;
		switch (c) {
		case 'a':
			if (scan_num(sc, NULL, &val)) {
				struct NestScope *nest = NestArr_tip(&o->nest);
				// AMOD lists inherit outer value
				if (pl->use_type == SAU_POP_AMOD)
					val *= nest->sopt_save.ampmult;
				o->sl.sopt.ampmult = val;
				o->sl.sopt.set |= SAU_SOPT_AMPMULT;
			}
			break;
		case 'c':
			if (scan_num(sc, scan_chanmix_const, &val)) {
				o->sl.sopt.def_chanmix = val;
				o->sl.sopt.set |= SAU_SOPT_DEF_CHANMIX;
			}
			break;
		case 'f':
			if (scan_num(sc, scan_note_const, &val)) {
				o->sl.sopt.def_freq = val;
				o->sl.sopt.set |= SAU_SOPT_DEF_FREQ;
			}
			if (SAU_Scanner_tryc(sc, '.') &&
			    SAU_Scanner_tryc(sc, 'n')) {
				if (scan_num(sc, NULL, &val)) {
					if (val < 1.f) {
						SAU_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
						break;
					}
					o->sl.sopt.A4_freq = val;
					o->sl.sopt.set |= SAU_SOPT_A4_FREQ;
				}
			}
			break;
		case 'r':
			if (scan_num(sc, NULL, &val)) {
				o->sl.sopt.def_relfreq = val;
				o->sl.sopt.set |= SAU_SOPT_DEF_RELFREQ;
			}
			break;
		case 't':
			if (scan_time_val(sc, &o->sl.sopt.def_time_ms))
				o->sl.sopt.set |= SAU_SOPT_DEF_TIME;
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c);

static void parse_in_par_sweep(SAU_Parser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	SAU_Ramp *ramp = nest->op_sweep;
	if (!(ramp->flags & SAU_RAMPP_TIME)) {
		ramp->time_ms = o->sl.sopt.def_time_ms;
		ramp->flags |= SAU_RAMPP_TIME;
	}
	PARSE_IN__HEAD(parse_in_par_sweep, true)
		double val;
		switch (c) {
		case 'g':
			if (scan_num(sc, nest->numconst_f, &val)) {
				ramp->vt = val;
				ramp->flags |= SAU_RAMPP_GOAL;
				if (nest->num_ratio)
					ramp->flags |= SAU_RAMPP_GOAL_RATIO;
				else
					ramp->flags &= ~SAU_RAMPP_GOAL_RATIO;
			}
			break;
		case 'r': {
			SAU_Symitem *sym = scan_sym(sc, SAU_SYM_RAMP_ID,
					SAU_Ramp_names);
			if (sym) {
				ramp->fill_type = sym->data.id;
				ramp->flags |= SAU_RAMPP_FILL_TYPE;
			}
			break; }
		case 't':
			if (scan_time_val(sc, &ramp->time_ms))
				ramp->flags &= ~SAU_RAMPP_TIME_IF_NEW;
			break;
		case 'v':
			scan_ramp_state(sc, nest->numconst_f, ramp,
					nest->num_ratio);
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool prepare_sweep(SAU_Parser *restrict o,
		struct NestScope *restrict nest,
		SAU_ScanNumConst_f numconst_f,
		SAU_Ramp **restrict op_sweep, bool ratio,
		uint8_t sweep_id) {
	if (!op_sweep) { /* clear when not provided */
		nest->op_sweep = NULL;
		return true;
	}
	if (!*op_sweep) { /* create for updating, unparsed values kept unset */
		*op_sweep = create_ramp(o, ratio, sweep_id);
		(*op_sweep)->flags &= ~(SAU_RAMPP_STATE | SAU_RAMPP_FILL_TYPE);
	}
	nest->op_sweep = *op_sweep;
	nest->numconst_f = numconst_f;
	nest->num_ratio = ratio;
	return true;
}

static void parse_par_list(SAU_Parser *restrict o,
		SAU_ScanNumConst_f numconst_f,
		SAU_Ramp **restrict op_sweep, bool ratio,
		uint8_t sweep_id, uint8_t use_type) {
	struct NestScope *nest = NestArr_add(&o->nest);
	prepare_sweep(o, nest, numconst_f, op_sweep, ratio, sweep_id);
	if (op_sweep)
		scan_ramp_state(o->sc, numconst_f, *op_sweep, ratio);
	bool clear = SAU_Scanner_tryc(o->sc, '-');
	while (SAU_Scanner_tryc(o->sc, '[')) {
		parse_level(o, use_type, SCOPE_NEST, ']');
		nest = NestArr_tip(&o->nest); // re-get, array may have changed
		if (clear) clear = false;
		else nest->list->append = true;
	}
	NestArr_pop(&o->nest);
}

static bool parse_op_amp(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptOpData *op = pl->operator;
	parse_par_list(o, NULL, &op->amp, false, SAU_PRAMP_AMP, SAU_POP_AMOD);
	if (SAU_Scanner_tryc(o->sc, '.')) switch (SAU_Scanner_getc(o->sc)) {
	case 'r':
		parse_par_list(o, NULL, &op->amp2, false,
				SAU_PRAMP_AMP2, SAU_POP_RAMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_op_chanmix(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptOpData *op = pl->operator;
	if (op->op_flags & SAU_SDOP_NESTED)
		return true; // reject
	parse_par_list(o, scan_chanmix_const, &op->pan, false, SAU_PRAMP_PAN,0);
	return false;
}

static bool parse_op_freq(SAU_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	SAU_ScanNumConst_f num_f = rel_freq ? NULL : scan_note_const;
	parse_par_list(o, num_f, &op->freq, rel_freq,
			SAU_PRAMP_FREQ, SAU_POP_FMOD);
	if (SAU_Scanner_tryc(o->sc, '.')) switch (SAU_Scanner_getc(o->sc)) {
	case 'r':
		parse_par_list(o, num_f, &op->freq2, rel_freq,
				SAU_PRAMP_FREQ2, SAU_POP_RFMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_op_phase(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_ScriptOpData *op = pl->operator;
	double val;
	if (scan_num(o->sc, scan_phase_const, &val)) {
		op->phase = SAU_cyclepos_dtoui32(val);
		op->params |= SAU_POPP_PHASE;
	}
	parse_par_list(o, NULL, NULL, false, 0, SAU_POP_PMOD);
	if (SAU_Scanner_tryc(o->sc, '.')) switch (SAU_Scanner_getc(o->sc)) {
	case 'f':
		parse_par_list(o, NULL, NULL, false, 0, SAU_POP_FPMOD);
		break;
	default:
		return true;
	}
	return false;
}

static void parse_in_op_step(SAU_Parser *restrict o) {
	PARSE_IN__HEAD(parse_in_op_step, pl->operator)
		SAU_ScriptOpData *op = pl->operator;
		switch (c) {
		case '/':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, false);
			}
			break;
		case '\\':
			pl->pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, true);
				pl->event->ev_flags |= SAU_SDEV_FROM_GAPSHIFT;
			}
			break;
		case 'a':
			if (parse_op_amp(o)) goto DEFER;
			break;
		case 'c':
			if (parse_op_chanmix(o)) goto DEFER;
			break;
		case 'f':
			if (parse_op_freq(o, false)) goto DEFER;
			break;
		case 'p':
			if (parse_op_phase(o)) goto DEFER;
			break;
		case 'r':
			if (parse_op_freq(o, true)) goto DEFER;
			break;
		case 't':
			if (SAU_Scanner_tryc(sc, 'd')) {
				op->time = (SAU_Time){o->sl.sopt.def_time_ms,
					0};
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_SDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				op->time = (SAU_Time){o->sl.sopt.def_time_ms,
					SAU_TIMEP_SET | SAU_TIMEP_IMPLICIT};
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				op->time = (SAU_Time){time_ms, SAU_TIMEP_SET};
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
	PARSE_IN__TAIL()
}

static bool parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c) {
	struct ParseLevel pl;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope, close_c);
	SAU_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		/* Use sub-parsing routine? May also happen in nested calls. */
		if (pl.sub_f) pl.sub_f(o);
		c = SAU_Scanner_getc(sc);
		SAU_ScanFrame sf_first = sc->sf;
		switch (c) {
		case SAU_SCAN_SPACE:
		case SAU_SCAN_LNBRK:
			pl.pl_flags &= ~PL_WARN_NOSPACE;
			continue;
		case '\'':
			/*
			 * Variable assignment, part 1; set to what follows.
			 */
			if (pl.set_var != NULL) {
				SAU_Scanner_warning(sc, NULL,
"ignoring variable assignment to variable assignment");
				break;
			}
			pl.set_var = scan_sym(sc, SAU_SYM_VAR, NULL);
			break;
		case '/':
			if (NestArr_tip(&o->nest)) goto INVALID;
			parse_waittime(o);
			break;
		case ';':
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			if ((pl.operator->time.flags &
			     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
			    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
				SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' separator");
			begin_node(o, pl.operator, true);
			pl.event->ev_flags |= SAU_SDEV_WAIT_PREV_DUR;
			pl.sub_f = parse_in_op_step;
			break;
		case '<':
			warn_opening_disallowed(sc, '<');
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
		case '=': {
			SAU_Symitem *var = pl.set_var;
			if (!var) goto INVALID;
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			pl.set_var = NULL; // used here
			if (scan_num(sc, NULL, &var->data.num))
				var->data_use = SAU_SYM_DATA_NUM;
			else
				SAU_Scanner_warning(sc, NULL,
"missing right-hand value for \"'%s=\"", var->sstr->key);
			break; }
		case '>':
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@': {
			if (SAU_Scanner_tryc(sc, '[')) {
				end_operator(o);
				NestArr_add(&o->nest);
				if (parse_level(o, pl.use_type, SCOPE_BIND,']'))
					goto RETURN;
				struct NestScope *nest = NestArr_pop(&o->nest);
				if (!nest || !nest->list->first_item) break;
				pl.pl_flags |= PL_BIND_MULTIPLE;
				begin_node(o, nest->list->first_item, false);
				/*
				 * Multiple-operator node now open.
				 */
				pl.sub_f = parse_in_op_step;
				break;
			}
			/*
			 * Variable reference (get and use object).
			 */
			pl.sub_f = NULL;
			SAU_Symitem *var = scan_sym(sc, SAU_SYM_VAR, NULL);
			if (var != NULL) {
				if (var->data_use == SAU_SYM_DATA_OBJ) {
					SAU_ScriptOpData *ref = var->data.obj;
					begin_node(o, ref, false);
					ref = pl.operator;
					var->data.obj = ref; /* update */
					pl.sub_f = parse_in_op_step;
				} else {
					SAU_Scanner_warning(sc, NULL,
"reference '@%s' doesn't point to an object", var->sstr->key);
				}
			}
			break; }
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			struct NestScope *nest = NestArr_tip(&o->nest);
			if (!pl.use_type && nest && nest->op_sweep) {
				SAU_Scanner_warning(sc, NULL, "modulators not supported here");
				break;
			}
			begin_node(o, NULL, false);
			pl.operator->wave = wave;
			pl.sub_f = parse_in_op_step;
			break; }
		case 'S':
			pl.sub_f = parse_in_settings;
			break;
		case '[':
			warn_opening_disallowed(sc, '[');
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
		case ']':
			if (c == close_c) {
				if (pl.scope == SCOPE_NEST) end_operator(o);
				endscope = true;
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '{':
			if (parse_level(o, pl.use_type, SCOPE_GROUP, '}'))
				goto RETURN;
			continue;
		case '|':
			if (NestArr_tip(&o->nest)) goto INVALID;
			if (newscope == SCOPE_SAME) {
				SAU_Scanner_ungetc(sc);
				goto RETURN;
			}
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			end_event(o);
			finish_durgroup(o);
			pl.sub_f = NULL;
			continue;
		case '}':
			if (c == close_c) goto RETURN;
			warn_closing_without_opening(sc, '}', '{');
			break;
		default:
		INVALID:
			if (!handle_unknown_or_eof(sc, c)) goto FINISH;
			continue;
		}
		if (pl.pl_flags & PL_WARN_NOSPACE)
			warn_missing_whitespace(sc, &sf_first, c);
		pl.pl_flags |= PL_WARN_NOSPACE;
	}
FINISH:
	if (close_c && c != close_c) warn_eof_without_closing(sc, close_c);
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
		const SAU_ScriptArg *restrict arg) {
	SAU_Scanner *sc = o->sc;
	const char *name;
	if (!SAU_Scanner_open(sc, arg->str, arg->is_path)) {
		return NULL;
	}
	parse_level(o, SAU_POP_CARR, SCOPE_GROUP, 0);
	name = sc->f->path;
	SAU_Scanner_close(sc);
	return name;
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

static void time_op_ramps(SAU_ScriptOpData *restrict op);
static uint32_t time_event(SAU_ScriptEvData *restrict e);
static void flatten_events(SAU_ScriptEvData *restrict e);

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static SAU_ScriptEvData *time_durgroup(SAU_ScriptEvData *restrict e_from,
		uint32_t *restrict wait_after) {
	SAU_ScriptEvData *e, *e_subtract_after = e_from;
	uint32_t cur_longest = 0, wait_sum = 0, group_carry = 0;
	bool subtract = false;
	for (e = e_from; e; ) {
		if (!(e->ev_flags & SAU_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_SDEV_VOICE_SET_DUR;
		time_event(e);
		if ((e->ev_flags & SAU_SDEV_VOICE_SET_DUR) != 0 &&
		    cur_longest < e->dur_ms) {
			cur_longest = e->dur_ms;
			group_carry = cur_longest;
			e_subtract_after = e;
		}
		if (!e->next) break;
		e = e->next;
		if (cur_longest > e->wait_ms)
			cur_longest -= e->wait_ms;
		else
			cur_longest = 0;
		wait_sum += e->wait_ms;
	}
	/*
	 * Flatten event forks in loop following the timing adjustments
	 * depending on composite step event structure, complete times.
	 */
	for (e = e_from; e; ) {
		while (e->forks != NULL) flatten_events(e);
		/*
		 * Track sequence of references and later use here.
		 */
		for (SAU_ScriptOpData *op = e->objs.first_item; op;
				op = op->next) {
			if (!(op->time.flags & SAU_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = cur_longest + wait_sum;
				op->time.flags |= SAU_TIMEP_SET;
				if (e->dur_ms < op->time.v_ms)
					e->dur_ms = op->time.v_ms;
				time_op_ramps(op);
			}
			SAU_ScriptOpData *prev_ref = op->info->last_ref;
			if (prev_ref != NULL) {
				op->prev_ref = prev_ref;
				prev_ref->op_flags |= SAU_SDOP_LATER_USED;
				prev_ref->event->ev_flags |=
					SAU_SDEV_VOICE_LATER_USED;
			}
			op->info->last_ref = op;
		}
		if (!e->next) break;
		if (e == e_subtract_after) subtract = true;
		e = e->next;
		wait_sum -= e->wait_ms;
		if (subtract) {
			if (group_carry >= e->wait_ms)
				group_carry -= e->wait_ms;
			else
				group_carry = 0;
		}
	}
	if (wait_after) *wait_after += group_carry;
	return e;
}

static void time_op_ramps(SAU_ScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	time_ramp(op->pan, dur_ms);
	time_ramp(op->amp, dur_ms);
	time_ramp(op->amp2, dur_ms);
	time_ramp(op->freq, dur_ms);
	time_ramp(op->freq2, dur_ms);
}

static uint32_t time_operator(SAU_ScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	if (!(op->params & SAU_POPP_TIME))
		op->event->ev_flags &= ~SAU_SDEV_VOICE_SET_DUR;
	if (!(op->time.flags & SAU_TIMEP_SET)) {
		op->time.flags |= SAU_TIMEP_DEFAULT;
		if (op->op_flags & SAU_SDOP_NESTED) {
			op->time.flags |= SAU_TIMEP_IMPLICIT;
			op->time.flags |= SAU_TIMEP_SET; /* no durgroup yet */
		}
	} else if (!(op->op_flags & SAU_SDOP_NESTED)) {
		op->event->ev_flags |= SAU_SDEV_LOCK_DUR_SCOPE;
	}
	for (SAU_ScriptListData *list = op->mods;
			list != NULL; list = list->next) {
		for (SAU_ScriptOpData *sub_op = list->first_item;
				sub_op != NULL; sub_op = sub_op->next) {
			uint32_t sub_dur_ms = time_operator(sub_op);
			if (dur_ms < sub_dur_ms
			    && (op->time.flags & SAU_TIMEP_DEFAULT) != 0)
				dur_ms = sub_dur_ms;
		}
	}
	op->time.v_ms = dur_ms;
	time_op_ramps(op);
	return dur_ms;
}

static uint32_t time_event(SAU_ScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	for (SAU_ScriptOpData *op = e->objs.first_item; op; op = op->next) {
		uint32_t sub_dur_ms = time_operator(op);
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
		SAU_ScriptOpData *ne_op = ne->objs.first_item,
				 *ne_op_prev = ne_op->prev_ref,
				 *e_op = ne_op_prev;
		uint32_t first_time_ms = e_op->time.v_ms;
		SAU_Time def_time = {
			e_op->time.v_ms,
			(e_op->time.flags & SAU_TIMEP_IMPLICIT)
		};
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SAU_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_SDEV_VOICE_SET_DUR;
		for (;;) {
			wait_sum_ms += ne->wait_ms;
			if (!(ne_op->time.flags & SAU_TIMEP_SET)) {
				ne_op->time = def_time;
				if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT)
					ne_op->time.flags |= SAU_TIMEP_SET |
						SAU_TIMEP_DEFAULT;
			}
			time_event(ne);
			def_time = (SAU_Time){
				ne_op->time.v_ms,
				(ne_op->time.flags & SAU_TIMEP_IMPLICIT)
			};
			if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT) {
				if (ne_op_prev->time.flags & SAU_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SAU_SDEV_FROM_GAPSHIFT)) {
					ne_op_prev->time = (SAU_Time){ // gap
						0,
						SAU_TIMEP_SET|SAU_TIMEP_DEFAULT
					};
				}
			}
			if (ne->ev_flags & SAU_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_op_prev->time.v_ms;
				ne_op_prev->time.flags &= ~SAU_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_op->time.flags |= SAU_TIMEP_SET;
			ne_op->params |= SAU_POPP_TIME;
			ne_op_prev = ne_op;
			ne_prev = ne;
			ne = ne->next;
			if (!ne) break;
			ne_op = ne->objs.first_item;
		}
		/*
		 * Exclude nested operators when setting a longer duration,
		 * if time has already been explicitly set for any carriers
		 * (otherwise the duration can be misreported as too long).
		 *
		 * TODO: Replace with design that gives nodes at each level
		 * their own event. Merge event and data nodes (always make
		 * new events for everything), or event and durgroup nodes?
		 */
		if (!(e->ev_flags & SAU_SDEV_LOCK_DUR_SCOPE)
		    || !(e_op->op_flags & SAU_SDOP_NESTED)) {
			if (dur_ms < first_time_ms)
				dur_ms = first_time_ms;
//			if (dur_ms < nest_dur_ms)
//				dur_ms = nest_dur_ms;
		}
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
	SAU_ScriptEvBranch *fork = e->forks;
	SAU_ScriptEvData *ne = fork->events;
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
	e->forks = fork->prev;
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SAU_Script* SAU_read_Script(const SAU_ScriptArg *restrict arg) {
	if (!arg)
		return NULL;
	SAU_Parser pr;
	SAU_Script *o = NULL;
	init_Parser(&pr, arg);
	const char *name = parse_file(&pr, arg);
	if (!name) goto DONE;
	o = SAU_mpalloc(pr.mp, sizeof(SAU_Script));
	o->mp = pr.mp;
	o->prg_mp = pr.prg_mp;
	o->st = pr.st;
	o->events = pr.events;
	o->name = name;
	o->sopt = pr.sl.sopt;
	pr.mp = pr.prg_mp = NULL; // keep with result

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
	SAU_destroy_Mempool(o->prg_mp);
	SAU_destroy_Mempool(o->mp);
}
