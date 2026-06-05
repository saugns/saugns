/* SAU library: Script parser module.
 * Copyright (c) 2011-2012, 2017-2026 Joel K. Pettersson
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

#include "scanner.h"
#include "parse.h"
#include "help.h"
#include "math.h"
#include "arrtype.h"
#include "parser/semantics.h"

/*
 * Parse state separated by nesting depth in lists used for modulators etc.
 *
 * The global scope uses one element with a NULL \a list.
 */
struct NestScope {
	sauParseListData *list, *last_mods;
	sauParseObjRef *last_item, *owner_item;
	sauParseSetOptions *sopt; // statically (block) scoped in lists
	/* values passed for outer parameter; these are set in next-to-tip */
	void *gen_subp;
	sauScanNumConst_f numconst_f;
	uint16_t subp_part;
	bool num_ratio : 1;
	/* tracking of updates to data parsed, and copies for nodes made */
	bool is_old_sopt : 1; // sopt must be assigned to a copy if changed
};
sauArrType(NestArr, struct NestScope, )

typedef struct sauParser {
	struct sauMath_state math_state;
	sauScanner *sc;
	sauSymtab *st;
	sauMempool *mp, *tmp_mp;
	NestArr nest;
	/* node state */
	struct ParseLevel *cur_pl;
	sauParseEvData *events, *last_event, *group_event;
	bool script_fail;
	ParseSem ps;
} sauParser;

/* \return lexical scope level i.e. list nesting/block level */
static inline size_t parser_block_i(sauParser *restrict o) {
	return o->nest.count - 1;
}

/*
 * File-reading code
 */

/* Music note key 8-bit identifiers. Based on C, D, E, F, G, A, B scale. */
#define MUSKEY(note, notemod) (((note) * 9) + 4 + (notemod))
#define MUSNOTE(key) ((key) / 9)

/* Music note modifier corresponding to \p or 0 if none. */
static int notemod(char c) {
	switch (c) {
	/*case 'p':*/ /* fall-through */
	case 'd': return -1; // half-flat
	case 'z': return +1; // half-sharp
	case 'f': /* fall-through */
	case 'b': return -2; // flat
	case 's': return +2; // sharp
	case 'v': return -3; // flat-and-a-half
	case 'k': return +3; // sharp-and-a-half
	case 'w': return -4; // double-flat
	case 'x': return +4; // double-sharp
	default: return 0;
	}
}
static inline int note12to7(int n) { return n >= 5 ? (n+1)/2 : n/2; }
static inline int note7to12(int n) { return n >= 3 ? (n*2)-1 : n*2; }

#define SAU_SYM__ITEMS(X) \
	X(VAR, "variable") \
	X(LABEL, "label") /* all something-ID name types must be after this */\
	X(MATH_ID, "math symbol") \
	X(LINE_ID, "line shape") \
	X(WAVE_ID, "wave type") \
	X(NOISE_ID, "noise type") \
	//
#define SAU_SYM__X_ID(ID, STR) SAU_SYM_##ID,
#define SAU_SYM__X_STR(ID, STR) STR,

enum {
	SAU_SYM__ITEMS(SAU_SYM__X_ID)
	SAU_SYM_TYPES
};

static const char *const scan_sym_typelabels[SAU_SYM_TYPES] = {
	SAU_SYM__ITEMS(SAU_SYM__X_STR)
};

/*
 * Default script options, used until changed in a script.
 */
static const sauParseSetOptions def_sopt = {
	.ampmult = NAN,
	.A4_freq = 440.f,
	.def_time_ms = 1000,
	.def_ampmult = 1.f,
	.def_freq = SAU_PDEF_FREQ,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
	.def_pan_law = SAU_PAN_DEFAULT,
	.def_lafx = {
		.pan = SAU_LADDERFX_PAN_CHIP, .thr = SAU_LADDERFX_THR_CHIP,
		.flags = SAU_LAFXP_PAN|SAU_LAFXP_THR,
	},
	.note_key = MUSKEY(0, 0),
	.key_octave = 4,
	.key_system = 0,
	.def_parenv_v = 0.f,
	.def_ras = {0},
	.def_woo = {.func = SAU_WAVE_F_ADAA, .flags = SAU_WAVE_O_FUNC_SET},
};

static inline sauParseSetOptions*
dup_sopt(sauParser *restrict o, const sauParseSetOptions *restrict sopt) {
	return sau_mpmemdup(o->tmp_mp, sopt, sizeof(*sopt));
}

static bool init_syms(sauParser *restrict o,
		const sauScriptArg *restrict arg,
		sauSymtab *restrict st) {
	struct NestScope *ns = NestArr_add(&o->nest); // global level uses one
	if (!ns || !(ns->sopt = dup_sopt(o, &def_sopt)) ||
	    !sauSymtab_add_stra(st, sauMath_names, SAU_MATH_NAMED,
			SAU_SYM_MATH_ID, 0) ||
	    !sauSymtab_add_stra(st, sauMath_vars_names, SAU_MATH_VARS_NAMED,
			SAU_SYM_VAR, 1 /* has ID only if > 0 */) ||
	    !sauSymtab_add_stra(st, sauLine_names, SAU_LINE_NAMED,
			SAU_SYM_LINE_ID, 0) ||
	    !sauSymtab_add_stra(st, sauWave_names, SAU_WAVE_NAMED,
			SAU_SYM_WAVE_ID, 0) ||
	    !sauSymtab_add_stra(st, sauNoise_names, SAU_NOISE_NAMED,
			SAU_SYM_NOISE_ID, 0))
		return false;
	/*
	 * Register predefined values as variable assignments.
	 */
	sauScriptPredef *predef = arg->predef;
	for (size_t i = 0, count = arg->predef_count; i < count; ++i) {
		sauSymstr *sstr = sauSymtab_get_symstr(st,
				predef[i].key, predef[i].len);
		sauSymitem *item;
		if (!sstr ||
		    !((item = sauSymtab_find_item(st, sstr, SAU_SYM_VAR)) ||
		      (item = sauSymtab_add_item(st, sstr, SAU_SYM_VAR))))
			return false;
		item->data.num = predef[i].val;
		item->data_use = SAU_SYM_DATA_NUM;
		if (item->data_id > 0)
			sauMath_vars_symbols[item->data_id - 1]
				(&o->math_state, item->data.num);
	}
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
		(SAU_IS_UPPER(c) ?
		"invalid or misplaced typename '%c'" :
		(SAU_IS_LOWER(c) ?
		"invalid or misplaced subname '%c'" :
		"misplaced or unrecognized '%c'")) :
		"invalid character (value 0x%02hhX)";
	sauScanner_warning(o, NULL, warn_str, c);
	return true;
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
 * Print warning for use of deprecated feature or alias.
 */
static void warn_deprecated(sauScanner *restrict o,
		const char *restrict old, const char *restrict new) {
	sauScanner_warning(o, NULL, "%s is deprecated, use new %s", old, new);
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_eof_without_closing(sauScanner *restrict o, uint8_t c) {
	sauScanner_warning(o, NULL, "end of file without closing '%c'", c);
}

/*
 * Print warning for something missing prior to syntactic element.
 */
static void warn_expected_before(sauScanner *restrict o,
		sauScanFrame *sf,
		const char *restrict missing, const char *restrict op_str) {
	sauScanner_warning(o, sf, "expected %s before '%s'", missing, op_str);
}

/*
 * Print warning for integer outside allowed range, with fallback value.
 */
static void warn_int_range_fallback(sauScanner *restrict o,
		sauScanFrame *sf, int32_t min, int32_t max, int32_t used,
		const char *restrict name) {
	sauScanner_warning(o, sf,
"invalid %s, using %d (valid range %d-%d)", name, used, min, max);
}

/*
 * Print warning for a subname missing in some context.
 */
static void warn_invalid_subname(sauScanner *restrict o, char c) {
	if (!c || c == SAU_SCAN_SPACE || c == SAU_SCAN_LNBRK)
		sauScanner_warning(o, NULL, "missing subname");
	else
		sauScanner_warning(o, NULL, "invalid subname '%c'", c);
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_missing_closing(sauScanner *restrict o, uint8_t c) {
	sauScanner_warning(o, NULL, "missing closing '%c'", c);
}

/*
 * Print warning for missing whitespace before character.
 */
static void warn_missing_whitespace(sauScanner *restrict o,
		sauScanFrame *sf, uint8_t next_c) {
	sauScanner_warning(o, sf, "missing whitespace before '%c'", next_c);
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

struct Symbol {
	sauSymstr *sstr;
	sauSymitem *item;
	bool var_chkset : 1;
	bool var_global : 1;
};
static struct Symbol scan_sym(sauScanner *restrict o, uint32_t type_id,
		const char *const*restrict help_stra, bool optional) {
	const char *type_label = scan_sym_typelabels[type_id];
	struct Symbol s = {0};
	if (type_id == SAU_SYM_VAR) {
		switch (sauScanner_getc(o)) {
		case '~':
			s.var_global = true; break;
		case '?':
			if (!optional) {
				s.var_chkset = true; break;
			} /* fall-through */
		default:
			sauScanner_ungetc(o); break;
		}
	}
	sauScanner_get_symstr(o, &s.sstr);
	if (!s.sstr) goto NOT_FOUND;
	if (s.var_global)
		s.item = sauSymtab_find_item_at(o->symtab, s.sstr, type_id, 0);
	else
		s.item = sauSymtab_find_item(o->symtab, s.sstr, type_id);
	if (!s.item) {
		/*
		 * Handle item not found per type. ID types are after the
		 * label type and only allow predefined names to be used.
		 *
		 * The label type allows only globals, always makes item.
		 */
		if (type_id > SAU_SYM_LABEL) goto NOT_FOUND;
		if (type_id == SAU_SYM_LABEL)
			s.item = sauSymtab_add_item(o->symtab, s.sstr, type_id);
	}
	return s;
NOT_FOUND:
	if (!s.sstr) {
		if (optional)
			return (struct Symbol){0};
		const char *msg = help_stra ?
				"%s name missing; available are:" :
				"%s name missing";
		sauScanner_warning(o, NULL, msg, type_label);
		if (help_stra) sau_print_names(help_stra, "\t", stderr);
	} else if (help_stra) /* standard warning produced here */ {
		sauScanner_warning_at(o, 0,
				"invalid %s name '%s'; available are:",
				type_label, s.sstr->key);
		sau_print_names(help_stra, "\t", stderr);
	}
	return (struct Symbol){0};
}

static bool scan_mathfunc(sauScanner *restrict o, size_t *restrict found_id) {
	sauSymitem *sym =
		scan_sym(o, SAU_SYM_MATH_ID, sauMath_names, false).item;
	if (!sym)
		return false;
	if (sauMath_params[sym->data_id] == SAU_MATH_NOARG_F // no parentheses
	    || sauScanner_tryc(o, '(')) {
		*found_id = sym->data_id;
		return true;
	}
	sauScanner_warning(o, NULL,
"expected '(' following math function name '%s'", sauMath_names[sym->data_id]);
	return false;
}

static inline bool is_numvar(sauSymitem *restrict item) {
	return item && item->data_use == SAU_SYM_DATA_NUM;
}

static sauSymitem *scan_numvar(sauScanner *restrict o) {
	struct Symbol s = scan_sym(o, SAU_SYM_VAR, NULL, true);
	if (!is_numvar(s.item) && s.sstr) {
		char *head = s.var_global ? "$~" : "$";
		sauScanner_warning(o, NULL,
"variable '%s%s' in numerical expression undefined", head, s.sstr->key);
		return NULL;
	}
	return s.item;
}

struct NumParser {
	sauScanner *sc;
	sauScanNumConst_f numconst_f;
	sauScanFrame sf_start;
	bool skip_num; // if true, only parse to verify; no side effects
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
static double
scan_num_r(struct NumParser *restrict o, uint8_t pri, uint32_t level) {
	sauScanner *sc = o->sc;
	sauParser *p = sc->data;
	uint8_t ws_level = sc->ws_level;
	double num;
	uint8_t c;
	if (level == 1 && ws_level != SAU_SCAN_WS_NONE)
		sauScanner_setws_level(sc, SAU_SCAN_WS_NONE);
	c = sauScanner_getc(sc);
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
		if (isnan(num)) o->has_nannum = true; // NaN never silent here
	} else if (c == '+' || c == '-') {
		num = scan_num_r(o, NUMEXP_ADT, level);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else if (c == '$') {
		sauSymitem *var = scan_numvar(sc);
		if (!var) goto REJECT;
		num = var->data.num;
	} else {
		size_t func_id = 0, read_len = 0;
		sauScanner_ungetc(sc);
		sauScanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0) {
			if (!SAU_IS_ALPHA(c) || !scan_mathfunc(sc, &func_id))
				goto REJECT; /* silent NaN (nothing was read) */
			switch (sauMath_params[func_id]) {
			case SAU_MATH_VAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				if (o->skip_num) break; // parse only, no call
				num = sauMath_symbols[func_id].val(num);
				break;
			case SAU_MATH_STATE_F:
				sauScanner_skipws(sc);
				if (!sauScanner_tryc(sc, ')')) {
					sauScanner_warning(sc, NULL,
"math function '%s()' takes no arguments", sauMath_names[func_id]);
					goto REJECT;
				}
				if (o->skip_num) break; // parse only, no call
				num = sauMath_symbols[func_id]
					.state(&p->math_state);
				break;
			case SAU_MATH_STATEVAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				if (o->skip_num) break; // parse only, no call
				num = sauMath_symbols[func_id]
					.stateval(&p->math_state, num);
				break;
			case SAU_MATH_NOARG_F:
				if (o->skip_num) break; // parse only, no call
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
		double rval;
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
			rval = scan_num_r(o, NUMEXP_ADT, level);
			if (isnan(rval)) goto DEFER; // allow suffix silently
			num -= rval;
			break;
		default:
			if (rpar_mlt &&
			    (c != SAU_SCAN_SPACE && c != SAU_SCAN_LNBRK)) {
				sauScanner_ungetc(sc);
				rval = scan_num_r(o, NUMEXP_MLT, level);
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
	struct NumParser np = {o, scan_numconst, o->sf, .skip_num = false};
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
static sauNoinline bool skip_num(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst) {
	struct NumParser np = {o, scan_numconst, o->sf, .skip_num = true};
	double num = scan_num_r(&np, NUMEXP_SUB, 0);
	if (np.has_nannum)
		return true;
	if (isnan(num)) /* silent NaN (ignored blank expression) */
		return false;
	return true;
}

static sauNoinline int32_t scan_int_in_range(sauScanner *restrict o,
		int32_t min, int32_t max, int32_t fallback,
		int32_t *restrict val, const char *restrict name) {
	sauScanFrame sf = o->sf;
	size_t num_len;
	int32_t num;
	sauScanner_geti(o, &num, min < 0, &num_len);
	if (num_len == 0)
		return false;
	if (num < min || num > max) {
		warn_int_range_fallback(o, &sf, min, max, fallback, name);
		num = fallback;
	}
	*val = num;
	return true;
}

/*
 * Use to define named constant functions that just map a char to a number.
 */
#define SIMPLE_NUMCONST_F(FName, XList) \
static size_t (FName)(sauScanner *restrict o, double *restrict val) { \
	sauParser *p = o->data; \
	struct NestScope *ns sauMaybeUnused = NestArr_tip(&p->nest); \
	switch (sauFile_GETC(o->f)) { \
	XList(SIMPLE_NUMCONST_F__CASE) \
	default: sauFile_DECP(o->f); return 0; \
	} \
}
#define SIMPLE_NUMCONST_F__CASE(Name, Value) \
	case Name: *val = (Value); return 1;

#define CHANMIX_XLIST(X) \
	X('C', 0.f) \
	X('L', -1.f) \
	X('R', 1.f) \
	//
SIMPLE_NUMCONST_F(scan_chanmix_const, CHANMIX_XLIST)

#define LADDERFX_XLIST(X) \
	X('C', SAU_LADDERFX_CHIP) \
	//
SIMPLE_NUMCONST_F(scan_ladderfx_const, LADDERFX_XLIST)

#define LADDERFX_PAN_XLIST(X) \
	X('C', SAU_LADDERFX_PAN_CHIP) \
	//
SIMPLE_NUMCONST_F(scan_ladderfx_pan_const, LADDERFX_PAN_XLIST)

#define LADDERFX_THR_XLIST(X) \
	X('C', SAU_LADDERFX_THR_CHIP) \
	//
SIMPLE_NUMCONST_F(scan_ladderfx_thr_const, LADDERFX_THR_XLIST)

#define OCTAVES 11
#define OCTAVE(n) ((1 << ((n)+1)) * (1.f/32)) // standard tuning at no. 4 = 1.0
#define OCTAVE_MIDI(n) ((1 << (n)) * (1.f/32)) // shifted range where 5 means 4
static double get_note_freq(const sauParseSetOptions *restrict sopt,
		int note, int notemod, int subnote) {
	static const float notes_sau_ji[3][12] = {
		{ /* SAU JI flat (7-limit simplified 5-limit flat) */
			24.f/25,   // Cf
			711.f/700,
			15.f/14,   // Df alt. 16.f/15
			159.f/140,
			6.f/5,     // Ef
			21.f/16,   // Ff alt. 125.f/96
			307.f/224,
			10.f/7,    // Gf alt. 36.f/25
			106.f/70,
			8.f/5,     // Af
			17.f/10,
			9.f/5,     // Bf alt. 16.f/9 (sym. 9/8)
		},
		{ /* SAU JI natural (5-limit natural) */
			1.f/1,     // C
			17.f/16,
			9.f/8,	   // D  alt. 10.f/9 (sym. 9/5)
			19.f/16,
			5.f/4,     // E
			4.f/3,     // F
			17.f/12,
			3.f/2,     // G
			19.f/12,
			5.f/3,     // A
			85.f/48,
			15.f/8,	   // B
		},
		{ /* SAU JI sharp (7-limit simplified 5-limit sharp) */
			25.f/24,   // Cs
			53.f/48,
			7.f/6,     // Ds alt. 75.f/64
			103.f/84,
			9.f/7,     // Es alt. 32.f/25
			7.f/5,     // Fs alt. 25.f/18
			133.f/90,
			14.f/9,    // Gs alt. 25.f/16
			119.f/72,
			7.f/4,     // As alt. 225.f/128
			307.f/168,
			40.f/21,   // Bs alt. 243.f/128, 256/135
		},
	};
	static const float notes_main[3][12] = {
		{ /* Equal temperament */
			1.f,                    // 0	C
			1.0594630943592952646f, // 1
			1.1224620483093729814f, // 2	D
			1.1892071150027210667f, // 3
			1.2599210498948731648f, // 4	E
			1.3348398541700343648f, // 5	F
			1.4142135623730950488f,	// 6
			1.4983070768766814988f, // 7	G
			1.5874010519681994748f,	// 8
			1.6817928305074290860f, // 9	A
			1.7817974362806786095f,	// 10
			1.8877486253633869932f, // 11	B
		},
		{ /* 5-limit JI a.k.a. Ptolemy's intense diatonic scale
		     (plus midpoints disregarding limit for MIDI numbers) */
			1.f/1,     // C
			17.f/16,
			9.f/8,     // D
			19.f/16,
			5.f/4,     // E
			4.f/3,     // F
			17.f/12,
			3.f/2,     // G
			19.f/12,
			5.f/3,     // A
			85.f/48,
			15.f/8,	   // B
		},
		{ /* 3-limit JI a.k.a. Pythagorean tuning
		     (plus midpoints disregarding limit for MIDI numbers) */
			1.f/1,     // C
			17.f/16,
			9.f/8,     // D
			153.f/128,
			81.f/64,   // E
			4.f/3,     // F
			17.f/12,
			3.f/2,     // G
			51.f/32,
			27.f/16,   // A
			459.f/256,
			243.f/128, // B
		},
	};
	static const float notemods_main[3][4] = {
		{ /* Equal temperament */
			1.0293022366434920288f,	// 1/2  z/d, quarter tone
			1.0594630943592952646f, // 1	s/b, semitone (sharp)
			1.0905077326652576592f,	// 3/2  k/v, 3/4 tone
			1.1224620483093729814f, // 2	x/w, tone
		},
		{ /* 5-limit JI a.k.a. Ptolemy's intense diatonic scale */
			49.f/48,      // z/d, large septimal / slendro diesis
			25.f/24,      // s/b, augmented unison (sharp)
			25.f/24 * 49.f/48, // k/v
			25.f/24 * 25.f/24, // x/w
		},
		{ /* 3-limit JI a.k.a. Pythagorean tuning */
			4235.f/4096,   // z/d, halved Pyth. chrom. semitone
			2187.f/2048,   // s/b, Pythagorean chromatic semitone
			2187.f/2048 * 4235.f/4096, // k/v
			2187.f/2048 * 2187.f/2048, // x/w
		},
	};
	const float *notes, *notemods;
	double freq = sopt->A4_freq;
	int system = sopt->key_system;
	if (system < 3) {
		notes = notes_main[system];
		notemods = notemods_main[system];
		freq /= notes[9]; // tune using A4/A
	} else { // special case for SAU JI table
		int key_table = 1;
		if (notemod >= +2) {
			key_table += 1; notemod -= 2; // table for sharp
		}
		else if (notemod <= -2) {
			key_table -= 1; notemod += 2; // table for flat
		}
		notes = notes_sau_ji[key_table];
		notemods = notemods_main[1]; // same as main 5-limit table
		freq /= notes_sau_ji[1][9]; // tune using A4/A
	}
	const int key = sopt->note_key, key_note = note7to12(MUSNOTE(key));
	if ((note -= key_note) < 0) { note += 12; freq *= 0.5f; }
	freq *= notes[note] * notes[key_note];
	if (notemod < 0)
		freq /= notemods[(-notemod) - 1]; // flatten
	else if (notemod > 0)
		freq *= notemods[(+notemod) - 1]; // sharpen
	if (subnote >= 0) {
		if ((subnote -= key_note) < 0) subnote += 12;
		double lonote = notes[note];
		note = note12to7(note);
		double hinote = (note < 6) ?
			notes[note7to12(note + 1)] :
			2*notes[0];
		freq *= 1.f + (hinote / lonote - 1.f) * (notes[subnote] - 1.f);
	}
	return freq;
}

static size_t scan_note_midinum(sauScanner *restrict o,
		const sauParseSetOptions *restrict sopt,
		double *restrict val) {
	size_t len = 0;
	int32_t note = 0;
	const int min = 0, max = 127 /* 143 */, default_note = 69;
	sauFile_geti(o->f, &note, false, &len);
	if (len == 0)
		sauScanner_warning(o, NULL,
"MIDI note number missing after 'M' (valid range %d-%d)", min, max);
	else if (note > max) {
		warn_int_range_fallback(o, NULL, min, max, default_note,
				"MIDI note number");
		note = default_note;
	}
	int notemod_num = notemod(sauFile_GETC(o->f));
	if (notemod_num != 0) ++len;
	else sauFile_DECP(o->f);
	double freq = get_note_freq(sopt, note % 12, notemod_num, -1);
	*val = freq * OCTAVE_MIDI(note / 12);
	return len;
}

static size_t scan_note_const(sauScanner *restrict o,
		double *restrict val) {
	sauParser *p = o->data;
	struct NestScope *ns = NestArr_tip(&p->nest);
	sauParseSetOptions *sopt = ns->sopt;
	sauFile *f = o->f;
	size_t len = 0, num_len;
	int c = sauFile_GETC(f); ++len;
	if (c == 'M') {
		num_len = scan_note_midinum(o, sopt, val);
		if (!num_len) {
			sauFile_UNGETN(f, len);
			return 0;
		}
		return len += num_len;
	}
	int subnote = -1;
	if (c >= 'a' && c <= 'g') {
		if ((c -= 'c') < 0) c += 7;
		subnote = note7to12(c);
		c = sauFile_GETC(f); ++len;
	}
	if (c < 'A' || c > 'G') {
		sauFile_UNGETN(f, len);
		return 0;
	}
	if ((c -= 'C') < 0) c += 7;
	const int key = sopt->note_key;
	int note = c;
	int32_t octave, default_octave = sopt->key_octave;
	int notemod_num = notemod(sauFile_GETC(f));
	if (notemod_num != 0) ++len;
	else sauFile_DECP(f);
	if (MUSKEY(note, notemod_num) < key) // wrap around below chosen key
		++default_octave;
	sauFile_geti(f, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = default_octave;
	else if (octave >= OCTAVES) {
		warn_int_range_fallback(o, NULL, 0, 10, default_octave,
				"note octave number");
		octave = default_octave;
	}
	double freq = get_note_freq(sopt,
			note7to12(note), notemod_num, subnote);
	*val = freq * OCTAVE(octave);
	return len;
}

#define CYCLEPOS_XLIST(X) \
	X('G', SAU_GLDA_1_2PI) \
	//
SIMPLE_NUMCONST_F(scan_cyclepos_const, CYCLEPOS_XLIST)

#define TIMEVAL_XLIST(X) \
	X('T', ns->sopt->def_time_ms * 0.001) \
	//
SIMPLE_NUMCONST_F(scan_timeval_const, TIMEVAL_XLIST)

static bool scan_posnum(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst, double *restrict val,
		const char *const label) {
	sauScanFrame sf = o->sf;
	double val_s;
	if (!scan_num(o, scan_numconst, &val_s))
		return false;
	if (val_s < 0.f) {
		sauScanner_warning(o, &sf,
				"discarding negative %s value", label);
		return false;
	}
	*val = val_s;
	return true;
}

static sauNoinline bool scan_time_val(sauScanner *restrict o,
		uint32_t *restrict val) {
	double val_s;
	if (!scan_posnum(o, scan_timeval_const, &val_s, "time"))
		return false;
	*val = sau_ui32rint(val_s * 1000.f);
	return true;
}

static sauNoinline bool scan_cutoff_freq(sauScanner *restrict o,
		float *restrict val) {
	double val_s;
	if (!scan_posnum(o, scan_note_const, &val_s, "cut-off frequency"))
		return false;
	*val = val_s;
	return true;
}

static bool scan_sym_id(sauScanner *restrict o,
		size_t *restrict found_id, uint32_t type_id,
		const char *const*restrict help_stra) {
	sauSymitem *sym = scan_sym(o, type_id, help_stra, true).item;
	if (sym) *found_id = sym->data_id;
	return sym;
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

// handle short syntax, one number deciding to use at most one of LPF or HPF
static bool scan_filt_shorthand(sauScanner *restrict o,
		sauFiltPar *restrict filt) {
	double val;
	if (!scan_num(o, scan_note_const, &val))
		return false;
	filt->l_v = val > 0.f ? val : 0.f;
	filt->h_v = val < 0.f ? -val : 0.f;
	filt->flags = SAU_FILTP_LPF | SAU_FILTP_HPF;
	return true;
}

/*
 * Main parser code
 */

/*
 * Finalize parser instance.
 */
static void fini_Parser(sauParser *restrict o) {
	sau_destroy_Scanner(o->sc);
	sau_destroy_Mempool(o->tmp_mp);
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
		    *tmp_mp = sau_create_Mempool(0);
	sauSymtab *st = sau_create_Symtab(mp);
	sauScanner *sc = sau_create_Scanner(st);
	*o = (sauParser){.sc = sc, .st = st, .mp = mp, .tmp_mp = tmp_mp,
		.ps = {.mp = mp}};
	if (!sc || !tmp_mp) goto ERROR;
	if (!init_syms(o, script_arg, st)) goto ERROR;
	sc->filters['#'] = scan_filter_hashcommands;
	sc->data = o;
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
	SCOPE_NEST,     // '[...]'
};

typedef void (*ParseLevel_sub_f)(sauParser *restrict o);
static void parse_in_settings(sauParser *restrict o);
static void parse_in_gen_step(sauParser *restrict o);
static void parse_in_filt_par(sauParser *restrict o);
static void parse_in_lafx_par(sauParser *restrict o);
static void parse_in_phase_par(sauParser *restrict o);
static void parse_in_par_sweep(sauParser *restrict o);
static void parse_in_par_env(sauParser *restrict o);
static void parse_in_par_env_and_sweep(sauParser *restrict o);

/* Indexing of subparameter components. */
enum {
	SUBP_NONE = SAU_RANGE_NONE,
	SOPT_PART = 1U<<8, // flag to add for sopt part
	// other kinds of values, not part of the range struct, follow...
	MAIN_FILT = SAU_RANGE_PARAMS,
	MAIN_LAFX,
};

/* Is part of a value range struct? */
static inline bool is_valr_part(unsigned parts) {
	parts &= UINT8_MAX;
	return parts > SAU_RANGE_NONE && parts < SAU_RANGE_PARAMS;
}

/* Get part of a value range struct. */
static inline sauLine *get_valr_line(sauRange *restrict r, unsigned parts) {
	switch (parts) {
	case SAU_RANGE_A: return &r->a;
	case SAU_RANGE_B: return &r->b;
	case SAU_RANGE_E: /* fall-through */
	case SAU_RANGE_E|SOPT_PART: return &r->e;
	default: return NULL;
	}
}

/* Get function for nested scope parsing of subparameter. */
static inline ParseLevel_sub_f get_subp_sub_f(unsigned parts) {
	switch (parts) {
	case SAU_RANGE_A:
	case SAU_RANGE_B: return parse_in_par_sweep;
	case SAU_RANGE_E: return parse_in_par_env_and_sweep;
	case SAU_RANGE_E|SOPT_PART: return parse_in_par_env;
	case MAIN_FILT: /* fall-through */
	case MAIN_FILT|SOPT_PART: return parse_in_filt_par;
	case MAIN_LAFX: /* fall-through */
	case MAIN_LAFX|SOPT_PART: return parse_in_lafx_par;
	default: return NULL;
	}
}

static inline bool is_valr_mod_additive(unsigned mod, unsigned valr_first) {
	unsigned valr_r = valr_first+SAU_MOD_VALR_r;
	unsigned valr_last = valr_first+SAU_MODS_VALR;
	return mod >= valr_first && mod != valr_r && mod <= valr_last;
}

/*
 * Parse level flags.
 */
enum {
	PL_NEW_EVENT_FORK = 1U<<0,
	PL_OWN_EV         = 1U<<1,
	PL_OWN_GEN        = 1U<<2,
	PL_WARN_NOSPACE   = 1U<<3,
	PL_FORBID_OBJ     = 1U<<4,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
struct ParseLevel {
	struct ParseLevel *parent;
	ParseLevel_sub_f sub_f;
	uint8_t pl_flags, scope, close_c;
	uint8_t use_type;
	sauParseEvData *event;
	sauParseGenData *gen;
	sauParseObjRef *ev_last; // TODO: is remnant of more gens per event...
	sauSymitem *set_label;
	sauParseEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t add_wait_ms; /* added for next event */
};

static sauRange *create_range(sauParser *restrict o,
		sauParseGenData *restrict gen,
		uint32_t valr_id) {
	if (!set_gen_valr(o->mp, gen))
		return NULL;
	sauRange *r;
	if (!((*gen->valr)[valr_id] = r = sau_mpalloc(o->mp, sizeof(*r))))
		return NULL;
	// the parameter ID is used as search ID in semantics code
	r->a.par_id = valr_id;
	// apply default time logic
	r->a.flags = r->b.flags = r->e.flags = SAU_LINEP_TIME_IF_NEW;
	return r;
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

static void end_gen(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_GEN))
		return;
	pl->pl_flags &= ~PL_OWN_GEN;
	pl->gen = NULL;
}

static void end_event(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_EV))
		return;
	pl->pl_flags &= ~PL_OWN_EV;
	end_gen(o);
	pl->ev_last = NULL;
	pl->event = NULL;
}

static void begin_event(sauParser *restrict o,
		sauParseGenData *restrict prev_data,
		bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseEvData *e;
	end_event(o);
	pl->event = sau_mpalloc(o->mp, sizeof(sauParseEvData));
	e = pl->event;
	e->wait_ms = pl->add_wait_ms;
	pl->add_wait_ms = 0;
	sauParseEvData *pve = NULL;
	if (prev_data != NULL) {
		struct NestScope *ns = NestArr_tip(&o->nest);
		if (prev_data->ref.is_nested)
			e->ev_flags |= SAU_PEV_IMPLICIT_TIME;
		if (is_compstep) {
			pve = prev_data->event;
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				sauParseEvBranch *fork =
					sau_mpalloc(o->tmp_mp, sizeof(*fork));
				fork->events = e;
				if (!pl->main_ev)
					pl->main_ev = pve;
				fork->prev = pl->main_ev->forks;
				pl->main_ev->forks = fork;
				pl->pl_flags &= ~PL_NEW_EVENT_FORK;
			} else {
				while (pve->next) pve = pve->next;
				pve->next = e;
			}
		} else if (ns->owner_item) {
			sauParseGenData *parent_gen = (void*)ns->owner_item;
			pve = parent_gen->event;
			while (pve->next) pve = pve->next;
			pve->next = e;
		}
	}
	if (!is_compstep) {
		if (!pve) {
			if (!o->events)
				o->events = e;
			else
				o->last_event->next = e;
			o->last_event = e;
		}
		pl->main_ev = NULL;
	}
	if (!o->group_event)
		o->group_event = (pl->main_ev != NULL) ? pl->main_ev : e;
	pl->pl_flags |= PL_OWN_EV;
}

/*
 * Create a new event for use with the next object if
 * needed, otherwise keep the current event in scope.
 */
static void prepare_event(sauParser *restrict o,
		void *restrict prev_obj, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	if (!pl->event || pl->add_wait_ms > 0 ||
			((prev_obj || !parser_block_i(o))
			 && pl->event->main_obj) ||
			is_compstep)
		begin_event(o, prev_obj, is_compstep);
}

/*
 * Add new object to parent(s), ie. either the current event node,
 * or an object ref node (either ordinary or representing multiple
 * objects) in the case of object linking/nesting.
 */
static void link_ev_obj(struct ParseLevel *restrict pl,
		struct NestScope *restrict ns,
		sauParseEvData *restrict e,
		sauParseObjRef *restrict obj,
		sauParseObjRef *restrict prev,
		bool is_copy) {
	obj->next = NULL; /* ensure NULL when new, may have been copied */
	if ((prev && !is_copy) || !ns->list) {
		if (!e->main_obj)
			e->main_obj = obj;
		else
			pl->ev_last->next = obj;
		pl->ev_last = obj;
	} else {
		if (!ns->list->first_item)
			ns->list->first_item = obj;
		else
			ns->last_item->next = obj;
		ns->last_item = obj;
	}
	/*
	 * Assign to label?
	 */
	if (pl->set_label != NULL) {
		pl->set_label->data_use = SAU_SYM_DATA_OBJ;
		pl->set_label->data.obj = obj;
		pl->set_label = NULL;
		obj->is_labeled = true;
	}
}

/*
 * Create a new list inside a NestScope. \p last_mods may point to a prior one.
 */
static void begin_list(sauParser *restrict o,
		sauParseListData *restrict plist,
		uint8_t use_type) {
	(void)plist;
	struct ParseLevel *pl = o->cur_pl, *parent_pl = pl->parent;
	struct NestScope *ns = NestArr_tip(&o->nest), *parent_ns = ns-1;
	pl->sub_f = (use_type == SAU_MOD_N_p_pm) ?
		parse_in_phase_par :
		get_subp_sub_f(parent_ns->subp_part);
	if (pl->pl_flags & PL_FORBID_OBJ) {
		static sauParseListData dummy_list = {0};
		ns->list = &dummy_list; // when only used for parsing
		return;
	}
	sauParseListData *list = sau_mpalloc(o->mp, sizeof(*ns->list));
	ns->list = list;
	list->use_type = use_type;
	sem_obj_ref_init(&list->ref, SAU_POBJT_LIST,
			0, use_type != SAU_MOD_N_carr);
	if (use_type == SAU_MOD_N_carr) {
		ns->owner_item = NULL;
		link_ev_obj(parent_pl, parent_ns, pl->event,
				&list->ref, &plist->ref, false);
	} else {
		/*
		 * Maintain linked list of modulator lists per owner (carrier).
		 */
		sauParseGenData *parent_gen = parent_pl->gen;
		if (ns->owner_item != &parent_gen->ref)
			ns->last_mods = NULL;
		ns->owner_item = &parent_gen->ref;
		if (!parent_gen->mods)
			parent_gen->mods = list;
		else {
			/*
			 * If this list is set for a heading subparameter,
			 * instead of above for a parameter for an object,
			 * then we're here with last_mods unset. Append to
			 * the list of lists one level above in this case.
			 */
			if (!ns->last_mods) ns = parent_ns;
			ns->last_mods->ref.next = list;
		}
		ns->last_mods = list;
	}
}

static void begin_gen(sauParser *restrict o,
		sauParseGenData *restrict pgen, bool is_compstep, bool is_copy,
		uint32_t type) {
	prepare_event(o, is_copy ? NULL : pgen, is_compstep);
	struct ParseLevel *pl = o->cur_pl;
	struct NestScope *ns = NestArr_tip(&o->nest);
	sauParseSetOptions *sopt = ns->sopt;
	sauParseEvData *e = pl->event;
	sauParseGenData *gen;
	end_gen(o);
	pl->gen = gen = sau_mpalloc(o->mp, sizeof(sauParseGenData));
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	/*
	 * Initialize node.
	 */
	bool is_nested = pl->use_type != SAU_MOD_N_carr;
	if (pgen != NULL) {
		pgen->ref.has_next_ref = true;
		gen->ref.prev_ref = &pgen->ref;
		// verify time flags wrt nesting for the case of cloning
		unsigned time_flags = is_nested ?
			pgen->time.flags & SAU_TIMEP_IMPLICIT :
			0;
		gen->time = sauTime_DEFAULT(pgen->time.v_ms, time_flags);
		gen->mode.main = pgen->mode.main;
		if (is_copy) {
			sem_obj_ref_init(&gen->ref, SAU_POBJT_GEN,
					pgen->ref.gen_type, is_nested);
			gen->ref.is_cloned = true;
			gen->params |= SAU_PGENP_TIME;
		} else {
			gen->ref.obj_id = pgen->ref.obj_id;
			gen->ref.obj_type = pgen->ref.obj_type;
			gen->ref.gen_type = pgen->ref.gen_type;
			gen->ref.is_nested = pgen->ref.is_nested;
		}
	} else {
		// Return to original event in mod list after any @ref
		if (is_nested)
			e = ((sauParseGenData*)ns->owner_item)->event;
		/*
		 * New generator with initial parameter values.
		 *
		 * Defaults not handled during parsing are not set here.
		 */
		gen->params = SAU_PGEN_PARAMS;
		if (sau_pgen_has_seed(type))
			gen->seed = sau_rand32(&o->math_state);
		gen->time = sauTime_DEFAULT(sopt->def_time_ms, is_nested);
		if (sau_pgen_is_osc(type)) {
			switch (type) {
			case SAU_PGEN_N_rals:
				gen->mode.ras = sopt->def_ras; break;
			case SAU_PGEN_N_wave:
				gen->mode.woo = sopt->def_woo; break;
			}
		}
		sem_obj_ref_init(&gen->ref, SAU_POBJT_GEN, type, is_nested);
	}
	link_ev_obj(pl, ns, e, &gen->ref, &pgen->ref, is_copy);
	gen->event = e;
	pl->pl_flags |= PL_OWN_GEN;
	// sopt is now used for this generator node, updates need new struct
	gen->sopt = sopt;
	ns->is_old_sopt = true;
}

static void finish_durgroup(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!o->group_event)
		return; /* nothing to do */
	o->last_event = sem_per_durgroup(&o->ps,
	                                 o->group_event, &pl->add_wait_ms);
	o->group_event = NULL;
}

static void enter_level(sauParser *restrict o,
		struct ParseLevel *restrict pl,
		uint8_t use_type, uint8_t newscope, uint8_t close_c) {
	struct ParseLevel *restrict parent_pl = o->cur_pl;
	*pl = (struct ParseLevel){
		.scope = newscope,
		.close_c = close_c,
		.use_type = use_type,
	};
	o->cur_pl = pl;
	if (!parent_pl)
		return; // nothing to copy or create
	pl->parent = parent_pl;
	pl->sub_f = parent_pl->sub_f;
	pl->pl_flags = parent_pl->pl_flags &
		(PL_NEW_EVENT_FORK | PL_FORBID_OBJ);
	if (newscope == SCOPE_SAME)
		pl->scope = parent_pl->scope;
	pl->event = parent_pl->event;
	pl->gen = parent_pl->gen;
	if (newscope == SCOPE_NEST) {
		struct NestScope *ns = NestArr_tip(&o->nest);
		/*
		 * Detect uses of nesting syntax without the usual
		 * support for objects in the nesting list. That's
		 * the case when it's used only to read subvalues.
		 */
		if (!pl->use_type && (ns-1)->subp_part)
			pl->pl_flags |= PL_FORBID_OBJ;
		begin_list(o, NULL, use_type);
		/*
		 * Copy script options, and prepare for a new context.
		 *
		 * The amplitude multiplier is reset each list, unless
		 * an amod list (where the value builds on the outer).
		 */
		ns->sopt = (ns-1)->sopt;
		ns->is_old_sopt = true;
		if (use_type != SAU_MOD_N_carr &&
		    !is_valr_mod_additive(use_type, SAU_MOD_N_a_am) &&
		    ns->sopt->def_ampmult != 1.f) {
			ns->sopt = dup_sopt(o, ns->sopt);
			ns->sopt->def_ampmult = 1.f;
			ns->is_old_sopt = false;
		}
		if (use_type != SAU_MOD_N_carr &&
		    ns->sopt->def_lafx.amp != 0.f) { // reset, clear
			if (ns->is_old_sopt) ns->sopt = dup_sopt(o, ns->sopt);
			ns->sopt->def_lafx = def_sopt.def_lafx;
			ns->is_old_sopt = false;
		}
	}
}

static void leave_level(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_gen(o);
	if (pl->set_label != NULL) {
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
		sem_end_dur_ms(&o->ps);
	} else if (pl->scope == SCOPE_GROUP) {
		end_event(o);
	} else if (pl->scope == SCOPE_NEST) {
		sauSymtab_drop_to(o->st, parser_block_i(o)-1);
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

static sauParseListData *parse_par_list(sauParser *restrict o,
		sauScanNumConst_f numconst_f,
		void *restrict gen_subp, bool ratio,
		unsigned valr_id, uint8_t use_type, unsigned subp_part);

static uint8_t parse_main_filt_par(sauParser *restrict o,
		sauFiltPar *restrict filt, uint8_t c) {
	switch (c) {
	break; case 'l':
		if (scan_cutoff_freq(o->sc, &filt->l_v))
			filt->flags |= SAU_FILTP_LPF;
	break; case 'h':
		if (scan_cutoff_freq(o->sc, &filt->h_v))
			filt->flags |= SAU_FILTP_HPF;
	break; default:
		return c;
	}
	return 0;
}

static uint8_t parse_main_filt(sauParser *restrict o,
		sauFiltPar *restrict filt, unsigned subp_part) {
	scan_filt_shorthand(o->sc, filt);
	parse_par_list(o, scan_note_const, filt, false, 0, 0, subp_part);
	uint8_t c = sauScanner_getc_after(o->sc, '.');
	if (c && !(c = parse_main_filt_par(o, filt, c)))
		warn_deprecated(o->sc, "filter .f.", ".f[]");
	return c;
}

static uint8_t parse_lafx_par(sauParser *restrict o,
		sauLafxPar *restrict lafx, uint8_t c) {
	double val;
	switch (c) {
	break; case 'p':
		if (scan_num(o->sc, scan_ladderfx_pan_const, &val)) {
			lafx->pan = val;
			lafx->flags |= SAU_LAFXP_PAN;
		}
	break; case 't':
		if (scan_posnum(o->sc, scan_ladderfx_thr_const, &val,
					"ladder effect threshold")) {
			lafx->thr = val;
			lafx->flags |= SAU_LAFXP_THR;
		}
	break; default:
		return c;
	}
	return 0;
}

static uint8_t parse_lafx(sauParser *restrict o,
		sauLafxPar *restrict lafx, unsigned subp_part) {
	double val;
	if (scan_posnum(o->sc, scan_ladderfx_const, &val,
			"ladder effect pulse amplitude")) {
		lafx->amp = val;
		lafx->flags |= SAU_LAFXP_AMP;
	}
	parse_par_list(o, scan_note_const, lafx, false, 0, 0, subp_part);
	uint8_t c = sauScanner_getc_after(o->sc, '.');
	if (c && !(c = parse_lafx_par(o, lafx, c)))
		warn_deprecated(o->sc, "ladder effect .l.", ".l[]");
	return c;
}

static bool parse_so_amp(sauParser *restrict o,
		struct NestScope *restrict ns) {
	sauParseSetOptions *sopt = ns->sopt;
	struct ParseLevel *pl = o->cur_pl;
	double val;
	int c;
	if (scan_num(o->sc, NULL, &val)) {
		// amod lists with summing inherit outer value
		if (is_valr_mod_additive(pl->use_type, SAU_MOD_N_a_am))
			val *= (ns-1)->sopt->def_ampmult;
		sopt->def_ampmult = val;
	}
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	break; case 'f':
		if (ns->list)
			return true; // only allow in global scope
		return parse_main_filt(o, &sopt->mix_filt, MAIN_FILT|SOPT_PART);
	break; case 'l':
		return parse_lafx(o, &sopt->def_lafx, MAIN_LAFX|SOPT_PART);
	break; case 'm':
		if (ns->list)
			return true; // only allow in global scope
		if (!isnan(sopt->ampmult))
			sauScanner_warning(o->sc, NULL,
"'a.m' script-wide gain mix control already set");
		if (scan_num(o->sc, NULL, &val)) {
			sopt->ampmult = val;
		}
	break; default:
		return c != 0;
	}
	return false;
}

static bool parse_so_freq(sauParser *restrict o,
		sauParseSetOptions *sopt, bool rel_freq) {
	double val;
	int c;
	if (rel_freq) {
		if (scan_num(o->sc, NULL, &val)) {
			sopt->def_relfreq = val;
		}
		return false;
	}
	if (scan_num(o->sc, scan_note_const, &val)) {
		sopt->def_freq = val;
	}
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	case 'k': {
		int32_t octave = sopt->key_octave;
		c = sauScanner_getc(o->sc);
		if (!SAU_IS_ASCIIVISIBLE(c))
			return true;
		if (c < 'A' || c > 'G') {
			if (SAU_IS_DIGIT(c)) {
				sauScanner_ungetc(o->sc);
				goto K_NUM;
			}
			sauScanner_warning(o->sc, NULL,
"invalid key; valid are 'A' through 'G',\n"
"\twith or without added 'b'/'d'/'v'/'w' (flat) or 's'/'z'/'k'/'x' (sharp)");
			break;
		}
		int sufc;
		int notemod_num = notemod((sufc = sauScanner_getc(o->sc)));
		if (!notemod_num) sauScanner_ungetc(o->sc);
		if ((c -= 'C') < 0) c += 7;
		sopt->note_key = MUSKEY(c, notemod_num);
	K_NUM:
		if (scan_int_in_range(o->sc, 0, 10, octave,
					 &octave, "mode level"))
			sopt->key_octave = octave;
		break; }
	case 'n':
		if (scan_num(o->sc, NULL, &val)) {
			if (val < 1.f) {
				sauScanner_warning(o->sc, NULL,
"ignoring A4 tuning frequency (Hz) below 1.0");
				break;
			}
			sopt->A4_freq = val;
		}
		break;
	case 's':
		switch ((c = sauScanner_get_suffc(o->sc))) {
		case 'e': sopt->key_system = 0; break;
		case 'c': sopt->key_system = 1; break;
		case 'p': sopt->key_system = 2; break;
		case 'j': sopt->key_system = 3; break;
		default:
			if (!c)
				return false;
			sauScanner_warning(o->sc, NULL,
"unknown scale; valid are:\n"
"\t'e' (24-EDO), 'p' (Pythagorean JI), 'c' (classic 5-limit), 'j' (SAU JI)");
			break;
		}
		break;
	default:
		return c != 0;
	}
	return false;
}

static bool parse_level(sauParser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c);

static uint8_t parse_par_sweep(sauScanner *restrict sc,
		sauLine *restrict line,
		struct NestScope *restrict ns, uint8_t c) {
	double val;
	size_t id;
	switch (c) {
	case 'g':
		if (scan_num(sc, ns->numconst_f, &val)) {
			line->vt = val;
			line->flags |= SAU_LINEP_GOAL;
			if (ns->num_ratio)
				line->flags |= SAU_LINEP_GOAL_RATIO;
			else
				line->flags &= ~SAU_LINEP_GOAL_RATIO;
		}
		break;
	case 'l':
		if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names))
			break;
		line->type = id;
		line->flags |= SAU_LINEP_TYPE;
		break;
	case 't':
		if (scan_time_val(sc, &line->time_ms)) {
			line->flags |= SAU_LINEP_TIME;
			line->flags &= ~SAU_LINEP_TIME_IF_NEW;
		}
		break;
	case 'v':
		scan_line_state(sc, ns->numconst_f, line, ns->num_ratio);
		break;
	default:
		return c;
	}
	return 0;
}

static void parse_env_line(sauScanner *restrict sc,
		uint8_t *restrict line_p1) {
	uint8_t c;
	size_t id;
	switch ((c = sauScanner_getc_after(sc, '.'))) {
	break; case 'l':
		if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names))
			break;
		*line_p1 = id + 1; // != 0 if set
	break; default:
		if (c) sauScanner_ungetc(sc);
	}
}

static bool parse_env_time(sauScanner *restrict sc,
		sauEnvPar *restrict env, unsigned i) {
	bool has_time = false;
	if (scan_time_val(sc, &env->time_ms[i])) {
		env->time_flags |= SAU_ENVP_TIME(i);
		has_time = true;
	}
	return has_time;
}

static bool parse_env_mode(sauScanner *restrict sc, sauEnvPar *restrict env) {
	uint8_t func = SAU_ENV_FUNCTIONS;
	uint8_t c;
	for (;;) {
		int matched = 0;
		if (!(func < SAU_ENV_FUNCTIONS) && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case '0': func = SAU_ENV_FN_OFF; break;
		case 't': func = SAU_ENV_FN_TRUNC; break;
		case 'd': func = SAU_ENV_FN_DECLICK; break;
		case 'l': func = SAU_ENV_FN_LOOP; break;
		case 's': func = SAU_ENV_FN_SHRINK; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (matched == 0)
			break;
	}
	if (func < SAU_ENV_FUNCTIONS) {
		env->mode = func;
		env->flags |= SAU_ENVP_MODE;
	}
	return false;
}

static uint8_t parse_par_env(sauScanner *restrict sc,
		sauEnvPar *restrict env, uint8_t c) {
	double val;
	uint8_t suffc;
	switch (c) {
	break; case 'a':
		parse_env_time(sc, env, SAU_ENV_TIME_A);
		parse_env_line(sc, &env->line_p1[SAU_ENV_LINE_A]);
	break; case 'd':
		parse_env_time(sc, env, SAU_ENV_TIME_D);
		parse_env_line(sc, &env->line_p1[SAU_ENV_LINE_D]);
	break; case 'e':
		parse_env_mode(sc, env);
		parse_env_line(sc, &env->line_all_p1);
	break; case 'r':
		switch ((suffc = sauScanner_get_suffc(sc))) {
		break; case 's':
			env->time_flags |= SAU_ENVP_TIME(SAU_ENV_TIME_R);
			env->flags |= SAU_ENVP_R_STRETCH;
		break; default:
			if (suffc) sauScanner_ungetc(sc);
			if (parse_env_time(sc, env, SAU_ENV_TIME_R))
				env->flags &= ~SAU_ENVP_R_STRETCH;
		}
		parse_env_line(sc, &env->line_p1[SAU_ENV_LINE_R]);
	break; case 's':
		if (scan_num(sc, NULL, &val)) {
			env->s_val = val;
			env->flags |= SAU_ENVP_S;
		}
		switch ((c = sauScanner_getc_after(sc, '.'))) {
		break; case 't':
			parse_env_time(sc, env, SAU_ENV_TIME_S);
		break; default:
			if (c) sauScanner_ungetc(sc);
		}
	break; default:
		return c;
	}
	return 0;
}

static void parse_in_par_sweep(sauParser *restrict o) {
	struct NestScope *ns = NestArr_getrev(&o->nest, 1);
	sauRange *range = ns->gen_subp;
	sauLine *line = get_valr_line(range, ns->subp_part);
	PARSE_IN__HEAD(parse_in_par_sweep, true)
		if (!c || parse_par_sweep(sc, line, ns, c)) goto DEFER;
	PARSE_IN__TAIL()
}

static void parse_in_par_env(sauParser *restrict o) {
	struct NestScope *ns = NestArr_getrev(&o->nest, 1);
	sauRange *range = ns->gen_subp;
	PARSE_IN__HEAD(parse_in_par_env, true)
		if (!c || parse_par_env(sc, &range->env, c)) goto DEFER;
	PARSE_IN__TAIL()
}

static void parse_in_par_env_and_sweep(sauParser *restrict o) {
	struct NestScope *ns = NestArr_getrev(&o->nest, 1);
	sauRange *range = ns->gen_subp;
	sauLine *line = get_valr_line(range, ns->subp_part);
	PARSE_IN__HEAD(parse_in_par_env_and_sweep, true)
		if (!c ||
		    (parse_par_sweep(sc, line, ns, c) &&
		     parse_par_env(sc, &range->env, c))) goto DEFER;
	PARSE_IN__TAIL()
}

static bool prepare_par_range(sauParser *restrict o,
		struct NestScope *restrict ns,
		sauScanNumConst_f numconst_f,
		void *restrict gen_subp, bool ratio,
		unsigned valr_id, unsigned subp_part) {
	ns->numconst_f = numconst_f;
	ns->num_ratio = ratio;
	ns->subp_part = subp_part;
	ns->gen_subp = gen_subp;
	if (!is_valr_part(subp_part)) // clear, not provided
		return true;
	sauRange *gen_valr = gen_subp;
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (gen_valr || !gen)
		return true;
	if (!gen->valr || !(gen_valr = (*gen->valr)[valr_id]))
		gen_valr = create_range(o, gen, valr_id);
	get_valr_line(gen_valr, subp_part)->flags |= SAU_LINEP; // is touched
	ns->gen_subp = gen_valr;
	return true;
}

static sauParseListData *parse_par_list(sauParser *restrict o,
		sauScanNumConst_f numconst_f,
		void *restrict gen_subp, bool ratio,
		unsigned valr_id, uint8_t use_type, unsigned subp_part) {
	struct NestScope *ns = NestArr_tip(&o->nest);
	prepare_par_range(o, ns, numconst_f,
			gen_subp, ratio, valr_id, subp_part);
	if (ns->gen_subp && is_valr_part(ns->subp_part)) {
		sauLine *line = get_valr_line(ns->gen_subp, subp_part);
		if (line) scan_line_state(o->sc, numconst_f, line, ratio);
	}
	ns = NestArr_add(&o->nest);
	bool clear = sauScanner_tryc(o->sc, '-');
	sauParseListData *first_list = NULL;
	while (sauScanner_tryc(o->sc, '[')) {
		parse_level(o, use_type, SCOPE_NEST, ']');
		ns = NestArr_tip(&o->nest); // array may have resized!
		if (clear) clear = false;
		else ns->list->append = true;
		if (!first_list) first_list = ns->list;
	}
	NestArr_pop(&o->nest); --ns;
	ns->gen_subp = NULL;
	ns->subp_part = SUBP_NONE;
	return first_list;
}

static bool parse_chanmix_pan_law(sauParser *restrict o,
		uint8_t *restrict val) {
	uint8_t pan_law, c;
	switch ((c = sauScanner_get_suffc(o->sc))) {
	break; case 'a': pan_law = SAU_PAN_ADD;
	break; case 'f': pan_law = SAU_PAN_FULL;
	break; case 'l': pan_law = SAU_PAN_LIN;
	break; default:
		if (!c)
			return false;
		sauScanner_warning(o->sc, NULL,
"unknown pan law; valid are:\n"
"\t'a' (additive, -6 dB), 'f' (full, -0 dB), 'l' (linear, -6 dB)");
		return true;
	}
	*val = pan_law;
	return false;
}

static bool parse_so_chanmix(sauParser *restrict o,
		struct NestScope *restrict ns) {
	sauParseSetOptions *sopt = ns->sopt;
	double val;
	int c;
	if (scan_num(o->sc, scan_chanmix_const, &val)) {
		ns->sopt->def_chanmix = val;
	}
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	case 'p':
		return parse_chanmix_pan_law(o, &sopt->def_pan_law);
	default:
		return c != 0;
	}
	return false;
}

static void parse_in_settings(sauParser *restrict o) {
	struct NestScope *ns = NestArr_tip(&o->nest);
	if (ns->is_old_sopt) {
		ns->sopt = dup_sopt(o, ns->sopt);
		ns->is_old_sopt = false;
	}
	PARSE_IN__HEAD(parse_in_settings, true)
		switch (c) {
		case 'a':
			if (parse_so_amp(o, ns)) goto DEFER;
			break;
		case 'c':
			if (parse_so_chanmix(o, ns)) goto DEFER;
			break;
		case 'e': {
			sauRange tmp_range = {.env = ns->sopt->def_parenv};
			parse_par_list(o, NULL, &tmp_range, false, 0,
					0, SAU_RANGE_E|SOPT_PART);
			ns = NestArr_tip(&o->nest); // array may have resized!
			if (tmp_range.e.flags & SAU_LINEP_STATE)
				ns->sopt->def_parenv_v = tmp_range.e.v0;
			ns->sopt->def_parenv = tmp_range.env;
			break; }
		case 'f':
			if (parse_so_freq(o, ns->sopt, false)) goto DEFER;
			break;
		case 'r':
			if (parse_so_freq(o, ns->sopt, true)) goto DEFER;
			break;
		case 't':
			scan_time_val(o->sc, &ns->sopt->def_time_ms);
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static void change_list_use(sauParseListData *first_list, uint8_t use_type) {
	for (sauParseListData *list = first_list; list; list = list->ref.next)
		list->use_type = use_type;
}

static uint8_t parse_par_dotdot(sauParser *restrict o,
		sauScanNumConst_f num_f,
		sauRange *restrict range, bool ratio,
		unsigned valr_id, uint8_t mod) {
	parse_par_list(o, num_f, range, ratio, valr_id,
			mod+SAU_MOD_VALR2, SAU_RANGE_B);
	uint8_t c = 0;
	const char *opt_expected =
		"'.r', '.e', '.a' or nothing after '..' and second value";
	int opt_at = 0;
	while (opt_at >= 0) {
		if (!(c = sauScanner_getc_after(o->sc, '.'))) break;
		switch (opt_at) {
		case 0:
			opt_at = 1;
			if (c == 'r') {
				parse_par_list(o, NULL, NULL, false, 0,
						mod+SAU_MOD_VALR_r, 0);
				opt_expected =
					"'.e', '.a' or nothing after '.r'";
				break;
			} /* fall-through */
		case 1:
			opt_at = 2;
			if (c == 'e') {
				parse_par_list(o, num_f, range, ratio, valr_id,
					mod+SAU_MOD_VALR_e, SAU_RANGE_E);
				opt_expected = "'.a' or nothing after '.e'";
				break;
			} /* fall-through */
		case 2:
			opt_at = 3;
			if (c == 'a') {
				parse_par_list(o, NULL, NULL, false, 0,
						mod+SAU_MOD_VALR_a, 0);
				opt_expected = "nothing after '.a'";
				break;
			} /* fall-through */
		default:
			sauScanner_warning(o->sc, NULL,
					"expected %s", opt_expected);
			break;
		}
	}
	return 0;
}

// does it all for parameters with these and no other subparameters
static uint8_t parse_par_modranges(sauParser *restrict o,
		sauScanNumConst_f num_f,
		sauRange *restrict range, bool ratio,
		unsigned valr_id, uint8_t mod) {
	uint8_t c;
	sauParseListData *first_list = parse_par_list(o, num_f, range, ratio,
			valr_id, mod, SAU_RANGE_A);
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	case '.':
		return parse_par_dotdot(o, num_f, range, ratio, valr_id, mod);
	case 'e':
		change_list_use(first_list, mod+4);
		parse_par_list(o, num_f, range, ratio, valr_id,
				mod+SAU_MOD_VALR_e, SAU_RANGE_E);
		break;
	case 'r':
		change_list_use(first_list, mod+4);
		parse_par_list(o, num_f, range, ratio, valr_id,
				mod+SAU_MOD_VALR_r, SAU_RANGE_B);
		break;
	default:
		return c;
	}
	return 0;
}

static uint8_t parse_par_pdset(sauParser *restrict o,
		uint8_t pdset_id, uint8_t mod) {
	uint8_t c;
	switch ((c = parse_par_modranges(o, NULL, NULL, false,
					pdset_id, mod))) {
	case 'f':
		parse_par_modranges(o, scan_note_const, NULL, false,
				pdset_id+1, mod+SAU_MODS_VALR);
		break;
	case 'p':
		parse_par_modranges(o, scan_cyclepos_const, NULL, false,
				pdset_id+2, mod+SAU_MODS_VALR*2);
		break;
	default:
		return c;
	}
	return 0;
}

static uint8_t parse_gen_phase(sauParser *restrict o);

static bool parse_gen_main(sauParser *restrict o, uint8_t gen_type,
	uint8_t sym_type, const char *const* restrict sym_names) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (gen->ref.gen_type != gen_type)
		return true; // reject, lacks parameter
	size_t id = 0; /* default as fallback value */
	if (sym_type != 0 && scan_sym_id(o->sc, &id, sym_type, sym_names)) {
		gen->mode.main = id;
		gen->params |= SAU_PGENP_MODE;
	}
	return false;
}

static bool parse_gen(sauParser *restrict o, uint8_t gen_type,
		uint8_t sym_type, const char *const* restrict sym_names) {
	struct ParseLevel *pl = o->cur_pl;
	if (pl->pl_flags & PL_FORBID_OBJ) {
		sauScanner_warning(o->sc, NULL,
				"modulators not supported here");
		return true;
	}
	begin_gen(o, NULL, false, false, gen_type);
	pl->sub_f = parse_in_gen_step;
	return parse_gen_main(o, gen_type, sym_type, sym_names);
}

static void parse_in_filt_par(sauParser *restrict o) {
	struct NestScope *ns = NestArr_getrev(&o->nest, 1);
	sauFiltPar *filt = ns->gen_subp;
	PARSE_IN__HEAD(parse_in_filt_par, true)
		if (!c || parse_main_filt_par(o, filt, c))
			goto DEFER;
	PARSE_IN__TAIL()
}

static void parse_in_lafx_par(sauParser *restrict o) {
	struct NestScope *ns = NestArr_getrev(&o->nest, 1);
	sauLafxPar *lafx = ns->gen_subp;
	PARSE_IN__HEAD(parse_in_lafx_par, true)
		if (!c || parse_lafx_par(o, lafx, c))
			goto DEFER;
	PARSE_IN__TAIL()
}

static uint8_t parse_gen_amp(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	uint8_t c;
	switch ((c = parse_par_modranges(o, NULL, NULL, false,
					SAU_PVALR_AMP, SAU_MOD_N_a_am))) {
	break; case 'f':
		if (!gen->main_filt)
			gen->main_filt = sau_mpalloc(o->mp, sizeof(sauFiltPar));
		return parse_main_filt(o, gen->main_filt, MAIN_FILT);
	break; case 'l':
		if (!gen->lafx)
			gen->lafx = sau_mpalloc(o->mp, sizeof(sauLafxPar));
		return parse_lafx(o, gen->lafx, MAIN_LAFX);
	break; default:
		return c;
	}
	return 0;
}

static bool parse_gen_chanmix(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (gen->ref.is_nested)
		return true; // reject, lacks parameter
	int c;
	switch ((c = parse_par_modranges(o, scan_chanmix_const, NULL, false,
					SAU_PVALR_PAN, SAU_MOD_N_c_am))) {
	case 'p': {
		sauRange *r = (*gen->valr)[SAU_PVALR_PAN];
		sauLine *line = &r->a;
		return parse_chanmix_pan_law(o, &line->user_flags); }
	default:
		return c != 0;
	}
}

/*
 * Parse frequency parameter.
 *
 * Accepted for all audio generators, whether oscillators or not.
 */
static bool parse_gen_freq(sauParser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (rel_freq && !gen->ref.is_nested)
		return true; // reject, lacks parameter
	int c;
	sauScanNumConst_f num_f = rel_freq ? NULL : scan_note_const;
	switch ((c = parse_par_modranges(o, num_f, NULL, rel_freq,
				SAU_PVALR_FREQ, SAU_MOD_N_f_fm))) {
	case 'c': {
		if (!rel_freq)
			return true; // reject, lacks parameter
		sauRange *r = (*gen->valr)[SAU_PVALR_FREQ];
		int32_t i;
		if (scan_int_in_range(o->sc, -UINT16_MAX, +UINT16_MAX, -1, &i,
					"carrier block level")) {
			sauLine *line = &r->a;
			line->user_flags = i < 0 ?
				SAU_CARR_GETREV :
				SAU_CARR_GET;
			gen->ratio_carr_level = i < 0 ? -i : i;
		}
		return false; }
	default:
		return c != 0;
	}
	return false;
}

static bool parse_gen_mode_rals(sauScanner *restrict sc,
		sauParseGenData *restrict gen) {
	uint8_t func = SAU_RAS_FUNCTIONS;
	uint8_t flags = 0;
	int32_t level = -1;
	uint8_t c;
	for (;;) {
		int matched = 0;
		if (!(func < SAU_RAS_FUNCTIONS) && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case 'u': func = SAU_RAS_F_URAND; break;
		case 'g': func = SAU_RAS_F_GAUSS; break;
		case 'b': func = SAU_RAS_F_BIN; break;
		case 't': func = SAU_RAS_F_TERN; break;
		case 'f': func = SAU_RAS_F_FIXED; break;
		case 'a': func = SAU_RAS_F_ADDREC; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (flags != SAU_RAS_O_FUNC_FLAGS && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case 'h': flags |= SAU_RAS_O_HALFSHAPE; break;
		case 'p': flags |= SAU_RAS_O_PERLIN; break;
		case 's': flags |= SAU_RAS_O_SQUARE; break;
		case 'v': flags |= SAU_RAS_O_VIOLET; break;
		case 'z': flags |= SAU_RAS_O_ZIGZAG; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (!(level >= 0) && ++matched) {
			c = sauScanner_retc(sc);
			if (SAU_IS_DIGIT(c)) scan_int_in_range(sc, 0, 9, 9,
					&level, "mode level");
			else --matched;
		}
		if (matched == 0)
			break;
	}
	if (func < SAU_RAS_FUNCTIONS) {
		gen->mode.ras.func = func;
		gen->mode.ras.flags &=
			~(SAU_RAS_O_FUNC_FLAGS | SAU_RAS_O_LEVEL_SET);
		gen->mode.ras.flags |= SAU_RAS_O_FUNC_SET;
		gen->params |= SAU_PGENP_MODE;
	}
	if (flags) {
		gen->mode.ras.flags |= flags;
		gen->params |= SAU_PGENP_MODE;
	}
	if (level >= 0) {
		gen->mode.ras.level = sau_ras_level(level);
		gen->mode.ras.flags |= SAU_RAS_O_LEVEL_SET;
		gen->params |= SAU_PGENP_MODE;
	}
	/*
	 * Subparameters under mode for 'R'.
	 */
	double val;
	switch ((c = sauScanner_getc_after(sc, '.'))) {
	case 'a':
		if (scan_num(sc, NULL, &val)) {
			gen->mode.ras.alpha = sau_weylseq_dtoui32(val);
			gen->mode.ras.flags |= SAU_RAS_O_ASUBVAL_SET;
			gen->params |= SAU_PGENP_MODE;
		}
		break;
	default:
		return c != 0;
	}
	return false;
}

static bool parse_gen_mode_wave(sauScanner *restrict sc,
		sauParseGenData *restrict gen) {
	uint8_t func = SAU_WAVE_FUNCTIONS;
	uint8_t c;
	for (;;) {
		int matched = 0;
		if (!(func < SAU_WAVE_FUNCTIONS) && ++matched)
		switch ((c = sauScanner_getc(sc))) {
		case 'n': func = SAU_WAVE_F_NAIVE; break;
		case 'a': func = SAU_WAVE_F_ADAA; break;
		default:
			sauScanner_ungetc(sc);
			--matched;
			break;
		}
		if (matched == 0)
			break;
	}
	if (func < SAU_WAVE_FUNCTIONS) {
		gen->mode.woo.func = func;
		gen->mode.woo.flags |= SAU_WAVE_O_FUNC_SET;
		gen->params |= SAU_PGENP_MODE;
	}
	return false;
}

static bool parse_gen_mode(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	sauParseGenData *gen = pl->gen;
	switch (gen->ref.gen_type) {
	case SAU_PGEN_N_rals:  return parse_gen_mode_rals(sc, gen);
	case SAU_PGEN_N_wave:   return parse_gen_mode_wave(sc, gen);
	default:                return true; // reject
	}
}

static uint8_t parse_gen_phase_pdpar(sauParser *restrict o, uint8_t c) {
	switch (c) {
	case 'a':
		return parse_par_modranges(o, NULL, NULL, false,
				SAU_PVALR_PMA, SAU_MOD_N_pa_pm);
	case 'c':
		return parse_par_pdset(o, SAU_PVALR_PD_C, SAU_MOD_N_pd_c);
	case 'd':
		return parse_par_pdset(o, SAU_PVALR_PD_D, SAU_MOD_N_pd_d);
	case 'h':
		return parse_par_pdset(o, SAU_PVALR_PD_H, SAU_MOD_N_pd_h);
	case 'x':
		return parse_par_pdset(o, SAU_PVALR_PD_X, SAU_MOD_N_pd_x);
	case 'y':
		return parse_par_pdset(o, SAU_PVALR_PD_Y, SAU_MOD_N_pd_y);
	default:
		return c;
	}
}

static void parse_in_phase_par(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_phase_par, true)
		if (!c || parse_gen_phase_pdpar(o, c)) goto DEFER;
	PARSE_IN__TAIL()
}

static uint8_t parse_gen_phase(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (!sau_pgen_is_osc(gen->ref.gen_type))
		return true; // reject, lacks parameter
	double val;
	if (scan_num(o->sc, scan_cyclepos_const, &val)) {
		gen->phase = sau_cyclepos_dtoui32(val);
		gen->params |= SAU_PGENP_PHASE;
	}
	parse_par_list(o, NULL, NULL, false, 0, SAU_MOD_N_p_pm, 0);
	uint8_t c;
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	case 'f':
		parse_par_list(o, NULL, NULL, false, 0, SAU_MOD_N_pf_pm, 0);
		break;
	default:
		if (c && !(c = parse_gen_phase_pdpar(o, c)))
			warn_deprecated(o->sc, "PD and self-PM p.", "p[]");
		return c;
	}
	return 0;
}

static bool parse_gen_seed(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (!sau_pgen_has_seed(gen->ref.gen_type))
		return true; // reject, lacks parameter
	double val;
	if (scan_num(o->sc, scan_cyclepos_const, &val)) {
		gen->seed = sau_cyclepos_dtoui32(val);
		gen->params |= SAU_PGENP_SEED;
	}
	return false;
}

static void parse_in_gen_step(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_gen_step, pl->gen)
		sauParseGenData *gen = pl->gen;
		switch (c) {
		case '/':
			if (parse_waittime(o)) {
				begin_gen(o, pl->gen, false, false, 0);
			}
			break;
		case ';':
			pl->pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			if (parse_waittime(o)) {
				begin_gen(o, pl->gen, true, false, 0);
				pl->event->ev_flags |= SAU_PEV_FROM_GAPSHIFT;
			} else {
				if ((gen->time.flags &
				     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
				    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' without number");
				begin_gen(o, pl->gen, true, false, 0);
				pl->event->ev_flags |= SAU_PEV_WAIT_PREV_DUR;
			}
			break;
		case 'a':
			if (parse_gen_amp(o)) goto DEFER;
			break;
		case 'c':
			if (parse_gen_chanmix(o)) goto DEFER;
			break;
		case 'f':
			if (parse_gen_freq(o, false)) goto DEFER;
			break;
		case 'l':
			if (parse_gen_main(o, SAU_PGEN_N_rals, SAU_SYM_LINE_ID,
						sauLine_names)) goto DEFER;
			gen->mode.ras.flags |= SAU_RAS_O_LINE_SET;
			break;
		case 'm':
			if (parse_gen_mode(o)) goto DEFER;
			break;
		case 'n':
			if (parse_gen_main(o, SAU_PGEN_N_noise, SAU_SYM_NOISE_ID,
						sauNoise_names)) goto DEFER;
			break;
		case 'p':
			if (parse_gen_phase(o)) goto DEFER;
			break;
		case 'r':
			if (parse_gen_freq(o, true)) goto DEFER;
			break;
		case 's':
			if (parse_gen_seed(o)) goto DEFER;
			break;
		case 't': {
			struct NestScope *ns = NestArr_tip(&o->nest);
			uint8_t suffc = sauScanner_get_suffc(sc);
			switch (suffc) {
			case 'i':
				if (!gen->ref.is_nested) {
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested generator");
					break;
				}
				gen->time = sauTime_VALUE(
						ns->sopt->def_time_ms, 1);
				break;
			default:
				if (suffc)
					sauScanner_ungetc(sc);
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				gen->time = sauTime_VALUE(time_ms, 0);
				break;
			}
			gen->params |= SAU_PGENP_TIME;
			break; }
		case 'w':
			if (parse_gen_main(o, SAU_PGEN_N_wave, SAU_SYM_WAVE_ID,
						sauWave_names)) goto DEFER;
			gen->mode.woo.flags |= SAU_WAVE_O_WAVE_SET;
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_set_numvar(sauParser *restrict o, struct Symbol *restrict s,
		uint16_t block_i, double num) {
	if (!s->item || s->item->block_i != block_i) {
		if (!(s->item = sauSymtab_add_item_at(o->st, s->sstr,
						SAU_SYM_VAR, block_i)))
			return false;
	}
	s->item->data_use = SAU_SYM_DATA_NUM;
	s->item->data.num = num;
	if (s->item->data_id > 0)
		sauMath_vars_symbols[s->item->data_id - 1]
			(&o->math_state, s->item->data.num);
	return true;
}

static sauScanNumConst_f parse_numvar_namespace(sauParser *restrict o) {
	uint8_t suffc, c;
	sauScanNumConst_f numconst_f = NULL;
	switch ((suffc = sauScanner_get_suffc(o->sc))) {
	break; case 'a':
		switch ((c = sauScanner_getc_after(o->sc, '.'))) {
		break; case 'l':
			numconst_f = scan_ladderfx_const;
			if (!sauScanner_tryc(o->sc, '[')) break;
			sauScanner_skipws(o->sc);
			switch ((c = sauScanner_getc(o->sc))) {
			break; case 'p': numconst_f = scan_ladderfx_pan_const;
			break; case 't': numconst_f = scan_ladderfx_thr_const;
			break; case ']': warn_invalid_subname(o->sc, 0);
			break; default: if (c) warn_invalid_subname(o->sc, c);
			}
			sauScanner_skipws(o->sc);
			if (!sauScanner_tryc(o->sc, ']'))
				warn_missing_closing(o->sc, ']');
		break; default: if (c) warn_invalid_subname(o->sc, c);
		}
	break; case 'c': numconst_f = scan_chanmix_const;
	break; case 'f': numconst_f = scan_note_const;
	break; case 'p': numconst_f = scan_cyclepos_const;
	break; case 's': numconst_f = scan_cyclepos_const;
	break; default: if (suffc) warn_invalid_subname(o->sc, suffc);
	}
	if (suffc) sauScanner_skipws(o->sc);
	return numconst_f;
}

static bool parse_numvar_rhs(sauParser *restrict o, struct Symbol *restrict s,
		const char *restrict head, bool no_override) {
	sauScanner_skipws(o->sc);
	sauScanNumConst_f numconst_f = parse_numvar_namespace(o);
	uint16_t block_i = s->var_global ? 0 : parser_block_i(o);
	if (!s->sstr || (no_override && is_numvar(s->item))) {
		if (skip_num(o->sc, numconst_f))
			return false;
	} else {
		double num;
		if (scan_num(o->sc, numconst_f, &num)) {
			parse_set_numvar(o, s, block_i, num);
			return false;
		}
	}
	if (s->sstr) sauScanner_warning(o->sc, NULL,
			"missing right-hand side value for \"%s%s%s\"",
			head, s->sstr->key,
			(!s->var_chkset && no_override) ? "?=" : "=");
	return true; // rejected expression
}

static bool parse_numvar_lhs(sauParser *restrict o) {
	bool global_scope = !parser_block_i(o);
	struct Symbol s = scan_sym(o->sc, SAU_SYM_VAR, NULL, !global_scope);
	const char *head = s.var_global ? "$~" : (s.var_chkset ? "$?" : "$");
	bool was_unset = s.var_chkset && !is_numvar(s.item);
	bool mark_fail = was_unset;
	bool no_override = s.var_chkset;
	if (s.sstr) {
		sauScanner_skipws(o->sc);
		if (sauScanner_tryc(o->sc, '?')) {
			no_override = true;
			if (s.var_chkset)
				sauScanner_warning(o->sc, NULL,
						"'%s%s' needs no '?' after",
						head, s.sstr->key);
		}
	}
	if (sauScanner_tryc(o->sc, '=')) {
		if (!parse_numvar_rhs(o, &s, head, no_override))
			mark_fail = false;
	} else if (!s.var_chkset) {
		if (s.item) sauScanner_warning(o->sc, NULL,
				"variable '%s%s' reference does nothing",
				head, s.sstr->key);
		if (no_override) sauScanner_ungetc(o->sc);
	}
	if (was_unset && s.sstr) {
		if (mark_fail) {
			o->script_fail = true;
			o->sc->s_flags |= SAU_SCAN_S_QUIET; // silence warnings
			sauScanner_notice(o->sc, NULL,
"usage: global variable '$%s' must be set to\n"
"\trun the script; the option to pass the script is \"%s=...\"",
					s.sstr->key, s.sstr->key);
		} else {
			sauScanner_notice(o->sc, NULL,
"usage: global variable '$%s' in script can\n"
"\tbe given a value as an option; using the default value\n"
"\tof %f; to change it, pass the script the option,\n"
"\t\"%s=...\"",
					s.sstr->key,
					s.item->data.num, s.sstr->key);
		}
	}
	return s.item; // skipped whitespace?
}

/*
 * Label reference (get and use object).
 */
static bool parse_getlabel(sauParser *restrict o, uint8_t c, bool is_copy) {
	struct ParseLevel *pl = o->cur_pl;
	pl->sub_f = NULL;
	struct Symbol s = scan_sym(o->sc, SAU_SYM_LABEL, NULL, false);
	if (s.item != NULL) {
		sauSymitem *label = s.item;
		if (label->data_use == SAU_SYM_DATA_OBJ) {
			sauParseGenData *gen = label->data.obj;
			if (gen->ref.obj_type == SAU_POBJT_GEN){
				begin_gen(o, gen, false, is_copy, 0);
				gen = pl->gen;
				pl->sub_f = parse_in_gen_step;
			}
			if (!is_copy) label->data.obj = gen; /* update */
		} else {
			sauScanner_warning(o->sc, NULL,
"label '%c%s' doesn't refer to any object", c, s.sstr->key);
		}
	}
	return false;
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
		case '$':
			if (parse_numvar_lhs(o))
				continue; /* no space is OK after */
			break;
		case '\'':
			/*
			 * Label assignment, part 1; set to what follows.
			 */
			if (pl.set_label != NULL) {
				sauScanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			pl.set_label =
				scan_sym(sc, SAU_SYM_LABEL, NULL, false).item;
			break;
		case '/':
			if (parser_block_i(o)) goto INVALID;
			parse_waittime(o);
			break;
		case ':':
			parse_getlabel(o, c, true);
			break;
		case '<':
			warn_opening_disallowed(sc, '<');
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
		case '=':
			warn_expected_before(sc, &sf_first, "variable", "=");
			break;
		case '>':
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@':
			parse_getlabel(o, c, false);
			break;
		case 'A':
			if (parse_gen(o, SAU_PGEN_N_amp, 0, NULL)) break;
			if ((c = parse_gen_amp(o))) goto INVALID;
			break;
		case 'N':
			if (parse_gen(o, SAU_PGEN_N_noise, SAU_SYM_NOISE_ID,
						sauNoise_names)) break;
			if ((c = parse_gen_amp(o))) goto INVALID;
			break;
		case 'R':
			if (parse_gen(o, SAU_PGEN_N_rals, SAU_SYM_LINE_ID,
						sauLine_names)) break;
			pl.gen->mode.ras.flags = SAU_RAS_O_LINE_SET;
			if ((c = parse_gen_phase(o))) goto INVALID;
			break;
		case 'S':
			end_gen(o);
			pl.sub_f = parse_in_settings;
			break;
		case 'O':
			warn_deprecated(sc, "type 'O'", "name 'W'");
			/* fall-through */
		case 'W':
			if (parse_gen(o, SAU_PGEN_N_wave, SAU_SYM_WAVE_ID,
					sauWave_names)) break;
			pl.gen->mode.woo.flags |= SAU_WAVE_O_WAVE_SET;
			if ((c = parse_gen_phase(o))) goto INVALID;
			break;
		case '[':
			if (pl.pl_flags & PL_FORBID_OBJ) {
				sauScanner_warning(o->sc, NULL,
					"free lists not supported here");
				break;
			}
			prepare_event(o, NULL, false);
			NestArr_add(&o->nest);
			parse_level(o, SAU_MOD_N_default, SCOPE_NEST, ']');
			NestArr_pop(&o->nest);
			end_gen(o);
			break;
		case ']':
			if (c == close_c) {
				if (pl.scope == SCOPE_NEST) end_gen(o);
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
			if (parser_block_i(o)) goto INVALID;
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
	name = sc->f->path;
	if (arg->print_info) sem_print_head(name);
	parse_level(o, SAU_MOD_N_carr, SCOPE_GROUP, 0);
	sauScanner_close(sc);
	if (o->script_fail) {
		sauScanner_notice(o->sc, NULL,
				"failed requirement, script will be skipped");
		return NULL;
	}
	return name;
}

/**
 * Create parse data for the given script file.
 *
 * \return instance or NULL on error preventing parse
 */
sauParse *
sau_build_Parse(const sauScriptArg *restrict arg) {
	if (!arg)
		return NULL;
	sauParser pr;
	sauParse *parse;
	if (!init_Parser(&pr, arg))
		return NULL;
	if (!(parse = sau_mpalloc(pr.mp, sizeof(*parse))) ||
	    !init_ParseSem(&pr.ps, arg, pr.mp, pr.tmp_mp)) goto DONE;
	const char *name = parse_file(&pr, arg);
	if (!name) {
		parse = NULL;
		goto DONE;
	}
	struct NestScope *ns = NestArr_tip(&pr.nest);
	parse->st = pr.st;
	parse->events = pr.events;
	parse->name = name;
	parse->sopt = *ns->sopt;
	if ((parse = fini_ParseSem(&pr.ps, parse)) != NULL)
		pr.mp = NULL; // keep with result
DONE:
	fini_Parser(&pr);
	return parse;
}

/**
 * Destroy instance.
 */
void
sau_discard_Parse(sauParse *restrict o) {
	if (!o)
		return;
	sau_destroy_Mempool(o->mp);
}
