/* SAU library: Script parser module.
 * Copyright (c) 2011-2012, 2017-2025 Joel K. Pettersson
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
#include <sau/parse.h>
#include <sau/help.h>
#include <sau/math.h>
#include <sau/arrtype.h>
#include "parser/semantics.h"

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
	X(LABEL, "label") \
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

struct ScanLookup {
	sauParseSetOptions sopt;
	struct sauMath_state math_state;
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
	.note_key = MUSKEY(0, 0),
	.key_octave = 4,
	.key_system = 0,
	.def_ras = {0},
	.def_woo = {.func = SAU_WAVE_F_ADAA, .flags = SAU_WAVE_O_FUNC_SET},
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		const sauScriptArg *restrict arg,
		sauSymtab *restrict st) {
	o->sopt = def_sopt;
	if (!sauSymtab_add_stra(st, sauMath_names, SAU_MATH_NAMED,
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
 * Print warning for something missing prior to syntactic element.
 */
static void warn_expected_before(sauScanner *restrict o,
		sauScanFrame *sf,
		const char *restrict missing, const char *restrict op_str) {
	sauScanner_warning(o, sf, "expected %s before '%s'", missing, op_str);
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
 * Print warning for integer outside allowed range, with fallback value.
 */
static void warn_int_range_fallback(sauScanner *restrict o,
		sauScanFrame *sf, int32_t min, int32_t max, int32_t used,
		const char *restrict name) {
	sauScanner_warning(o, sf,
"invalid %s, using %d (valid range %d-%d)", name, used, min, max);
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
	const char *type_label = scan_sym_typelabels[type_id];
	sauSymstr *s = NULL;
	sauScanner_get_symstr(o, &s);
	if (!s) goto NOT_FOUND;
	sauSymitem *item = sauSymtab_find_item(o->symtab, s, type_id);
	if (!item) {
		if (type_id > SAU_SYM_LABEL) goto NOT_FOUND;
		item = sauSymtab_add_item(o->symtab, s, type_id);
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
		sauScanner_warning_at(o, 0,
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
	if (sauMath_params[sym->data_id] == SAU_MATH_NOARG_F // no parentheses
	    || sauScanner_tryc(o, '(')) {
		*found_id = sym->data_id;
		return true;
	}
	sauScanner_warning(o, NULL,
"expected '(' following math function name '%s'", sauMath_names[sym->data_id]);
	return false;
}

static sauSymitem *scan_numvar(sauScanner *restrict o) {
	sauSymitem *var = scan_sym(o, SAU_SYM_VAR, NULL, false);
	if (!var)
		return NULL;
	if (var->data_use != SAU_SYM_DATA_NUM) {
		sauScanner_warning(o, NULL,
"variable '$%s' in numerical expression doesn't hold a number",
				var->sstr->key);
		return NULL;
	}
	return var;
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
					.state(&sl->math_state);
				break;
			case SAU_MATH_STATEVAL_F:
				num = scan_num_r(o, NUMEXP_SUB, level+1);
				if (o->skip_num) break; // parse only, no call
				num = sauMath_symbols[func_id]
					.stateval(&sl->math_state, num);
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

static sauNoinline int32_t scan_int_in_range(sauScanner *restrict o,
		int32_t min, int32_t max, int32_t fallback,
		int32_t *restrict val, const char *restrict name) {
	sauScanFrame sf = o->sf;
	size_t num_len;
	int32_t num;
	sauScanner_geti(o, &num, false, &num_len);
	if (num_len == 0)
		return false;
	if (num < min || num > max) {
		warn_int_range_fallback(o, &sf, min, max, fallback, name);
		num = fallback;
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
#define OCTAVE(n) ((1 << ((n)+1)) * (1.f/32)) // standard tuning at no. 4 = 1.0
#define OCTAVE_MIDI(n) ((1 << (n)) * (1.f/32)) // shifted range where 5 means 4
static double get_note_freq(struct ScanLookup *restrict sl,
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
	double freq = sl->sopt.A4_freq;
	int system = sl->sopt.key_system;
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
	const int key = sl->sopt.note_key, key_note = note7to12(MUSNOTE(key));
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
		double *restrict val) {
	struct ScanLookup *sl = o->data;
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
	double freq = get_note_freq(sl, note % 12, notemod_num, -1);
	*val = freq * OCTAVE_MIDI(note / 12);
	return len;
}

static size_t scan_note_const(sauScanner *restrict o,
		double *restrict val) {
	sauFile *f = o->f;
	size_t len = 0, num_len;
	int c = sauFile_GETC(f); ++len;
	if (c == 'M') {
		num_len = scan_note_midinum(o, val);
		if (!num_len) {
			sauFile_UNGETN(f, len);
			return 0;
		}
		return len += num_len;
	}
	struct ScanLookup *sl = o->data;
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
	const int key = sl->sopt.note_key;
	int note = c;
	int32_t octave, default_octave = sl->sopt.key_octave;
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
	double freq = get_note_freq(sl, note7to12(note), notemod_num, subnote);
	*val = freq * OCTAVE(octave);
	return len;
}

static size_t scan_cyclepos_const(sauScanner *restrict o,
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
	*found_id = sym->data_id;
	return true;
}

static bool scan_line_state(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLinePar *restrict line, bool ratio) {
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
	sauParseListData *list, *last_mods;
	sauParseObjRef *last_item, *owner_item;
	sauParseSetOptions sopt_save; /* save/restore on nesting */
	/* values passed for outer parameter */
	sauRange *gen_valr;
	sauScanNumConst_f numconst_f;
	bool num_ratio : 1;
	uint8_t valr_parts;
};

sauArrType(NestArr, struct NestScope, )

typedef struct sauParser {
	struct ScanLookup sl;
	sauScanner *sc;
	sauSymtab *st;
	sauMempool *mp, *tmp_mp;
	NestArr nest;
	/* node state */
	struct ParseLevel *cur_pl;
	sauParseEvData *events, *last_event, *group_event;
	bool script_fail;
	uint32_t root_gen_obj;
	ParseSem ps;
} sauParser;

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
static void parse_in_gen_step(sauParser *restrict o);
static void parse_in_phase_par(sauParser *restrict o);
static void parse_in_par_sweep(sauParser *restrict o);
static void parse_in_par_env_and_sweep(sauParser *restrict o);

/* Indexing of parts of a value range struct. */
enum {
	RANGE_A = 0,
	RANGE_B,
	RANGE_E,
};

static inline sauLinePar *get_valr_line(sauRange *restrict r, unsigned parts) {
	switch (parts) {
	case RANGE_A: return &r->a;
	case RANGE_B: return &r->b;
	case RANGE_E: return &r->e;
	default: return NULL;
	}
}

static inline ParseLevel_sub_f get_valr_sub_f(unsigned parts) {
	switch (parts) {
	case RANGE_A:
	case RANGE_B: return parse_in_par_sweep;
	case RANGE_E: return parse_in_par_env_and_sweep;
	default: return NULL;
	}
}

static inline bool is_valr_mod_additive(unsigned mod, unsigned valr_first) {
	unsigned valr_r = valr_first+3;
	unsigned valr_last = valr_first+SAU_MODS_VALR;
	return mod >= valr_first && mod != valr_r && mod <= valr_last;
}

/*
 * Parse level flags.
 */
enum {
	PL_NEW_EVENT_FORK = 1<<0,
	PL_OWN_EV         = 1<<1,
	PL_OWN_GEN        = 1<<2,
	PL_WARN_NOSPACE   = 1<<3,
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
	sauParseObjRef *ev_last;
	sauSymitem *set_label;
	/* timing/delay */
	sauParseEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t add_wait_ms, carry_wait_ms; /* added for next event */
	float used_ampmult; /* update on node creation */
};

static void init_range(sauParser *restrict o, sauRange *restrict r) {
	// default implicit time value is flexible
	r->a.time_ms = r->b.time_ms = r->e.time_ms =
		o->sl.sopt.def_time_ms;
	r->a.flags = r->b.flags = r->e.flags =
		SAU_LINEP_TIME | SAU_LINEP_TIME_IF_NEW;
}

static sauRange *create_range(sauParser *restrict o,
		bool mult, uint32_t par_flag) {
	struct ScanLookup *sl = &o->sl;
	sauRange *r = sau_mpalloc(o->mp, sizeof(*r));
	if (!r)
		return NULL;
	init_range(o, r);
	float a;
	switch (par_flag) {
	case SAU_PSWEEP_PAN:
		a = sl->sopt.def_chanmix;
		break;
	case SAU_PSWEEP_AMP:
		a = 1.0f; // multiplied by sl->sopt.def_ampmult separately
		break;
	case SAU_PSWEEP_FREQ:
		a = mult ? sl->sopt.def_relfreq : sl->sopt.def_freq;
		break;
	default:
		return r;
	}
	r->a.v0 = a;
	r->a.flags |= SAU_LINEP_STATE;
	if (mult)
		r->a.flags |= SAU_LINEP_STATE_RATIO;
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
	sauParseGenData *gen = pl->gen;
	if (gen->amp) {
		gen->amp->a.v0 *= pl->used_ampmult;
		gen->amp->a.vt *= pl->used_ampmult;
		gen->amp->b.v0 *= pl->used_ampmult;
		gen->amp->b.vt *= pl->used_ampmult;
		gen->amp->e.v0 *= pl->used_ampmult;
		gen->amp->e.vt *= pl->used_ampmult;
	}
	sauParseGenData *pgen = gen->prev_ref;
	if (!pgen) {
		/*
		 * Reset all generator state for initial event.
		 */
		gen->params = SAU_PGEN_PARAMS;
	}
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
	e->wait_ms = pl->add_wait_ms + pl->carry_wait_ms;
	pl->add_wait_ms = pl->carry_wait_ms = 0;
	if (prev_data != NULL) {
		sauParseEvData *pve = prev_data->event;
		if (prev_data->is_nested)
			e->ev_flags |= SAU_PEV_IMPLICIT_TIME;
		if (is_compstep) {
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

/*
 * Create a new event for use with the next object if
 * needed, otherwise keep the current event in scope.
 */
static void prepare_event(sauParser *restrict o,
		void *restrict prev_obj, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	if (!pl->event || pl->add_wait_ms > 0 ||
			((prev_obj || !NestArr_tip(&o->nest))
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
		struct NestScope *restrict nest,
		sauParseObjRef *restrict obj,
		sauParseObjRef *restrict prev) {
	sauParseEvData *e = pl->event;
	obj->next = NULL; /* ensure NULL when new, may have been copied */
	if (prev || !nest) {
		if (!e->main_obj)
			e->main_obj = obj;
		else
			pl->ev_last->next = obj;
		pl->ev_last = obj;
	} else {
		if (!nest->list->first_item)
			nest->list->first_item = obj;
		else
			nest->last_item->next = obj;
		nest->last_item = obj;
	}
	/*
	 * Assign to label?
	 */
	if (pl->set_label != NULL) {
		pl->set_label->data_use = SAU_SYM_DATA_OBJ;
		pl->set_label->data.obj = obj;
		pl->set_label = NULL;
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
	struct NestScope *nest = NestArr_tip(&o->nest);
	sauParseListData *list = sau_mpalloc(o->mp, sizeof(*nest->list));
	nest->list = list;
	if (use_type == SAU_MOD_N_p_pm)
		pl->sub_f = parse_in_phase_par;
	else
		pl->sub_f = nest->gen_valr ?
			get_valr_sub_f(nest->valr_parts) :
			NULL;
	list->use_type = use_type;
	struct NestScope *parent_nest = NestArr_getrev(&o->nest, 1);
	if (use_type == SAU_MOD_N_carr) {
		//if (plist != NULL) {
		//	list->ref.prev = plist;
		//} else {
			sem_objinfo_add(&o->ps, &list->ref, SAU_POBJT_LIST, 0);
		//}
		link_ev_obj(parent_pl, parent_nest, &list->ref, &plist->ref);
	} else {
		/*
		 * Maintain linked list of modulator lists per owner (carrier).
		 */
		list->ref.obj_id = SAU_POBJ_NO_ID; // only used as linked list
		sauParseGenData *parent_on = parent_pl->gen;
		if (nest->owner_item != &parent_on->ref)
			nest->last_mods = NULL;
		nest->owner_item = &parent_on->ref;
		if (!parent_on->mods)
			parent_on->mods = list;
		else {
			/*
			 * If this list is set for a heading subparameter,
			 * instead of above for a parameter for an object,
			 * then we're here with last_mods unset. Append to
			 * the list of lists one level above in this case.
			 */
			if (!nest->last_mods) nest = parent_nest;
			nest->last_mods->ref.next = list;
		}
		nest->last_mods = list;
	}
}

static void begin_gen(sauParser *restrict o,
		sauParseGenData *restrict pgen, bool is_compstep,
		uint32_t type) {
	prepare_event(o, pgen, is_compstep);
	struct ParseLevel *pl = o->cur_pl;
	struct NestScope *nest = NestArr_tip(&o->nest);
	sauParseEvData *e = pl->event;
	sauParseGenData *gen;
	end_gen(o);
	pl->gen = gen = sau_mpalloc(o->mp, sizeof(sauParseGenData));
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	pl->used_ampmult = o->sl.sopt.def_ampmult;
	/*
	 * Initialize node.
	 */
	if (pgen != NULL) {
		gen->ref = pgen->ref;
		gen->prev_ref = pgen;
		gen->is_nested = pgen->is_nested;
		gen->time = sauTime_DEFAULT(pgen->time.v_ms,
				pgen->time.flags & SAU_TIMEP_IMPLICIT);
		gen->mode.main = pgen->mode.main;
	} else {
		/*
		 * New generator with initial parameter values.
		 *
		 * Defaults not handled during parsing are not set here.
		 */
		bool is_nested = pl->use_type != SAU_MOD_N_carr;
		gen->is_nested = is_nested;
		sauParseObjInfo *info = sem_objinfo_add(&o->ps, &gen->ref,
				SAU_POBJT_GEN, type);
		if (sau_pgen_has_seed(type))
			gen->seed = sau_rand32(&o->sl.math_state);
		gen->time = sauTime_DEFAULT(o->sl.sopt.def_time_ms, is_nested);
		if (!is_nested) {
			o->root_gen_obj = gen->ref.obj_id;
			if (o->sl.sopt.def_chanmix != 0.f)
				gen->pan = create_range(o,
						false, SAU_PSWEEP_PAN);
		}
		info->root_gen_obj = o->root_gen_obj;
		if (pl->used_ampmult != 1.f)
			gen->amp = create_range(o, false, SAU_PSWEEP_AMP);
		if (sau_pgen_is_osc(type)) {
			switch (type) {
			case SAU_PGEN_N_raseg:
				gen->mode.ras = o->sl.sopt.def_ras; break;
			case SAU_PGEN_N_wave:
				gen->mode.woo = o->sl.sopt.def_woo; break;
			}
		}
		/*
		 * All audio generators have frequency, not only oscillators.
		 */
		if (is_nested || o->sl.sopt.def_freq != SAU_PDEF_FREQ)
			gen->freq = create_range(o, is_nested, SAU_PSWEEP_FREQ);
	}
	link_ev_obj(pl, nest, &gen->ref, &pgen->ref);
	gen->event = e;
	pl->pl_flags |= PL_OWN_GEN;
}

static void finish_durgroup(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	pl->add_wait_ms = 0; /* reset by each '|' boundary */
	if (!o->group_event)
		return; /* nothing to do */
	o->last_event = sem_per_durgroup(&o->ps,
	                                 o->group_event, &pl->carry_wait_ms);
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
		pl->gen = parent_pl->gen;
		if (newscope == SCOPE_BIND) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = sau_mpalloc(o->mp, sizeof(*nest->list));
			pl->sub_f = NULL;
		} else if (newscope == SCOPE_NEST) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			begin_list(o, NULL, use_type);
			/*
			 * Push script options, and prepare for a new context.
			 *
			 * The amplitude multiplier is reset each list, unless
			 * an amod list (where the value builds on the outer).
			 */
			nest->sopt_save = o->sl.sopt;
			if (use_type != SAU_MOD_N_carr &&
			    !is_valr_mod_additive(use_type, SAU_MOD_N_a_am))
				o->sl.sopt.def_ampmult = def_sopt.def_ampmult;
		}
	}
	pl->use_type = use_type;
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

static bool parse_so_amp(sauParser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	double val;
	int c;
	if (scan_num(sc, NULL, &val)) {
		// amod lists with summing inherit outer value
		if (is_valr_mod_additive(pl->use_type, SAU_MOD_N_a_am))
			val *= nest->sopt_save.def_ampmult;
		o->sl.sopt.def_ampmult = val;
	}
	switch ((c = sauScanner_getc_after(sc, '.'))) {
	case 'm':
		if (nest)
			return true; // only allow in global scope
		if (!isnan(o->sl.sopt.ampmult))
			sauScanner_warning(sc, NULL,
"'a.m' script-wide gain mix control already set");
		if (scan_num(sc, NULL, &val)) {
			o->sl.sopt.ampmult = val;
		}
		break;
	default:
		return c != 0;
	}
	return false;
}

static bool parse_so_freq(sauParser *restrict o, bool rel_freq) {
	sauScanner *sc = o->sc;
	double val;
	int c;
	if (rel_freq) {
		if (scan_num(sc, NULL, &val)) {
			o->sl.sopt.def_relfreq = val;
		}
		return false;
	}
	if (scan_num(sc, scan_note_const, &val)) {
		o->sl.sopt.def_freq = val;
	}
	switch ((c = sauScanner_getc_after(sc, '.'))) {
	case 'k': {
		int32_t octave = o->sl.sopt.key_octave;
		c = sauScanner_getc(sc);
		if (!SAU_IS_ASCIIVISIBLE(c))
			return true;
		if (c < 'A' || c > 'G') {
			if (SAU_IS_DIGIT(c)) {
				sauScanner_ungetc(sc);
				goto K_NUM;
			}
			sauScanner_warning(sc, NULL,
"invalid key; valid are 'A' through 'G',\n"
"\twith or without added 'b'/'d'/'v'/'w' (flat) or 's'/'z'/'k'/'x' (sharp)");
			break;
		}
		int sufc, notemod_num = notemod((sufc = sauScanner_getc(sc)));
		if (!notemod_num) sauScanner_ungetc(sc);
		if ((c -= 'C') < 0) c += 7;
		o->sl.sopt.note_key = MUSKEY(c, notemod_num);
	K_NUM:
		if (scan_int_in_range(sc, 0, 10, octave,
					 &octave, "mode level"))
			o->sl.sopt.key_octave = octave;
		break; }
	case 'n':
		if (scan_num(sc, NULL, &val)) {
			if (val < 1.f) {
				sauScanner_warning(sc, NULL,
"ignoring A4 tuning frequency (Hz) below 1.0");
				break;
			}
			o->sl.sopt.A4_freq = val;
		}
		break;
	case 's':
		switch ((c = sauScanner_get_suffc(sc))) {
		case 'e': o->sl.sopt.key_system = 0; break;
		case 'c': o->sl.sopt.key_system = 1; break;
		case 'p': o->sl.sopt.key_system = 2; break;
		case 'j': o->sl.sopt.key_system = 3; break;
		default:
			if (!c)
				return false;
			sauScanner_warning(sc, NULL,
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

static void parse_in_settings(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_settings, true)
		double val;
		switch (c) {
		case 'a':
			if (parse_so_amp(o)) goto DEFER;
			break;
		case 'c':
			if (scan_num(sc, scan_chanmix_const, &val)) {
				o->sl.sopt.def_chanmix = val;
			}
			break;
		case 'f':
			if (parse_so_freq(o, false)) goto DEFER;
			break;
		case 'r':
			if (parse_so_freq(o, true)) goto DEFER;
			break;
		case 't':
			if (scan_time_val(sc, &o->sl.sopt.def_time_ms))
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_level(sauParser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c);

static uint8_t parse_par_sweep(sauScanner *restrict sc,
		sauLinePar *restrict line,
		struct NestScope *restrict nest, uint8_t c) {
	double val;
	size_t id;
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
	case 'l':
		if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names))
			break;
		line->type = id;
		line->flags |= SAU_LINEP_TYPE;
		break;
	case 't':
		if (scan_time_val(sc, &line->time_ms))
			line->flags &= ~SAU_LINEP_TIME_IF_NEW;
		break;
	case 'v':
		scan_line_state(sc, nest->numconst_f, line, nest->num_ratio);
		break;
	default:
		return c;
	}
	return 0;
}

static void parse_env_line(sauScanner *restrict sc,
		sauEnvPar *restrict env, unsigned i) {
	uint8_t c;
	size_t id;
	switch ((c = sauScanner_getc_after(sc, '.'))) {
	case 'l':
		if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names))
			break;
		env->line_p1[i] = id + 1; // != 0 if set
		break;
	default:
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
	parse_env_line(sc, env, i);
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
		case 'c': func = SAU_ENV_FN_CLAMP; break;
		case 'l': func = SAU_ENV_FN_LOOP; break;
		case 't': func = SAU_ENV_FN_TRUNC; break;
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
	size_t id;
	uint8_t suffc;
	switch (c) {
	case 'a':
		parse_env_time(sc, env, SAU_ENV_TIME_A);
		break;
	case 'd':
		parse_env_time(sc, env, SAU_ENV_TIME_D);
		break;
	case 'e':
		parse_env_mode(sc, env);
		switch ((c = sauScanner_getc_after(sc, '.'))) {
		case 'l':
			if (!scan_sym_id(sc, &id, SAU_SYM_LINE_ID,
			                 sauLine_names)) break;
			env->line_all_p1 = id + 1; // != 0 if set
			break;
		default:
			if (c) sauScanner_ungetc(sc);
		}
		break;
	case 'r':
		switch ((suffc = sauScanner_get_suffc(sc))) {
		case 's':
			env->time_flags |= SAU_ENVP_TIME(SAU_ENV_TIME_R);
			env->flags |= SAU_ENVP_R_STRETCH;
			parse_env_line(sc, env, SAU_ENV_TIME_R);
			break;
		default:
			if (suffc) sauScanner_ungetc(sc);
			if (parse_env_time(sc, env, SAU_ENV_TIME_R))
				env->flags &= ~SAU_ENVP_R_STRETCH;
			break;
		}
		break;
	case 's':
		if (scan_num(sc, NULL, &val)) {
			env->s_val = val;
			env->flags |= SAU_ENVP_S;
		}
		break;
	default:
		return c;
	}
	return 0;
}

static void parse_in_par_sweep(sauParser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	sauRange *range = nest->gen_valr;
	sauLinePar *line = get_valr_line(nest->gen_valr, nest->valr_parts);
	PARSE_IN__HEAD(parse_in_par_sweep, range)
		if (!c || parse_par_sweep(sc, line, nest, c)) goto DEFER;
	PARSE_IN__TAIL()
}

static void parse_in_par_env_and_sweep(sauParser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	sauRange *range = nest->gen_valr;
	sauLinePar *line = get_valr_line(nest->gen_valr, nest->valr_parts);
	PARSE_IN__HEAD(parse_in_par_env_and_sweep, range)
		if (!c ||
		    (parse_par_sweep(sc, line, nest, c) &&
		     parse_par_env(sc, &range->env, c))) goto DEFER;
	PARSE_IN__TAIL()
}

static bool prepare_sweep(sauParser *restrict o,
		struct NestScope *restrict nest,
		sauScanNumConst_f numconst_f,
		sauRange **restrict gen_valr, bool ratio,
		uint8_t valr_id, unsigned valr_parts) {
	if (!gen_valr) { /* clear when not provided */
		nest->gen_valr = NULL;
		return true;
	}
	if (!*gen_valr) { /* create for updating, unparsed values kept unset */
		*gen_valr = create_range(o, ratio, valr_id);
		(*gen_valr)->a.flags &= ~SAU_LINEP_STATE;
		(*gen_valr)->b.flags &= ~SAU_LINEP_STATE;
		(*gen_valr)->e.flags &= ~SAU_LINEP_STATE;
	}
	nest->gen_valr = *gen_valr;
	nest->numconst_f = numconst_f;
	nest->num_ratio = ratio;
	nest->valr_parts = valr_parts;
	return true;
}

static sauParseListData *parse_par_list(sauParser *restrict o,
		sauScanNumConst_f numconst_f,
		sauRange **restrict gen_valr, bool ratio,
		uint8_t valr_id, uint8_t use_type, unsigned valr_parts) {
	struct NestScope *nest = NestArr_add(&o->nest);
	prepare_sweep(o, nest, numconst_f,
			gen_valr, ratio, valr_id, valr_parts);
	if (gen_valr) {
		sauLinePar *line = get_valr_line(*gen_valr, valr_parts);
		if (line) scan_line_state(o->sc, numconst_f, line, ratio);
	}
	bool clear = sauScanner_tryc(o->sc, '-');
	sauParseListData *first_list = NULL;
	while (sauScanner_tryc(o->sc, '[')) {
		parse_level(o, use_type, SCOPE_NEST, ']');
		nest = NestArr_tip(&o->nest); // re-get, array may have changed
		if (clear) clear = false;
		else nest->list->append = true;
		if (!first_list) first_list = nest->list;
	}
	nest->gen_valr = NULL;
	NestArr_pop(&o->nest);
	return first_list;
}

static void change_list_use(sauParseListData *first_list, uint8_t use_type) {
	for (sauParseListData *list = first_list; list; list = list->ref.next)
		list->use_type = use_type;
}

static uint8_t parse_par_dotdot(sauParser *restrict o,
		sauScanNumConst_f num_f,
		sauRange **restrict range, bool ratio,
		uint8_t valr_id, uint8_t mod) {
	const uint8_t mod2 = mod+1, mod_r = mod+2, mod_e = mod+3, mod_a = mod+4;
	parse_par_list(o, num_f, range, ratio, valr_id, mod2, RANGE_B);
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
						mod_r, 0);
				opt_expected =
					"'.e', '.a' or nothing after '.r'";
				break;
			} /* fall-through */
		case 1:
			opt_at = 2;
			if (c == 'e') {
				parse_par_list(o, num_f, range, ratio, valr_id,
						mod_e, RANGE_E);
				opt_expected = "'.a' or nothing after '.e'";
				break;
			} /* fall-through */
		case 2:
			opt_at = 3;
			if (c == 'a') {
				parse_par_list(o, NULL, NULL, false, 0,
						mod_a, 0);
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
		sauRange **restrict range, bool ratio,
		uint8_t valr_id, uint8_t mod) {
	uint8_t c;
	sauParseListData *first_list =
		parse_par_list(o, num_f, range, ratio, valr_id, mod, RANGE_A);
	switch ((c = sauScanner_getc_after(o->sc, '.'))) {
	case '.':
		return parse_par_dotdot(o, num_f, range, ratio, valr_id, mod);
	case 'e':
		change_list_use(first_list, mod+4);
		parse_par_list(o, num_f, range, ratio, valr_id, mod+3, RANGE_E);
		break;
	case 'r':
		change_list_use(first_list, mod+4);
		parse_par_list(o, num_f, range, ratio, valr_id, mod+2, RANGE_B);
		break;
	default:
		return c;
	}
	return 0;
}
static uint8_t parse_par_pdset(sauParser *restrict o,
		sauPDSet **restrict pdset,
		uint8_t pdset_id, uint8_t mod) {
	if (!*pdset) {
		*pdset = sau_mpalloc(o->mp, sizeof(sauPDSet) * SAU_PPD_TYPES);
		for (uint32_t i = 0; i < SAU_PPD_TYPES; ++i) {
			init_range(o, &(*pdset)[i].v);
			init_range(o, &(*pdset)[i].f);
			init_range(o, &(*pdset)[i].p);
		}
	}
	sauPDSet *p = &(*pdset)[pdset_id];
	sauRange *range_v = &p->v, *range_f = &p->f, *range_p = &p->p;
	uint8_t c;
	switch ((c = parse_par_modranges(o, NULL, &range_v, false, 0, mod))) {
	case 'f':
		parse_par_modranges(o, scan_note_const, &range_f,
				false, 0, mod+SAU_MODS_VALR);
		break;
	case 'p':
		parse_par_modranges(o, scan_cyclepos_const, &range_p,
				false, 0, mod+SAU_MODS_VALR*2);
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
	struct NestScope *nest = NestArr_tip(&o->nest);
	if (!pl->use_type && nest && nest->gen_valr) {
		sauScanner_warning(o->sc, NULL,
				"modulators not supported here");
		return true;
	}
	begin_gen(o, NULL, false, gen_type);
	pl->sub_f = parse_in_gen_step;
	return parse_gen_main(o, gen_type, sym_type, sym_names);
}

static uint8_t parse_gen_amp(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	return parse_par_modranges(o, NULL, &gen->amp, false,
			SAU_PSWEEP_AMP, SAU_MOD_N_a_am);
}

static bool parse_gen_chanmix(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (gen->is_nested)
		return true; // reject, lacks parameter
	return parse_par_modranges(o, scan_chanmix_const, &gen->pan, false,
			SAU_PSWEEP_PAN, SAU_MOD_N_c_am);
}

/*
 * Parse frequency parameter.
 *
 * Accepted for all audio generators, whether oscillators or not.
 */
static bool parse_gen_freq(sauParser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	sauParseGenData *gen = pl->gen;
	if (rel_freq && !gen->is_nested)
		return true; // reject, lacks parameter
	sauScanNumConst_f num_f = rel_freq ? NULL : scan_note_const;
	return parse_par_modranges(o, num_f, &gen->freq, rel_freq,
			SAU_PSWEEP_FREQ, SAU_MOD_N_f_fm);
}

static bool parse_gen_mode_raseg(sauScanner *restrict sc,
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
	case SAU_PGEN_N_raseg:  return parse_gen_mode_raseg(sc, gen);
	case SAU_PGEN_N_wave:   return parse_gen_mode_wave(sc, gen);
	default:                return true; // reject
	}
}

static uint8_t parse_gen_phase_pdpar(sauParser *restrict o,
		sauParseGenData *restrict gen, uint8_t c) {
	switch (c) {
	case 'a':
		return parse_par_modranges(o, NULL, &gen->pm_a, false,
				SAU_PSWEEP_PMA, SAU_MOD_N_pa_pm);
	case 'c':
		return parse_par_pdset(o, &gen->pd, SAU_PPD_C, SAU_MOD_N_pd_c);
	case 'd':
		return parse_par_pdset(o, &gen->pd, SAU_PPD_D, SAU_MOD_N_pd_d);
	case 'h':
		return parse_par_pdset(o, &gen->pd, SAU_PPD_H, SAU_MOD_N_pd_h);
	case 'x':
		return parse_par_pdset(o, &gen->pd, SAU_PPD_X, SAU_MOD_N_pd_x);
	case 'y':
		return parse_par_pdset(o, &gen->pd, SAU_PPD_Y, SAU_MOD_N_pd_y);
	default:
		return c;
	}
}

static void parse_in_phase_par(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_phase_par, pl->gen)
		if (!c || parse_gen_phase_pdpar(o, pl->gen, c)) goto DEFER;
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
	uint8_t c = parse_gen_phase_pdpar(o, gen,
			sauScanner_getc_after(o->sc, '.'));
	switch (c) {
	case 'f':
		parse_par_list(o, NULL, NULL, false, 0, SAU_MOD_N_pf_pm, 0);
		break;
	default:
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
				begin_gen(o, pl->gen, false, 0);
			}
			break;
		case ';':
			pl->pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			if (parse_waittime(o)) {
				begin_gen(o, pl->gen, true, 0);
				pl->event->ev_flags |= SAU_PEV_FROM_GAPSHIFT;
			} else {
				if ((gen->time.flags &
				     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
				    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' without number");
				begin_gen(o, pl->gen, true, 0);
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
			if (parse_gen_main(o, SAU_PGEN_N_raseg, SAU_SYM_LINE_ID,
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
			uint8_t suffc = sauScanner_get_suffc(sc);
			switch (suffc) {
			case 'd':
				gen->time = sauTime_DEFAULT(
						o->sl.sopt.def_time_ms, 0);
				break;
			case 'i':
				if (!gen->is_nested) {
					sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested generator");
					break;
				}
				gen->time = sauTime_VALUE(
						o->sl.sopt.def_time_ms, 1);
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

static bool parse_numvar_rhs(sauParser *restrict o, sauSymitem *restrict var,
		bool check_unset, bool no_override) {
	uint8_t suffc;
	sauScanNumConst_f numconst_f = NULL;
	sauScanner_skipws(o->sc);
	switch ((suffc = sauScanner_get_suffc(o->sc))) {
	case 'c': numconst_f = scan_chanmix_const; break;
	case 'f': numconst_f = scan_note_const; break;
	case 'p': numconst_f = scan_cyclepos_const; break;
	case 's': numconst_f = scan_cyclepos_const; break;
	default: if (suffc) sauScanner_ungetc(o->sc); break;
	}
	if (numconst_f) sauScanner_skipws(o->sc);
	if (!var || (no_override && var->data_use == SAU_SYM_DATA_NUM)) {
		if (skip_num(o->sc, numconst_f))
			return false;
	} else {
		if (scan_num(o->sc, numconst_f, &var->data.num)) {
			var->data_use = SAU_SYM_DATA_NUM;
			if (var->data_id > 0)
				sauMath_vars_symbols[var->data_id - 1]
					(&o->sl.math_state, var->data.num);
			return false;
		}
	}
	if (var) sauScanner_warning(o->sc, NULL,
			"missing right-hand side value for \"$%s%s%s\"",
			check_unset ? "?" : "", var->sstr->key,
			(!check_unset && no_override) ? "?=" : "=");
	return true; // rejected expression
}

static bool parse_numvar_lhs(sauParser *restrict o) {
	bool check_unset = sauScanner_tryc(o->sc, '?'), was_unset = false;
	sauSymitem *var = scan_sym(o->sc, SAU_SYM_VAR, NULL, false);
	if (check_unset && var && var->data_use != SAU_SYM_DATA_NUM)
		was_unset = true;
	bool mark_fail = was_unset;
	bool no_override = check_unset;
	if (var) {
		sauScanner_skipws(o->sc);
		if (sauScanner_tryc(o->sc, '?')) {
			if (!check_unset)
				no_override = true;
			else
				sauScanner_warning(o->sc, NULL,
						"'$?%s' needs no '?' after",
						var->sstr->key);
		}
	}
	if (sauScanner_tryc(o->sc, '=')) {
		if (!parse_numvar_rhs(o, var, check_unset, no_override))
			mark_fail = false;
	} else if (!check_unset) {
		if (var) sauScanner_warning(o->sc, NULL,
				"variable '$%s' reference does nothing",
				var->sstr->key);
		if (no_override) sauScanner_ungetc(o->sc);
	}
	if (was_unset) {
		if (mark_fail) {
			o->script_fail = true;
			o->sc->s_flags |= SAU_SCAN_S_QUIET; // silence warnings
			sauScanner_notice(o->sc, NULL,
"usage: variable '$%s' in script wasn't set;\n"
"\ttry passing it to the script as an option, \"%s=...\"",
					var->sstr->key, var->sstr->key);
		} else {
			sauScanner_notice(o->sc, NULL,
"usage: variable '$%s' in script wasn't set;\n"
"\tusing the fallback value of %f; to set,\n"
"\tpass it to the script as an option, \"%s=...\"",
					var->sstr->key,
					var->data.num, var->sstr->key);
		}
	}
	return var; // skipped whitespace?
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
			pl.set_label = scan_sym(sc, SAU_SYM_LABEL, NULL, false);
			sauScanner_skipws(o->sc);
			if (sauScanner_tryc(sc, '=')) {
				sauSymitem *item = sauSymtab_find_item(o->st,
						pl.set_label->sstr,SAU_SYM_VAR);
				if (!item) item = sauSymtab_add_item(o->st,
						pl.set_label->sstr,SAU_SYM_VAR);
				warn_deprecated(sc,
					"\"'name=value\"", "\"$name=value\"");
				parse_numvar_rhs(o, item, false, false);
				pl.set_label = NULL; // used here
			}
			continue; /* no space is OK after, always (skipws) */
		case '/':
			if (NestArr_tip(&o->nest)) goto INVALID;
			parse_waittime(o);
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
		case '@': {
			/*
			 * Label reference (get and use object).
			 */
			pl.sub_f = NULL;
			sauSymitem *label = scan_sym(sc, SAU_SYM_LABEL,
					NULL, false);
			if (label != NULL) {
				if (label->data_use == SAU_SYM_DATA_OBJ) {
					sauParseGenData *gen = label->data.obj;
					if (gen->ref.obj_type == SAU_POBJT_GEN){
						begin_gen(o, gen, false, 0);
						gen = pl.gen;
						pl.sub_f = parse_in_gen_step;
					}
					label->data.obj = gen; /* update */
				} else {
					sauScanner_warning(sc, NULL,
"label '@%s' doesn't refer to any object", label->sstr->key);
				}
			}
			break; }
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
			if (parse_gen(o, SAU_PGEN_N_raseg, SAU_SYM_LINE_ID,
						sauLine_names)) break;
			pl.gen->mode.ras.flags = SAU_RAS_O_LINE_SET;
			if ((c = parse_gen_phase(o))) goto INVALID;
			break;
		case 'S':
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
	parse_level(o, SAU_MOD_N_carr, SCOPE_GROUP, 0);
	name = sc->f->path;
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
	    !init_ParseSem(&pr.ps, pr.mp)) goto DONE;
	const char *name = parse_file(&pr, arg);
	if (!name) goto DONE;
	parse->st = pr.st;
	parse->events = pr.events;
	parse->name = name;
	parse->sopt = pr.sl.sopt;
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
