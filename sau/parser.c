/* SAU library: Script parser module.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

enum {
	SAU_SYM_VAR = 0,
	SAU_SYM_MATH_ID,
	SAU_SYM_LINE_ID,
	SAU_SYM_WAVE_ID,
	SAU_SYM_TYPES
};

static const char *const scan_sym_labels[SAU_SYM_TYPES] = {
	"variable",
	"math symbol",
	"line shape",
	"wave type",
};

struct ScanLookup {
	sauScriptOptions sopt;
	struct sauMath_state math_state;
};

/*
 * Default script options, used until changed in a script.
 */
static const sauScriptOptions def_sopt = {
	.set = 0,
	.ampmult = 1.f,
	.A4_freq = 440.f,
	.def_time_ms = 1000,
	.def_freq = 440.f,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		const sauScriptArg *restrict arg,
		sauSymtab *restrict st) {
	o->sopt = def_sopt;
	if (!sauSymtab_add_stra(st, sauMath_names, SAU_MATH_NAMED,
			SAU_SYM_MATH_ID) ||
	    !sauSymtab_add_stra(st, sauLine_names, SAU_LINE_NAMED,
			SAU_SYM_LINE_ID) ||
	    !sauSymtab_add_stra(st, sauWave_names, SAU_WAVE_NAMED,
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
static bool handle_unknown_or_eof(sauScanner *restrict o, uint8_t c) {
	if (c == 0)
		return false;
	const char *warn_str = SAU_IS_ASCIIVISIBLE(c) ?
		(IS_UPPER(c) ?
		"invalid or misplaced typename '%c'" :
		(IS_LOWER(c) ?
		"invalid or misplaced subname '%c'" :
		"misplaced or unrecognized '%c'")) :
		"invalid character (value 0x%02hhX)";
	sauScanner_warning(o, NULL, warn_str, c);
	return true;
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_eof_without_closing(sauScanner *restrict o, uint8_t c) {
	sauScanner_warning(o, NULL, "end of file without closing '%c'", c);
}

/*
 * Print warning for scope-opening character in disallowed place.
 */
static void warn_opening_disallowed(sauScanner *restrict o,
		uint8_t open_c) {
	sauScanner_warning(o, NULL, "opening '%c' out of place",
			open_c);
}

/*
 * Print warning for scope-closing character without scope-opening character.
 */
static void warn_closing_without_opening(sauScanner *restrict o,
		uint8_t close_c, uint8_t open_c) {
	sauScanner_warning(o, NULL, "closing '%c' without opening '%c'",
			close_c, open_c);
}

/*
 * Print warning for missing whitespace before character.
 */
static void warn_missing_whitespace(sauScanner *restrict o,
		sauScanFrame *sf, uint8_t next_c) {
	sauScanner_warning(o, sf, "missing whitespace before '%c'", next_c);
}

/*
 * Print warning for use of deprecated feature or alias.
 */
static void warn_deprecated(sauScanner *restrict o,
		const char *restrict old, const char *restrict new) {
	sauScanner_warning(o, NULL, "%s is deprecated, use new %s", old, new);
}

/*
 * Handle '#'-commands.
 */
static uint8_t scan_filter_hashcommands(sauScanner *restrict o, uint8_t c) {
	sauFile *f = o->f;
	uint8_t next_c = sauFile_GETC(f);
	if (next_c == '!') {
		++o->sf.char_num;
		return sauScanner_filter_linecomment(o, next_c);
	}
	if (next_c == 'Q') {
		sauFile_DECP(f);
		sauScanner_close(o);
		return SAU_SCAN_EOF;
	}
	sauFile_DECP(f);
	return c;
}

static sauSymitem *scan_sym(sauScanner *restrict o, uint32_t type_id,
		const char *const*restrict help_stra, bool optional) {
	const char *type_label = scan_sym_labels[type_id];
	sauScanFrame sf_begin = o->sf;
	sauSymstr *s = NULL;
	sauScanner_get_symstr(o, &s);
	if (!s) goto NOT_FOUND;
	sauSymitem *item = sauSymtab_find_item(o->symtab, s, type_id);
	if (!item) {
		if (type_id != SAU_SYM_VAR) goto NOT_FOUND;
		item = sauSymtab_add_item(o->symtab, s, SAU_SYM_VAR);
	}
	return item;
NOT_FOUND:
	if (!s) {
		if (optional)
			return NULL;
		const char *msg = help_stra ?
				"%s name missing; available are:" :
				"%s name missing";
		sauScanner_warning(o, NULL, msg, type_label);
		if (help_stra) sau_print_names(help_stra, "\t", stderr);
	} else if (help_stra) /* standard warning produced here */ {
		sauScanner_warning(o, &sf_begin,
				"invalid %s name '%s'; available are:",
				type_label, s->key);
		sau_print_names(help_stra, "\t", stderr);
	}
	return NULL;
}

static bool scan_mathfunc(sauScanner *restrict o, size_t *restrict found_id) {
	sauSymitem *sym = scan_sym(o, SAU_SYM_MATH_ID, sauMath_names, false);
	if (!sym)
		return false;
	if (sauMath_params[sym->data.id] == SAU_MATH_NOARG_F // no parentheses
	    || sauScanner_tryc(o, '(')) {
		*found_id = sym->data.id;
		return true;
	}
	sauScanner_warning(o, NULL,
"expected '(' following math function name '%s'", sauMath_names[sym->data.id]);
	return false;
}

struct NumParser {
	sauScanner *sc;
	sauScanNumConst_f numconst_f;
	sauScanFrame sf_start;
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
	sauScanner *sc = o->sc;
	struct ScanLookup *sl = sc->data;
	uint8_t ws_level = sc->ws_level;
	double num;
	uint8_t c;
	if (level == 1 && ws_level != SAU_SCAN_WS_NONE)
		sauScanner_setws_level(sc, SAU_SCAN_WS_NONE);
	c = sauScanner_getc(sc);
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else if (c == '+' || c == '-') {
		num = scan_num_r(o, NUMEXP_ADT, level);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else if (c == '$') {
		sauSymitem *var = scan_sym(sc, SAU_SYM_VAR, NULL, false);
		if (!var) goto REJECT;
		if (var->data_use != SAU_SYM_DATA_NUM) {
			sauScanner_warning(sc, NULL,
"variable '$%s' in numerical expression doesn't hold a number", var->sstr->key);
			goto REJECT;
		}
		num = var->data.num;
	} else {
		size_t func_id = 0, read_len = 0;
		sauScanner_ungetc(sc);
		sauScanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0) {
			if (!IS_ALPHA(c) || !scan_mathfunc(sc, &func_id))
				goto REJECT; /* silent NaN (nothing was read) */
			switch (sauMath_params[func_id]) {
			case SAU_MATH_VAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = sauMath_symbols[func_id].val(num);
				break;
			case SAU_MATH_STATE_F:
				sauScanner_skipws(sc);
				if (!sauScanner_tryc(sc, ')')) {
					sauScanner_warning(sc, NULL,
"math function '%s()' takes no arguments", sauMath_names[func_id]);
					goto REJECT;
				}
				num = sauMath_symbols[func_id]
					.state(&sl->math_state);
				break;
			case SAU_MATH_STATEVAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				num = sauMath_symbols[func_id]
					.stateval(&sl->math_state, num);
				break;
			case SAU_MATH_NOARG_F:
				num = sauMath_symbols[func_id].noarg();
				break;
			default:
				sau_error("scan_num_r",
"math function '%s' has unimplemented parameter type",
						sauMath_names[func_id]);
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
		c = sauScanner_getc(sc);
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
				sauScanner_ungetc(sc);
				double rval = scan_num_r(o, NUMEXP_MLT, level);
				if (isnan(rval)) goto ACCEPT;
				num *= rval;
				break;
			}
			if (pri == NUMEXP_SUB && level > 0) {
				sauScanner_warning(sc, &o->sf_start,
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
	sauScanner_ungetc(sc);
ACCEPT:
	if (0)
REJECT: {
		num = NAN;
	}
	if (ws_level != sc->ws_level)
		sauScanner_setws_level(sc, ws_level);
	return num;
}
static sauNoinline bool scan_num(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst, double *restrict var) {
	struct NumParser np = {o, scan_numconst, o->sf, false, false, false};
	double num = scan_num_r(&np, NUMEXP_SUB, 0);
	if (np.has_nannum) {
		sauScanner_warning(o, &np.sf_start,
				"discarding expression containing NaN value");
		return false;
	}
	if (isnan(num)) /* silent NaN (ignored blank expression) */
		return false;
	if (isinf(num)) np.has_infnum = true;
	if (np.has_infnum) {
		sauScanner_warning(o, &np.sf_start,
				"discarding expression with infinite number");
		return false;
	}
	*var = num;
	return true;
}

static sauNoinline bool scan_time_val(sauScanner *restrict o,
		uint32_t *restrict val) {
	sauScanFrame sf = o->sf;
	double val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		sauScanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = sau_ui32rint(val_s * 1000.f);
	return true;
}

static sauNoinline bool scan_digit_val(sauScanner *restrict o,
		int32_t *restrict val) {
	sauScanFrame sf = o->sf;
	size_t num_len;
	int32_t num;
	sauScanner_geti(o, &num, false, &num_len);
	if (!num_len)
		return false;
	if (num_len > 1) {
		sauScanner_warning(o, &sf,
				"discarding integer out of range (0-9)");
		return false;
	}
	*val = num;
	return true;
}

static size_t scan_chanmix_const(sauScanner *restrict o,
		double *restrict val) {
	char c = sauFile_GETC(o->f);
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
		sauFile_DECP(o->f);
		return 0;
	}
}

#define OCTAVES 11
static size_t scan_note_const(sauScanner *restrict o,
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
	sauFile *f = o->f;
	struct ScanLookup *sl = o->data;
	size_t len = 0, num_len;
	uint8_t c;
	double freq;
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	c = sauFile_GETC(f); ++len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = sauFile_GETC(f); ++len;
	}
	if (c < 'A' || c > 'G') {
		sauFile_UNGETN(f, len);
		return 0;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = sauFile_GETC(f); ++len;
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else {
		sauFile_DECP(f); --len;
	}
	sauFile_geti(f, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		sauScanner_warning(o, NULL,
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

static size_t scan_phase_const(sauScanner *restrict o,
		double *restrict val) {
	char c = sauFile_GETC(o->f);
	switch (c) {
	case 'G':
		*val = SAU_GLDA_1_2PI;
		return 1;
	default:
		sauFile_DECP(o->f);
		return 0;
	}
}

static bool scan_sym_id(sauScanner *restrict o,
		size_t *restrict found_id, uint32_t type_id,
		const char *const*restrict help_stra) {
	sauSymitem *sym = scan_sym(o, type_id, help_stra, true);
	if (!sym)
		return false;
	*found_id = sym->data.id;
	return true;
}

static bool scan_line_state(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLine *restrict line, bool ratio) {
	double v0;
	if (!scan_num(o, scan_numconst, &v0))
		return false;
	line->v0 = v0;
	line->flags |= SAU_LINEP_STATE;
	if (ratio)
		line->flags |= SAU_LINEP_STATE_RATIO;
	else
		line->flags &= ~SAU_LINEP_STATE_RATIO;
	return true;
}

/*
 * Parser
 */

struct NestScope {
	sauScriptListData *list, *last_mods;
	sauScriptOpData *last_item;
	sauScriptOptions sopt_save; /* save/restore on nesting */
	/* values passed for outer parameter */
	sauLine *op_sweep;
	sauScanNumConst_f numconst_f;
	bool num_ratio;
};

sauArrType(NestArr, struct NestScope, )

typedef struct sauParser {
	struct ScanLookup sl;
	sauScanner *sc;
	sauSymtab *st;
	sauMempool *mp, *tmp_mp, *prg_mp;
	NestArr nest;
	/* node state */
	struct ParseLevel *cur_pl;
	sauScriptEvData *events, *last_event, *group_event;
} sauParser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(sauParser *restrict o) {
	sau_destroy_Scanner(o->sc);
	sau_destroy_Mempool(o->tmp_mp);
	sau_destroy_Mempool(o->prg_mp);
	sau_destroy_Mempool(o->mp);
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
static bool init_Parser(sauParser *restrict o,
		const sauScriptArg *restrict script_arg) {
	sauMempool *mp = sau_create_Mempool(0),
		    *tmp_mp = sau_create_Mempool(0),
		    *prg_mp = sau_create_Mempool(0);
	sauSymtab *st = sau_create_Symtab(mp);
	sauScanner *sc = sau_create_Scanner(st);
	*o = (sauParser){0};
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

typedef void (*ParseLevel_sub_f)(sauParser *restrict o);
static void parse_in_settings(sauParser *restrict o);
static void parse_in_op_step(sauParser *restrict o);
static void parse_in_par_sweep(sauParser *restrict o);

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
	sauScriptEvData *event;
	sauScriptOpData *operator, *ev_last;
	sauSymitem *set_var;
	/* timing/delay */
	sauScriptEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t add_wait_ms, carry_wait_ms; /* added for next event */
	float used_ampmult; /* update on node creation */
};

typedef struct sauScriptEvBranch {
	sauScriptEvData *events;
	struct sauScriptEvBranch *prev;
} sauScriptEvBranch;

static sauLine *create_line(sauParser *restrict o,
		bool mult, uint32_t par_flag) {
	struct ScanLookup *sl = &o->sl;
	sauLine *line = sau_mpalloc(o->prg_mp, sizeof(sauLine));
	float v0 = 0.f;
	if (!line)
		return NULL;
	line->type = SAU_LINE_N_lin; // default if goal enabled
	switch (par_flag) {
	case SAU_PSWEEP_PAN:
		v0 = sl->sopt.def_chanmix;
		break;
	case SAU_PSWEEP_AMP:
		v0 = 1.0f; /* multiplied with sl->sopt.ampmult separately */
		break;
	case SAU_PSWEEP_AMP2:
		v0 = 0.f;
		break;
	case SAU_PSWEEP_FREQ:
		v0 = mult ?
			sl->sopt.def_relfreq :
			sl->sopt.def_freq;
		break;
	case SAU_PSWEEP_FREQ2:
		v0 = 0.f;
		break;
	default:
		return NULL;
	}
	line->v0 = v0;
	line->flags |= SAU_LINEP_STATE |
		SAU_LINEP_TYPE |
		SAU_LINEP_TIME_IF_NEW; /* don't set main SAU_LINEP_TIME here */
	if (mult) {
		line->flags |= SAU_LINEP_STATE_RATIO;
	}
	return line;
}

static bool parse_waittime(sauParser *restrict o) {
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

static void end_operator(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_OP))
		return;
	pl->pl_flags &= ~PL_OWN_OP;
	sauScriptOpData *op = pl->operator;
	if (op->amp) {
		op->amp->v0 *= pl->used_ampmult;
		op->amp->vt *= pl->used_ampmult;
	}
	if (op->amp2) {
		op->amp2->v0 *= pl->used_ampmult;
		op->amp2->vt *= pl->used_ampmult;
	}
	sauScriptOpData *pop = op->prev_ref;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->params = SAU_POP_PARAMS;
	}
	pl->operator = NULL;
}

static void end_event(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_EV))
		return;
	pl->pl_flags &= ~PL_OWN_EV;
	end_operator(o);
	pl->ev_last = NULL;
	pl->event = NULL;
}

static void begin_event(sauParser *restrict o,
		sauScriptOpData *restrict prev_data,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptEvData *e;
	end_event(o);
	pl->event = sau_mpalloc(o->mp, sizeof(sauScriptEvData));
	e = pl->event;
	e->wait_ms = pl->add_wait_ms + pl->carry_wait_ms;
	pl->add_wait_ms = pl->carry_wait_ms = 0;
	if (prev_data != NULL) {
		sauScriptEvData *pve = prev_data->event;
		if (prev_data->op_flags & SAU_SDOP_NESTED)
			e->ev_flags |= SAU_SDEV_IMPLICIT_TIME;
		e->root_ev = prev_data->info->root_event;
		if (is_compstep) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				sauScriptEvBranch *fork =
					sau_mpalloc(o->tmp_mp, sizeof(*fork));
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

static void begin_operator(sauParser *restrict o,
		sauScriptOpData *restrict pop, bool is_compstep,
		uint32_t type) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptEvData *e = pl->event;
	sauScriptOpData *op;
	/*
	 * It is assumed that a valid event exists.
	 */
	end_operator(o);
	pl->operator = op = sau_mpalloc(o->mp, sizeof(sauScriptOpData));
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
		op->time = sauTime_DEFAULT(pop->time.v_ms,
				pop->time.flags & SAU_TIMEP_IMPLICIT);
		op->wave = pop->wave;
		op->phase = pop->phase;
		op->info = pop->info;
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			sauScriptOpData *mpop = pop;
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
		bool is_nested = pl->use_type != SAU_POP_CARR;
		op->time = sauTime_DEFAULT(o->sl.sopt.def_time_ms, is_nested);
		if (!is_nested) {
			op->pan = create_line(o, false, SAU_PSWEEP_PAN);
			op->freq = create_line(o, false, SAU_PSWEEP_FREQ);
		} else {
			op->op_flags |= SAU_SDOP_NESTED;
			op->freq = create_line(o, true, SAU_PSWEEP_FREQ);
		}
		op->amp = create_line(o, false, SAU_PSWEEP_AMP);
		op->info = sau_mpalloc(o->mp, sizeof(sauScriptObjInfo));
		op->info->root_event = e;
		op->info->type = type;
		if (type == SAU_POPT_RAS)
			op->info->seed = sau_rand32(&o->sl.math_state);
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
static void begin_node(sauParser *restrict o,
		sauScriptOpData *restrict previous, bool is_compstep,
		uint32_t type) {
	struct ParseLevel *pl = o->cur_pl;
	if (!pl->event || pl->add_wait_ms > 0 ||
			/* previous event implicitly ended */
			((previous || pl->use_type == SAU_POP_CARR)
			 && pl->event->objs.first_item) ||
			is_compstep)
		begin_event(o, previous, is_compstep);
	begin_operator(o, previous, is_compstep, type);
}

static sauScriptEvData *time_durgroup(sauScriptEvData *restrict e_from,
		uint32_t *restrict wait_after);

static void finish_durgroup(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	pl->add_wait_ms = 0; /* reset by each '|' boundary */
	if (!o->group_event)
		return; /* nothing to do */
	o->last_event = time_durgroup(o->group_event, &pl->carry_wait_ms);
	o->group_event = NULL;
}

static void enter_level(sauParser *restrict o,
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
			nest->list = sau_mpalloc(o->mp, sizeof(*nest->list));
			pl->sub_f = NULL;
		} else if (newscope == SCOPE_NEST) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = sau_mpalloc(o->mp, sizeof(*nest->list));
			pl->sub_f = nest->op_sweep ? parse_in_par_sweep : NULL;
			sauScriptOpData *parent_on = parent_pl->operator;
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

static void leave_level(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_operator(o);
	if (pl->set_var != NULL) {
		sauScanner_warning(o->sc, NULL,
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
	sauScanner *sc = o->sc; \
	if (!(GuardCond)) { pl->sub_f = NULL; return; } \
	pl->sub_f = (Name); \
	for (;;) { \
		uint8_t c = sauScanner_getc(sc); \
		sauScanFrame sf_first = sc->sf; \
		/* switch (c) { ... default: ... goto DEFER; } */

#define PARSE_IN__TAIL() \
		/* switch (c) { ... default: ... goto DEFER; } */ \
		if (pl->pl_flags & PL_WARN_NOSPACE) \
			warn_missing_whitespace(sc, &sf_first, c); \
		pl->pl_flags |= PL_WARN_NOSPACE; \
	} \
	return; \
DEFER: \
	sauScanner_ungetc(sc); /* let parse_level() take care of it */

static void parse_in_settings(sauParser *restrict o) {
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
			if (sauScanner_tryc(sc, '.') &&
			    sauScanner_tryc(sc, 'n')) {
				if (scan_num(sc, NULL, &val)) {
					if (val < 1.f) {
						sauScanner_warning(sc, NULL,
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

static bool parse_level(sauParser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c);

static void parse_in_par_sweep(sauParser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	sauLine *line = nest->op_sweep;
	if (!(line->flags & SAU_LINEP_TIME)) {
		line->time_ms = o->sl.sopt.def_time_ms;
		line->flags |= SAU_LINEP_TIME;
	}
	PARSE_IN__HEAD(parse_in_par_sweep, true)
		double val;
		switch (c) {
		case 'g':
			if (scan_num(sc, nest->numconst_f, &val)) {
				line->vt = val;
				line->flags |= SAU_LINEP_GOAL;
				if (nest->num_ratio)
					line->flags |= SAU_LINEP_GOAL_RATIO;
				else
					line->flags &= ~SAU_LINEP_GOAL_RATIO;
			}
			break;
		case 'r':
			warn_deprecated(sc, "sweep parameter 'r'", "name 'l'");
			/* fall-through */
		case 'l': {
			size_t id;
			if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID,
						sauLine_names))
				break;
			line->type = id;
			line->flags |= SAU_LINEP_TYPE;
			break; }
		case 't':
			if (scan_time_val(sc, &line->time_ms))
				line->flags &= ~SAU_LINEP_TIME_IF_NEW;
			break;
		case 'v':
			scan_line_state(sc, nest->numconst_f, line,
					nest->num_ratio);
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool prepare_sweep(sauParser *restrict o,
		struct NestScope *restrict nest,
		sauScanNumConst_f numconst_f,
		sauLine **restrict op_sweep, bool ratio,
		uint8_t sweep_id) {
	if (!op_sweep) { /* clear when not provided */
		nest->op_sweep = NULL;
		return true;
	}
	if (!*op_sweep) { /* create for updating, unparsed values kept unset */
		*op_sweep = create_line(o, ratio, sweep_id);
		(*op_sweep)->flags &= ~(SAU_LINEP_STATE | SAU_LINEP_TYPE);
	}
	nest->op_sweep = *op_sweep;
	nest->numconst_f = numconst_f;
	nest->num_ratio = ratio;
	return true;
}

static void parse_par_list(sauParser *restrict o,
		sauScanNumConst_f numconst_f,
		sauLine **restrict op_sweep, bool ratio,
		uint8_t sweep_id, uint8_t use_type) {
	struct NestScope *nest = NestArr_add(&o->nest);
	prepare_sweep(o, nest, numconst_f, op_sweep, ratio, sweep_id);
	if (op_sweep)
		scan_line_state(o->sc, numconst_f, *op_sweep, ratio);
	bool clear = sauScanner_tryc(o->sc, '-');
	while (sauScanner_tryc(o->sc, '[')) {
		parse_level(o, use_type, SCOPE_NEST, ']');
		nest = NestArr_tip(&o->nest); // re-get, array may have changed
		if (clear) clear = false;
		else nest->list->append = true;
	}
	NestArr_pop(&o->nest);
}

static bool parse_op_amp(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptOpData *op = pl->operator;
	parse_par_list(o, NULL, &op->amp, false, SAU_PSWEEP_AMP, SAU_POP_AMOD);
	if (sauScanner_tryc(o->sc, '.')) switch (sauScanner_getc(o->sc)) {
	case 'r':
		parse_par_list(o, NULL, &op->amp2, false,
				SAU_PSWEEP_AMP2, SAU_POP_RAMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_op_chanmix(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptOpData *op = pl->operator;
	if (op->op_flags & SAU_SDOP_NESTED)
		return true; // reject
	parse_par_list(o, scan_chanmix_const, &op->pan, false, SAU_PSWEEP_PAN,0);
	return false;
}

static bool parse_op_freq(sauParser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	sauScanNumConst_f num_f = rel_freq ? NULL : scan_note_const;
	parse_par_list(o, num_f, &op->freq, rel_freq,
			SAU_PSWEEP_FREQ, SAU_POP_FMOD);
	if (sauScanner_tryc(o->sc, '.')) switch (sauScanner_getc(o->sc)) {
	case 'r':
		parse_par_list(o, num_f, &op->freq2, rel_freq,
				SAU_PSWEEP_FREQ2, SAU_POP_RFMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_op_mode(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	sauScriptOpData *op = pl->operator;
	if (op->info->type != SAU_POPT_RAS)
		return true; // reject
	uint8_t func = SAU_RAS_FUNCTIONS;
	uint8_t flags = 0;
	int32_t level = -1;
	for (;;) {
		char c;
		int matched = 0;
		if (!(func < SAU_RAS_FUNCTIONS) && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case 'r': func = SAU_RAS_F_RAND; break;
		case 'g': func = SAU_RAS_F_GAUSS; break;
		case 'b': func = SAU_RAS_F_BIN; break;
		case 't': func = SAU_RAS_F_TERN; break;
		case 'f': func = SAU_RAS_F_FIXED; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (flags != SAU_RAS_O_FUNC_FLAGS && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case 'v': flags |= SAU_RAS_O_VIOLET; break;
		case 's': flags |= SAU_RAS_O_SQUARE; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (!(level >= 0) && ++matched) {
			c = sauScanner_retc(sc);
			if (IS_DIGIT(c)) scan_digit_val(sc, &level);
			else --matched;
		}
		if (matched == 0)
			break;
	}
	if (func < SAU_RAS_FUNCTIONS) {
		op->ras_opt.func = func;
		op->ras_opt.flags &= SAU_RAS_O_LINE_SET;
		op->ras_opt.flags |= SAU_RAS_O_FUNC_SET;
		op->params |= SAU_POPP_RAS;
	}
	if (flags) {
		op->ras_opt.flags |= flags;
		op->params |= SAU_POPP_RAS;
	}
	if (level >= 0) {
		op->ras_opt.level = sau_ras_level(level);
		op->ras_opt.flags |= SAU_RAS_O_LEVEL_SET;
		op->params |= SAU_POPP_RAS;
	}
	return false;
}

static bool parse_op_phase(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptOpData *op = pl->operator;
	double val;
	if (scan_num(o->sc, scan_phase_const, &val)) {
		op->phase = sau_cyclepos_dtoui32(val);
		op->params |= SAU_POPP_PHASE;
	}
	parse_par_list(o, NULL, NULL, false, 0, SAU_POP_PMOD);
	if (sauScanner_tryc(o->sc, '.')) switch (sauScanner_getc(o->sc)) {
	case 'f':
		parse_par_list(o, NULL, NULL, false, 0, SAU_POP_FPMOD);
		break;
	default:
		return true;
	}
	return false;
}

static void parse_in_op_step(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_op_step, pl->operator)
		sauScriptOpData *op = pl->operator;
		switch (c) {
		case '/':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, false, 0);
			}
			break;
		case ';':
			pl->pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, true, 0);
				pl->event->ev_flags |= SAU_SDEV_FROM_GAPSHIFT;
			} else {
				if ((op->time.flags &
				     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
				    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' without number");
				begin_node(o, pl->operator, true, 0);
				pl->event->ev_flags |= SAU_SDEV_WAIT_PREV_DUR;
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
		case 'l': {
			if (op->info->type != SAU_POPT_RAS) goto DEFER;
			size_t id;
			if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID,
						sauLine_names))
				break;
			op->ras_opt.line = id;
			op->ras_opt.flags |= SAU_RAS_O_LINE_SET;
			op->params |= SAU_POPP_RAS;
			break; }
		case 'm':
			if (parse_op_mode(o)) goto DEFER;
			break;
		case 'p':
			if (parse_op_phase(o)) goto DEFER;
			break;
		case 'r':
			if (parse_op_freq(o, true)) goto DEFER;
			break;
		case 't':
			if (sauScanner_tryc(sc, 'd')) {
				op->time = sauTime_DEFAULT(
						o->sl.sopt.def_time_ms, 0);
			} else if (sauScanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_SDOP_NESTED)) {
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				op->time = sauTime_VALUE(
						o->sl.sopt.def_time_ms, 1);
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				op->time = sauTime_VALUE(time_ms, 0);
			}
			op->params |= SAU_POPP_TIME;
			break;
		case 'w': {
			if (op->info->type != SAU_POPT_WAVE) goto DEFER;
			size_t id;
			if (!scan_sym_id(sc, &id, SAU_SYM_WAVE_ID,
						sauWave_names))
				break;
			op->wave = id;
			op->params |= SAU_POPP_WAVE;
			break; }
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_level(sauParser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c) {
	struct ParseLevel pl;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope, close_c);
	sauScanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		/* Use sub-parsing routine? May also happen in nested calls. */
		if (pl.sub_f) pl.sub_f(o);
		c = sauScanner_getc(sc);
		sauScanFrame sf_first = sc->sf;
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
				sauScanner_warning(sc, NULL,
"ignoring variable assignment to variable assignment");
				break;
			}
			pl.set_var = scan_sym(sc, SAU_SYM_VAR, NULL, false);
			break;
		case '/':
			if (NestArr_tip(&o->nest)) goto INVALID;
			parse_waittime(o);
			break;
		case '<':
			warn_opening_disallowed(sc, '<');
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
		case '=': {
			sauSymitem *var = pl.set_var;
			if (!var) goto INVALID;
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			pl.set_var = NULL; // used here
			if (scan_num(sc, NULL, &var->data.num))
				var->data_use = SAU_SYM_DATA_NUM;
			else
				sauScanner_warning(sc, NULL,
"missing right-hand value for \"'%s=\"", var->sstr->key);
			break; }
		case '>':
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@': {
			if (sauScanner_tryc(sc, '[')) {
				end_operator(o);
				NestArr_add(&o->nest);
				if (parse_level(o, pl.use_type, SCOPE_BIND,']'))
					goto RETURN;
				struct NestScope *nest = NestArr_pop(&o->nest);
				if (!nest || !nest->list->first_item) break;
				pl.pl_flags |= PL_BIND_MULTIPLE;
				begin_node(o, nest->list->first_item, false, 0);
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
			sauSymitem *var = scan_sym(sc, SAU_SYM_VAR,
					NULL, false);
			if (var != NULL) {
				if (var->data_use == SAU_SYM_DATA_OBJ) {
					sauScriptOpData *ref = var->data.obj;
					begin_node(o, ref, false, 0);
					ref = pl.operator;
					var->data.obj = ref; /* update */
					pl.sub_f = parse_in_op_step;
				} else {
					sauScanner_warning(sc, NULL,
"reference '@%s' doesn't point to an object", var->sstr->key);
				}
			}
			break; }
		case 'R': {
			size_t id = 0; /* default as fallback value */
			scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names);
			struct NestScope *nest = NestArr_tip(&o->nest);
			if (!pl.use_type && nest && nest->op_sweep) {
				sauScanner_warning(sc, NULL, "modulators not supported here");
				break;
			}
			begin_node(o, NULL, false, SAU_POPT_RAS);
			pl.operator->ras_opt.line = id;
			pl.operator->ras_opt.flags = SAU_RAS_O_LINE_SET;
			pl.sub_f = parse_in_op_step;
			break; }
		case 'O': {
			size_t id = 0; /* default as fallback value */
			scan_sym_id(sc, &id, SAU_SYM_WAVE_ID, sauWave_names);
			struct NestScope *nest = NestArr_tip(&o->nest);
			if (!pl.use_type && nest && nest->op_sweep) {
				sauScanner_warning(sc, NULL, "modulators not supported here");
				break;
			}
			begin_node(o, NULL, false, SAU_POPT_WAVE);
			pl.operator->wave = id;
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
				sauScanner_ungetc(sc);
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
static const char *parse_file(sauParser *restrict o,
		const sauScriptArg *restrict arg) {
	sauScanner *sc = o->sc;
	const char *name;
	if (!sauScanner_open(sc, arg->str, arg->is_path)) {
		return NULL;
	}
	parse_level(o, SAU_POP_CARR, SCOPE_GROUP, 0);
	name = sc->f->path;
	sauScanner_close(sc);
	return name;
}

static inline void time_line(sauLine *restrict line,
		uint32_t default_time_ms) {
	if (!line)
		return;
	if (line->flags & SAU_LINEP_TIME_IF_NEW) { /* update fallback value */
		line->time_ms = default_time_ms;
		line->flags |= SAU_LINEP_TIME;
	}
}

static void time_op_lines(sauScriptOpData *restrict op);
static uint32_t time_event(sauScriptEvData *restrict e);
static void flatten_events(sauScriptEvData *restrict e);

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static sauScriptEvData *time_durgroup(sauScriptEvData *restrict e_from,
		uint32_t *restrict wait_after) {
	sauScriptEvData *e, *e_subtract_after = e_from;
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
		for (sauScriptOpData *op = e->objs.first_item; op;
				op = op->next) {
			if ((op->time.flags & (SAU_TIMEP_SET|SAU_TIMEP_DEFAULT))
			    != SAU_TIMEP_SET) {
				/* fill in sensible default time */
				op->time.v_ms = cur_longest + wait_sum;
				op->time.flags |= SAU_TIMEP_SET;
				if (e->dur_ms < op->time.v_ms)
					e->dur_ms = op->time.v_ms;
				time_op_lines(op);
			}
			sauScriptOpData *prev_ref = op->info->last_ref;
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

static void time_op_lines(sauScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	time_line(op->pan, dur_ms);
	time_line(op->amp, dur_ms);
	time_line(op->amp2, dur_ms);
	time_line(op->freq, dur_ms);
	time_line(op->freq2, dur_ms);
}

static uint32_t time_operator(sauScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	if (!(op->params & SAU_POPP_TIME))
		op->event->ev_flags &= ~SAU_SDEV_VOICE_SET_DUR;
	if (!(op->time.flags & SAU_TIMEP_SET)) {
		if (op->time.flags & SAU_TIMEP_DEFAULT)
			op->time.flags |= SAU_TIMEP_SET; /* use, may adjust */
		else
			op->time.flags |= SAU_TIMEP_DEFAULT;
	} else if (!(op->op_flags & SAU_SDOP_NESTED)) {
		op->event->ev_flags |= SAU_SDEV_LOCK_DUR_SCOPE;
	}
	for (sauScriptListData *list = op->mods;
			list != NULL; list = list->next) {
		for (sauScriptOpData *sub_op = list->first_item;
				sub_op != NULL; sub_op = sub_op->next) {
			uint32_t sub_dur_ms = time_operator(sub_op);
			if (dur_ms < sub_dur_ms
			    && (op->time.flags & SAU_TIMEP_DEFAULT) != 0)
				dur_ms = sub_dur_ms;
		}
	}
	op->time.v_ms = dur_ms;
	time_op_lines(op);
	return dur_ms;
}

static uint32_t time_event(sauScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	for (sauScriptOpData *op = e->objs.first_item; op; op = op->next) {
		uint32_t sub_dur_ms = time_operator(op);
		if (dur_ms < sub_dur_ms)
			dur_ms = sub_dur_ms;
	}
	/*
	 * Timing for sub-events - done before event list flattened.
	 */
	sauScriptEvBranch *fork = e->forks;
	while (fork != NULL) {
		uint32_t nest_dur_ms = 0, wait_sum_ms = 0;
		sauScriptEvData *ne = fork->events, *ne_prev = e;
		sauScriptOpData *ne_op = ne->objs.first_item,
				 *ne_op_prev = ne_op->prev_ref,
				 *e_op = ne_op_prev;
		uint32_t first_time_ms = e_op->time.v_ms;
		uint32_t def_time_ms = e_op->time.v_ms;
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SAU_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SAU_SDEV_VOICE_SET_DUR;
		for (;;) {
			wait_sum_ms += ne->wait_ms;
			if (!(ne_op->time.flags & SAU_TIMEP_SET)) {
				ne_op->time.v_ms = def_time_ms;
				if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT)
					ne_op->time.flags |= SAU_TIMEP_SET;
			}
			time_event(ne);
			def_time_ms = ne_op->time.v_ms;
			if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT) {
				if (ne_op_prev->time.flags & SAU_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SAU_SDEV_FROM_GAPSHIFT)) /* gap */
					ne_op_prev->time = sauTime_VALUE(0, 0);
			}
			if (ne->ev_flags & SAU_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_op_prev->time.v_ms;
				ne_op_prev->time.flags &= ~SAU_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_op_prev->time.flags &= ~SAU_TIMEP_DEFAULT; // fix val
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
static void flatten_events(sauScriptEvData *restrict e) {
	sauScriptEvBranch *fork = e->forks;
	sauScriptEvData *ne = fork->events;
	sauScriptEvData *fe = e->next, *fe_prev = e;
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
		sauScriptEvData *ne_next = ne->next;
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
			sauScriptEvData *fe_next = fe->next;
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
sauScript* sau_read_Script(const sauScriptArg *restrict arg) {
	if (!arg)
		return NULL;
	sauParser pr;
	sauScript *o = NULL;
	init_Parser(&pr, arg);
	const char *name = parse_file(&pr, arg);
	if (!name) goto DONE;
	o = sau_mpalloc(pr.mp, sizeof(sauScript));
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
void sau_discard_Script(sauScript *restrict o) {
	if (!o)
		return;
	sau_destroy_Mempool(o->prg_mp);
	sau_destroy_Mempool(o->mp);
}
