/* SAU library: Script parser module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
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
#include <string.h>
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

/* Music note key 8-bit identifiers. Based on C, D, E, F, G, A, B scale. */
#define MUSKEY(note, notemod) (((note) * 9) + 4 + (notemod))
#define MUSNOTE(key) ((key) / 9)

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
	sauScanNumConst_f scan_numconst;
	bool num_ratio;
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
	.note_key = MUSKEY(0, 0),
	.key_octave = 4,
	.key_system = 0,
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
static size_t scan_note_const(sauScanner *restrict o,
		double *restrict val) {
	static const float notes_sau_ji[3][8] = {
		{ /* SAU JI flat (7-limit simplified 5-limit flat) */
			24.f/25, // Cf
			15.f/14, // Df alt. 16.f/15
			6.f/5,   // Ef
			21.f/16, // Ff alt. 125.f/96
			10.f/7,  // Gf alt. 36.f/25
			8.f/5,   // Af
			9.f/5,   // Bf alt. 16.f/9 (sym. 9/8)
			48.f/25, //
		},
		{ /* SAU JI natural (5-limit natural) */
			1.f/1,	 // C
			9.f/8,	 // D  alt. 10.f/9 (sym. 9/5)
			5.f/4,	 // E
			4.f/3,	 // F
			3.f/2,	 // G
			5.f/3,	 // A
			15.f/8,	 // B
			2.f/1,   //
		},
		{ /* SAU JI sharp (7-limit simplified 5-limit sharp) */
			25.f/24, // Cs
			7.f/6,   // Ds alt. 75.f/64
			9.f/7,   // Es alt. 32.f/25
			7.f/5,   // Fs alt. 25.f/18
			14.f/9,  // Gs alt. 25.f/16
			7.f/4,   // As alt. 225.f/128
			40.f/21, // Bs alt. 243.f/128, 256/135
			25.f/12, //
		},
	};
	static const float notes_main[3][8] = {
		{ /* Equal temperament */
			1.f,                    // 0	C
			1.1224620483093729814f, // 2	D
			1.2599210498948731648f, // 4	E
			1.3348398541700343648f, // 5	F
			1.4983070768766814988f, // 7	G
			1.6817928305074290860f, // 9	A
			1.8877486253633869932f, // 11	B
			2.f,                    //
		},
		{ /* 5-limit JI (Ptolemy's intense diatonic scale) */
			1.f/1,     // C
			9.f/8,     // D
			5.f/4,     // E
			4.f/3,     // F
			3.f/2,     // G
			5.f/3,     // A
			15.f/8,	   // B
			2.f/1,     //
		},
		{ /* Pythagorean tuning */
			1.f/1,     // C
			9.f/8,     // D
			81.f/64,   // E
			4.f/3,     // F
			3.f/2,     // G
			27.f/16,   // A
			243.f/128, // B
			2.f/1,     //
		},
	};
	static const float notemods_main[3][4] = {
		{ /* Equal temperament */
			1.0293022366434920288f,	// 1/2  z/d, quarter tone
			1.0594630943592952646f, // 1	s/b, semitone (sharp)
			1.0905077326652576592f,	// 3/2  k/v, 3/4 tone
			1.1224620483093729814f, // 2	x/w, tone
		},
		{ /* 5-limit JI (Ptolemy's intense diatonic scale) */
			36.f/35,      // z/d, septimal quarter tone
			25.f/24,      // s/b, augmented unison (sharp)
			25.f/24 * 36.f/35, // k/v
			25.f/24 * 25.f/24, // x/w
		},
		{ /* Pythagorean tuning */
			36.f/35,       // z/d, septimal quarter tone
			2187.f/2048,   // s/b, Pythagorean chromatic semitone
			2187.f/2048 * 36.f/35, // k/v
			2187.f/2048 * 2187.f/2048, // x/w
		},
	};
	sauFile *f = o->f;
	size_t len = 0, num_len;
	int note, subnote = -1;
	int c = sauFile_GETC(f); ++len;
	if (c >= 'a' && c <= 'g') {
		if ((c -= 'c') < 0) c += 7;
		subnote = c;
		c = sauFile_GETC(f); ++len;
	}
	if (c < 'A' || c > 'G') {
		sauFile_UNGETN(f, len);
		return 0;
	}
	if ((c -= 'C') < 0) c += 7;
	note = c;
	struct ScanLookup *sl = o->data;
	const int key = sl->sopt.note_key, key_note = MUSNOTE(key);
	int32_t octave, default_octave = sl->sopt.key_octave;
	int notemod = 0;
	switch (++len, (c = sauFile_GETC(f))) {
	/*case 'p':*/ /* fall-through */
	case 'd': notemod = -1; break; // half-flat
	case 'z': notemod = +1; break; // half-sharp
	case 'f': /* fall-through */
	case 'b': notemod = -2; break; // flat
	case 's': notemod = +2; break; // sharp
	case 'v': notemod = -3; break; // flat-and-a-half
	case 'k': notemod = +3; break; // sharp-and-a-half
	case 'w': notemod = -4; break; // double-flat
	case 'x': notemod = +4; break; // double-sharp
	default: --len; sauFile_DECP(f); break;
	}
	if (MUSKEY(note, notemod) < key) // wrap around below chosen key
		++default_octave;
	const float *notes, *notemods;
	double freq;
	if (sl->sopt.key_system < 3) {
		notes = notes_main[sl->sopt.key_system];
		notemods = notemods_main[sl->sopt.key_system];
		freq = sl->sopt.A4_freq / notes[5]; // tune using A4/A
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
		freq = sl->sopt.A4_freq / notes_sau_ji[1][5]; // tune using A4/A
	}
	if (notemod < 0)
		freq /= notemods[(-notemod) - 1]; // flatten
	else if (notemod > 0)
		freq *= notemods[notemod - 1]; // sharpen
	sauFile_geti(f, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = default_octave;
	else if (octave >= OCTAVES) {
		warn_int_range_fallback(o, NULL, 0, 10, default_octave,
				"note octave number");
		octave = default_octave;
	}
	freq *= notes[note] * OCTAVE(octave);
	if (subnote >= 0) {
		double shift = notes[subnote] / notes[key_note];
		if (subnote < key_note) // wrap around below chosen key
			shift += shift;
		shift = (notes[note+1] / notes[note] - 1.f) * (shift - 1.f);
		freq += freq * shift;
	}
	*val = freq;
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

static bool scan_line_param(sauScanner *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLine *restrict line, bool ratio) {
	bool state = scan_line_state(o, scan_numconst, line, ratio);
	if (!sauScanner_tryc(o, '{'))
		return state;
	warn_deprecated(o, "sweep in {...}", "sweep in [...]\n"
"\tat the beginning of the list, before any modulators added");
	struct ScanLookup *sl = o->data;
	bool warn_nospace = false;
	double vt;
	uint32_t time_ms = (line->flags & SAU_LINEP_TIME) != 0 ?
		line->time_ms :
		sl->sopt.def_time_ms;
	for (;;) {
		uint8_t c = sauScanner_getc(o);
		sauScanFrame sf_first = o->sf;
		switch (c) {
		case SAU_SCAN_SPACE:
		case SAU_SCAN_LNBRK:
			warn_nospace = false;
			continue;
		case 'g':
			if (scan_num(o, scan_numconst, &vt)) {
				line->vt = vt;
				line->flags |= SAU_LINEP_GOAL;
				if (ratio)
					line->flags |= SAU_LINEP_GOAL_RATIO;
				else
					line->flags &= ~SAU_LINEP_GOAL_RATIO;
			}
			break;
		case 'r':
			warn_deprecated(o, "sweep parameter 'r'", "name 'l'");
			/* fall-through */
		case 'l': {
			size_t id;
			if (!scan_sym_id(o, &id, SAU_SYM_LINE_ID,
						sauLine_names))
				break;
			line->type = id;
			line->flags |= SAU_LINEP_TYPE;
			break; }
		case 't':
			if (scan_time_val(o, &time_ms))
				line->flags &= ~SAU_LINEP_TIME_IF_NEW;
			break;
		case 'v':
			if (state) goto REJECT;
			scan_line_state(o, scan_numconst, line, ratio);
			break;
		case '}':
			goto RETURN;
		default:
		REJECT:
			if (!handle_unknown_or_eof(o, c)) {
				warn_eof_without_closing(o, '}');
				goto RETURN;
			}
			continue;
		}
		if (warn_nospace)
			warn_missing_whitespace(o, &sf_first, c);
		warn_nospace = true;
	}
RETURN:
	line->time_ms = time_ms;
	line->flags |= SAU_LINEP_TIME;
	return true;
}

/*
 * Parser
 */

typedef struct sauParser {
	struct ScanLookup sl;
	sauScanner *sc;
	sauSymtab *st;
	sauMempool *mp, *tmp_mp, *prg_mp;
	uint32_t call_level;
	/* node state */
	struct ParseLevel *cur_pl;
	sauScriptEvData *events, *last_event, *group_event;
	sauLine *cur_op_line;
} sauParser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(sauParser *restrict o) {
	sau_destroy_Scanner(o->sc);
	sau_destroy_Mempool(o->tmp_mp);
	sau_destroy_Mempool(o->prg_mp);
	sau_destroy_Mempool(o->mp);
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
	SCOPE_SAME = 0, // specially handled inner copy of parent scope
	SCOPE_GROUP,    // '<...>' or top scope
	SCOPE_BIND,     // '@[...]'
	SCOPE_NEST,     // '[...]'
};

typedef void (*ParseLevel_sub_f)(sauParser *restrict o);

static void parse_in_op_line(sauParser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_DEFERRED_SUB   = 1<<0, // \a sub_f exited to attempt handling above
	PL_BIND_MULTIPLE  = 1<<1, // previous node interpreted as set of nodes
	PL_NEW_EVENT_FORK = 1<<2,
	PL_OWN_EV         = 1<<3,
	PL_OWN_OP         = 1<<4,
	PL_WARN_NOSPACE   = 1<<5,
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
	sauScriptEvData *event;
	sauScriptListData *nest_list, *last_mods_list;
	sauScriptOpData *operator, *scope_first, *ev_last, *nest_last;
	sauSymitem *set_var;
	/* timing/delay */
	sauScriptEvData *main_ev; /* if events are nested, for grouping... */
	uint32_t add_wait_ms, carry_wait_ms; /* added for next event */
	float used_ampmult; /* update on node creation */
	sauScriptOptions sopt_save; /* save/restore on nesting */
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
	line->time_ms = sl->sopt.def_time_ms; /* initial default */
	line->flags |= SAU_LINEP_STATE |
		SAU_LINEP_TYPE |
		SAU_LINEP_TIME |
		SAU_LINEP_TIME_IF_NEW; /* default implicit value is flexible */
	if (mult) {
		line->flags |= SAU_LINEP_STATE_RATIO;
	}
	return line;
}

static bool prepare_line(sauParser *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLine **restrict linep, bool mult,
		uint32_t line_id) {
	if (!linep) { /* clear when no line provided */
		o->cur_op_line = NULL;
		return true;
	}
	if (!*linep) { /* create for updating, unparsed values kept unset */
		*linep = create_line(o, mult, line_id);
		(*linep)->flags &= ~(SAU_LINEP_STATE | SAU_LINEP_TYPE);
	}
	o->sl.scan_numconst = scan_numconst;
	o->sl.num_ratio = mult;
	o->cur_op_line = *linep;
	return true;
}

static bool parse_line(sauParser *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLine **restrict linep, bool mult,
		uint32_t line_id) {
	if (!*linep) { /* create for updating, unparsed values kept unset */
		*linep = create_line(o, mult, line_id);
		(*linep)->flags &= ~(SAU_LINEP_STATE | SAU_LINEP_TYPE);
	}
	return scan_line_param(o->sc, scan_numconst, *linep, mult);
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
	sauScriptEvData *e = pl->event;
	end_operator(o);
	pl->scope_first = pl->ev_last = NULL;
	pl->event = NULL;
	if (!o->group_event)
		o->group_event = (pl->main_ev != NULL) ? pl->main_ev : e;
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
			((prev_obj || pl->use_type == SAU_POP_CARR)
			 && pl->event->objs.first_item) ||
			is_compstep)
		begin_event(o, prev_obj, is_compstep);
}

static void begin_list(sauParser *restrict o,
		sauScriptListData *restrict plist,
		uint8_t use_type) {
	(void)plist;
	struct ParseLevel *pl = o->cur_pl, *parent_pl = pl->parent;
	/* end old event object after nesting popped out of... */
	sauScriptListData *list = sau_mpalloc(o->mp, sizeof(*list));
	list->use_type = use_type;
	/* linking done *before* setting up this as the nest list... */
	pl->nest_list = list;
	if (use_type != SAU_POP_CARR) {
		sauScriptOpData *parent_on = parent_pl->operator;
		if (!parent_on->mods)
			parent_on->mods = pl->nest_list;
		else
			parent_pl->last_mods_list->next_list = list;
		parent_pl->last_mods_list = pl->nest_list;
	}
}

static void begin_operator(sauParser *restrict o,
		sauScriptOpData *restrict pop, bool is_compstep,
		uint32_t type) {
	prepare_event(o, pop, is_compstep);
	struct ParseLevel *pl = o->cur_pl;
	sauScriptEvData *e = pl->event;
	sauScriptOpData *op;
	end_operator(o);
	pl->operator = op = sau_mpalloc(o->mp, sizeof(sauScriptOpData));
	pl->last_mods_list = NULL; /* now track for this node */
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
		op->time = (sauTime){pop->time.v_ms,
			(pop->time.flags & SAU_TIMEP_IMPLICIT)};
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
		op->time = (sauTime){o->sl.sopt.def_time_ms, 0};
		if (pl->use_type == SAU_POP_CARR) {
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
	if (pop || !pl->nest_list) {
		if (!e->objs.first_item)
			e->objs.first_item = op;
		else
			pl->ev_last->next = op;
		pl->ev_last = op;
	} else {
		if (!pl->nest_list->first_item)
			pl->nest_list->first_item = op;
		else
			pl->nest_last->next = op;
		pl->nest_last = op;
	}
	if (!pl->scope_first)
		pl->scope_first = op;
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
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel *restrict parent_pl = o->cur_pl;
	++o->call_level;
	o->cur_pl = pl;
	*pl = (struct ParseLevel){0};
	pl->scope = newscope;
	if (parent_pl != NULL) {
		pl->parent = parent_pl;
		pl->sub_f = parent_pl->sub_f;
		pl->pl_flags = parent_pl->pl_flags & PL_BIND_MULTIPLE;
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
			pl->sub_f = (use_type != SAU_POP_DEFAULT
				     && o->cur_op_line != NULL)
				? parse_in_op_line /* for parameter sub-args */
				: NULL;
			pl->set_var = parent_pl->set_var; // for list assign
			if (use_type == SAU_POP_DEFAULT)
				pl->nest_list = parent_pl->nest_list;
			begin_list(o, NULL, use_type);
			/*
			 * Push script options, and prepare for a new context.
			 *
			 * The amplitude multiplier is reset each list, unless
			 * an AMOD list (where the value builds on the outer).
			 */
			parent_pl->sopt_save = o->sl.sopt;
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
	--o->call_level;
	o->cur_pl = pl->parent;
	if (pl->scope == SCOPE_GROUP) {
		if (pl->pl_flags & PL_OWN_EV) {
			end_event(o);
			pl->parent->pl_flags |= PL_OWN_EV;
			pl->parent->event = pl->event;
		}
	}
	if (pl->scope == SCOPE_BIND) {
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->scope_first != NULL) {
			pl->parent->pl_flags |= PL_BIND_MULTIPLE;
			begin_operator(o, pl->scope_first, false, 0);
		}
	} else if (pl->scope == SCOPE_NEST) {
		/*
		 * Pop script options.
		 */
		o->sl.sopt = pl->parent->sopt_save;
	}
}

/*
 * Main parser functions
 */

#define PARSE_IN__HEAD(Name) \
	struct ParseLevel *pl = o->cur_pl; \
	sauScanner *sc = o->sc; \
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
	sauScanner_ungetc(sc); \
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */

static void parse_in_op_line(sauParser *restrict o) {
	struct ScanLookup *sl = &o->sl;
	sauLine *line = o->cur_op_line;
	PARSE_IN__HEAD(parse_in_op_line)
	double vt;
	switch (c) {
	case 'g':
		if (scan_num(sc, sl->scan_numconst, &vt)) {
			line->vt = vt;
			line->flags |= SAU_LINEP_GOAL;
			if (sl->num_ratio)
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
		scan_line_state(sc, sl->scan_numconst, line, sl->num_ratio);
		break;
	default:
		goto DEFER;
	}
	PARSE_IN__TAIL()
}

static bool parse_so_freq(sauParser *restrict o, bool rel_freq) {
	sauScanner *sc = o->sc;
	double val;
	int c;
	if (rel_freq) {
		if (scan_num(sc, NULL, &val)) {
			o->sl.sopt.def_relfreq = val;
			o->sl.sopt.set |= SAU_SOPT_DEF_RELFREQ;
		}
		return false;
	}
	if (scan_num(sc, scan_note_const, &val)) {
		o->sl.sopt.def_freq = val;
		o->sl.sopt.set |= SAU_SOPT_DEF_FREQ;
	}
	if (sauScanner_tryc(sc, '.')) switch ((c = sauScanner_getc(sc))) {
	case 'k': {
		int32_t octave = o->sl.sopt.key_octave;
		c = sauScanner_getc(sc);
		if (!SAU_IS_ASCIIVISIBLE(c))
			return true;
		if (c < 'A' || c > 'G') {
			if (IS_DIGIT(c)) {
				sauScanner_ungetc(sc);
				goto K_NUM;
			}
			sauScanner_warning(sc, NULL,
"invalid key; valid are 'A' through 'G',\n"
"\twith or without added 'b'/'d'/'v'/'w' (flat) or 's'/'z'/'k'/'x' (sharp)");
			break;
		}
		int sufc, notemod = 0;
		switch ((sufc = sauScanner_getc(sc))) {
		/*case 'p':*/ /* fall-through */
		case 'd': notemod = -1; break;
		case 'z': notemod = +1; break;
		case 'f': /* fall-through */
		case 'b': notemod = -2; break;
		case 's': notemod = +2; break;
		case 'v': notemod = -3; break;
		case 'k': notemod = +3; break;
		case 'w': notemod = -4; break;
		case 'x': notemod = +4; break;
		default: sauScanner_ungetc(sc); break;
		}
		if ((c -= 'C') < 0) c += 7;
		o->sl.sopt.note_key = MUSKEY(c, notemod);
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
			o->sl.sopt.set |= SAU_SOPT_A4_FREQ;
		}
		break;
	case 's':
		switch ((c = sauScanner_get_suffc(sc))) {
		case 'e':
			o->sl.sopt.key_system = 0;
			o->sl.sopt.set |= SAU_SOPT_NOTE_SCALE;
			break;
		case 'c':
			o->sl.sopt.key_system = 1;
			o->sl.sopt.set |= SAU_SOPT_NOTE_SCALE;
			break;
		case 'p':
			o->sl.sopt.key_system = 2;
			o->sl.sopt.set |= SAU_SOPT_NOTE_SCALE;
			break;
		case 'j':
			o->sl.sopt.key_system = 3;
			o->sl.sopt.set |= SAU_SOPT_NOTE_SCALE;
			break;
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
		return true;
	}
	return false;
}

static void parse_in_settings(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_settings)
	double val;
	switch (c) {
	case 'a':
		if (scan_num(sc, NULL, &val)) {
			// AMOD lists inherit outer value
			if (pl->use_type == SAU_POP_AMOD)
				val *= pl->parent->sopt_save.ampmult;
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
		if (parse_so_freq(o, false)) goto DEFER;
		break;
	case 'r':
		if (parse_so_freq(o, true)) goto DEFER;
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
		uint8_t use_type, uint8_t newscope);

static bool parse_ev_modparam(sauParser *restrict o,
		sauScanNumConst_f scan_numconst,
		sauLine **restrict linep, bool mult,
		uint32_t line_id, uint32_t mod_type) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	if (linep) // deprecated syntax
		parse_line(o, scan_numconst, linep, mult, line_id);
	bool append = !sauScanner_tryc(sc, '-');
	if (sauScanner_tryc(sc, '[')) {
		prepare_line(o, scan_numconst, linep, mult, line_id);
		parse_level(o, mod_type, SCOPE_NEST);
		if (append)
			pl->last_mods_list->flags |= SAU_SDLI_APPEND;
	}
	return false;
}

static bool parse_ev_amp(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	sauScriptOpData *op = pl->operator;
	uint8_t c;
	parse_ev_modparam(o, NULL, &op->amp, false,
			SAU_PSWEEP_AMP, SAU_POP_AMOD);
	if (sauScanner_tryc(sc, '.')) switch ((c = sauScanner_getc(sc))) {
	case 'r':
		parse_ev_modparam(o, NULL, &op->amp2, false,
				SAU_PSWEEP_AMP2, SAU_POP_RAMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_ev_chanmix(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScriptOpData *op = pl->operator;
	if (op->op_flags & SAU_SDOP_NESTED)
		return true; // reject
	parse_ev_modparam(o, scan_chanmix_const, &op->pan, false,
			SAU_PSWEEP_PAN, SAU_POP_CAMOD);
	return false;
}

static bool parse_ev_freq(sauParser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	sauScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_SDOP_NESTED))
		return true; // reject
	sauScanNumConst_f numconst_f = rel_freq ? NULL : scan_note_const;
	uint8_t c;
	parse_ev_modparam(o, numconst_f, &op->freq, rel_freq,
			SAU_PSWEEP_FREQ, SAU_POP_FMOD);
	if (sauScanner_tryc(sc, '.')) switch ((c = sauScanner_getc(sc))) {
	case 'r':
		parse_ev_modparam(o, numconst_f, &op->freq2, rel_freq,
				SAU_PSWEEP_FREQ2, SAU_POP_RFMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_ev_phase(sauParser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	sauScanner *sc = o->sc;
	sauScriptOpData *op = pl->operator;
	double val;
	if (scan_num(sc, scan_phase_const, &val)) {
		op->phase = sau_cyclepos_dtoui32(val);
		op->params |= SAU_POPP_PHASE;
	}
	uint8_t c;
	parse_ev_modparam(o, NULL, NULL, false, 0, SAU_POP_PMOD);
	if (sauScanner_tryc(sc, '.')) switch ((c = sauScanner_getc(sc))) {
	case 'f':
		parse_ev_modparam(o, NULL, NULL, false, 0, SAU_POP_FPMOD);
		break;
	default:
		return true;
	}
	return false;
}

static bool parse_ev_mode(sauParser *restrict o) {
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
		case 'h': flags |= SAU_RAS_O_HALFSHAPE; break;
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
			if (IS_DIGIT(c)) scan_int_in_range(sc, 0, 9, 9,
					&level, "mode level");
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

static void parse_in_opdata(sauParser *restrict o) {
	PARSE_IN__HEAD(parse_in_opdata)
	sauScriptOpData *op = pl->operator;
	switch (c) {
	case '/':
		if (parse_waittime(o)) {
			begin_operator(o, pl->operator, false, 0);
		}
		break;
	case ';':
		pl->pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
		if (parse_waittime(o)) {
			begin_operator(o, pl->operator, true, 0);
			pl->event->ev_flags |= SAU_SDEV_FROM_GAPSHIFT;
		} else {
			if ((op->time.flags &
			     (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT)) ==
			    (SAU_TIMEP_SET|SAU_TIMEP_IMPLICIT))
				sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' without number");
			begin_operator(o, pl->operator, true, 0);
			pl->event->ev_flags |= SAU_SDEV_WAIT_PREV_DUR;
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
		if (parse_ev_mode(o)) goto DEFER;
		break;
	case 'p':
		if (parse_ev_phase(o)) goto DEFER;
		break;
	case 'r':
		if (parse_ev_freq(o, true)) goto DEFER;
		break;
	case 't': {
		uint8_t suffc = sauScanner_get_suffc(sc);
		switch (suffc) {
		case 'd':
			op->time = (sauTime){o->sl.sopt.def_time_ms,0};
			break;
		case 'i':
			if (!(op->op_flags & SAU_SDOP_NESTED)) {
				sauScanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
				break;
			}
			op->time = (sauTime){o->sl.sopt.def_time_ms,
				SAU_TIMEP_SET | SAU_TIMEP_IMPLICIT};
			break;
		default:
			if (suffc)
				sauScanner_ungetc(sc);
			uint32_t time_ms;
			if (!scan_time_val(sc, &time_ms))
				break;
			op->time = (sauTime){time_ms, SAU_TIMEP_SET};
			break;
		}
		op->params |= SAU_POPP_TIME;
		break; }
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
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel pl;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope);
	sauScanner *sc = o->sc;
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
		uint8_t c = sauScanner_getc(sc);
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
			if (pl.nest_list) goto INVALID;
			parse_waittime(o);
			break;
		case '<':
			if (parse_level(o, pl.use_type, SCOPE_GROUP))
				goto RETURN;
			break;
		case '=': {
			uint8_t suffc;
			sauScanNumConst_f numconst_f = NULL;
			sauSymitem *var = pl.set_var;
			if (!var) goto INVALID;
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK before */
			pl.set_var = NULL; // used here
			switch ((suffc = sauScanner_get_suffc(sc))) {
			case 'c': numconst_f = scan_chanmix_const; break;
			case 'f': numconst_f = scan_note_const; break;
			case 'p': numconst_f = scan_phase_const; break;
			default: if (suffc) sauScanner_ungetc(sc); break;
			}
			if (numconst_f) sauScanner_skipws(sc);
			if (scan_num(sc, numconst_f, &var->data.num))
				var->data_use = SAU_SYM_DATA_NUM;
			else
				sauScanner_warning(sc, NULL,
"missing right-hand value for \"'%s=\"", var->sstr->key);
			break; }
		case '>':
			if (pl.scope == SCOPE_GROUP) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@': {
			if (sauScanner_tryc(sc, '[')) {
				end_operator(o);
				if (parse_level(o, pl.use_type, SCOPE_BIND))
					goto RETURN;
				/*
				 * Multiple-operator node now open.
				 */
				pl.sub_f = parse_in_opdata;
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
					begin_operator(o, ref, false, 0);
					ref = pl.operator;
					var->data.obj = ref; /* update */
					pl.sub_f = parse_in_opdata;
				} else {
					sauScanner_warning(sc, NULL,
"reference '@%s' doesn't point to an object", var->sstr->key);
				}
			}
			break; }
		case 'R': {
			size_t id = 0; /* default as fallback value */
			scan_sym_id(sc, &id, SAU_SYM_LINE_ID, sauLine_names);
			begin_operator(o, NULL, false, SAU_POPT_RAS);
			pl.operator->ras_opt.line = id;
			pl.operator->ras_opt.flags = SAU_RAS_O_LINE_SET;
			pl.sub_f = parse_in_opdata;
			break; }
		case 'S':
			pl.sub_f = parse_in_settings;
			break;
		case 'O':
			warn_deprecated(sc, "type 'O'", "name 'W'");
			/* fall-through */
		case 'W': {
			size_t id = 0; /* default as fallback value */
			scan_sym_id(sc, &id, SAU_SYM_WAVE_ID, sauWave_names);
			begin_operator(o, NULL, false, SAU_POPT_WAVE);
			pl.operator->wave = id;
			pl.sub_f = parse_in_opdata;
			break; }
		case '[':
			warn_opening_disallowed(sc, '[');
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
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
			pl.pl_flags &= ~PL_WARN_NOSPACE; /* OK around */
			continue;
		case '|':
			if (pl.nest_list) goto INVALID;
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
static const char *parse_file(sauParser *restrict o,
		const sauScriptArg *restrict arg) {
	sauScanner *sc = o->sc;
	const char *name;
	if (!sauScanner_open(sc, arg->str, arg->is_path)) {
		return NULL;
	}
	parse_level(o, SAU_POP_CARR, SCOPE_GROUP);
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
	for (e = e_from; e; ) {
		for (sauScriptOpData *op = e->objs.first_item; op;
				op = op->next) {
			if (!(op->time.flags & SAU_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = cur_longest + wait_sum;
				op->time.flags |= SAU_TIMEP_SET;
				if (e->dur_ms < op->time.v_ms)
					e->dur_ms = op->time.v_ms;
				time_op_lines(op);
			}
		}
		if (!e->next) break;
		e = e->next;
		wait_sum -= e->wait_ms;
	}
	/*
	 * Flatten event forks in pass following timing adjustments per
	 * durgroup; the wait times must be correctly filled in for the
	 * unified event list to always be arranged in the right order.
	 */
	for (e = e_from; e; ) {
		while (e->forks != NULL) flatten_events(e);
		/*
		 * Track sequence of references and later use here.
		 */
		for (sauScriptOpData *sub_op = e->objs.first_item;
				sub_op; sub_op = sub_op->next) {
			sauScriptOpData *prev_ref = sub_op->info->last_ref;
			if (prev_ref != NULL) {
				sub_op->prev_ref = prev_ref;
				prev_ref->op_flags |= SAU_SDOP_LATER_USED;
				prev_ref->event->ev_flags |=
					SAU_SDEV_VOICE_LATER_USED;
			}
			sub_op->info->last_ref = sub_op;
		}
		if (!e->next) break;
		if (e == e_subtract_after) subtract = true;
		e = e->next;
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
		op->time.flags |= SAU_TIMEP_DEFAULT;
		if (op->op_flags & SAU_SDOP_NESTED) {
			op->time.flags |= SAU_TIMEP_IMPLICIT;
			op->time.flags |= SAU_TIMEP_SET; /* no durgroup yet */
		}
	} else if (!(op->op_flags & SAU_SDOP_NESTED)) {
		op->event->ev_flags |= SAU_SDEV_LOCK_DUR_SCOPE;
	}
	for (sauScriptListData *list = op->mods;
			list != NULL; list = list->next_list) {
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
		sauTime def_time = {
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
			def_time = (sauTime){
				ne_op->time.v_ms,
				(ne_op->time.flags & SAU_TIMEP_IMPLICIT)
			};
			if (ne->ev_flags & SAU_SDEV_FROM_GAPSHIFT) {
				if (ne_op_prev->time.flags & SAU_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SAU_SDEV_FROM_GAPSHIFT)) {
					ne_op_prev->time = (sauTime){ // gap
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
