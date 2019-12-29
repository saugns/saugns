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

#include "scanner.h"
#include "symtab.h"
#include "parser.h"
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

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

typedef struct ScanLookup {
	SAU_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*rac_names;
} ScanLookup;

/*
 * Default script options, used until changed in a script.
 */
static const SAU_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
};

static bool init_ScanLookup(ScanLookup *restrict o, SAU_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SAU_SymTab_pool_stra(st,
			SAU_Wave_names, SAU_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->rac_names = SAU_SymTab_pool_stra(st,
			SAU_RampCurve_names, SAU_RAC_TYPES);
	if (!o->rac_names)
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
	if (IS_VISIBLE(c)) {
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

typedef float (*NumSym_f)(SAU_Scanner *restrict o);

typedef struct NumParser {
	SAU_Scanner *sc;
	NumSym_f numsym_f;
	SAU_ScanFrame sf_start;
	bool has_infnum;
} NumParser;
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
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
		num = scan_num_r(o, 255, level+1);
		if (minus) num = -num;
		if (level == 0)
			return num;
	} else if (o->numsym_f && IS_ALPHA(c)) {
		SAU_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (isnan(num))
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
	if (pri == 0)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		c = SAU_Scanner_getc(sc);
		switch (c) {
		case '(':
			num *= scan_num_r(o, 255, level+1);
			break;
		case ')':
			if (pri < 255) goto DEFER;
			return num;
		case '^':
			num = exp(log(num) * scan_num_r(o, 0, level));
			break;
		case '*':
			num *= scan_num_r(o, 1, level);
			break;
		case '/':
			num /= scan_num_r(o, 1, level);
			break;
		case '+':
			if (pri < 2) goto DEFER;
			num += scan_num_r(o, 2, level);
			break;
		case '-':
			if (pri < 2) goto DEFER;
			num -= scan_num_r(o, 2, level);
			break;
		default:
			if (pri == 255) {
				SAU_Scanner_warning(sc, &o->sf_start,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	return num;
}
static sauNoinline bool scan_num(SAU_Scanner *restrict o,
		NumSym_f scan_numsym, float *restrict var) {
	NumParser np = {o, scan_numsym, o->sf, false};
	uint8_t ws_level = o->ws_level;
	float num = scan_num_r(&np, 0, 0);
	SAU_Scanner_setws_level(o, ws_level); // restore if changed
	if (isnan(num))
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

static sauNoinline bool scan_time(SAU_Scanner *restrict o,
		uint32_t *restrict var) {
	SAU_ScanFrame sf = o->sf;
	float num_s;
	if (!scan_num(o, NULL, &num_s))
		return false;
	if (num_s < 0.f) {
		SAU_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	uint32_t num_ms;
	num_ms = lrint(num_s * 1000.f);
	*var = num_ms;
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
	ScanLookup *sl = o->data;
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
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f +
			(notes[semitone][note+1] / notes[semitone][note] -
				1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static const char *scan_label(SAU_Scanner *restrict o,
		size_t *restrict lenp, char op) {
	const void *s = NULL;
	SAU_Scanner_getsymstr(o, &s, lenp);
	if (*lenp == 0) {
		SAU_Scanner_warning(o, NULL,
				"ignoring %c without label name", op);
	}
	return s;
}

static bool scan_symafind(SAU_Scanner *restrict o,
		const char *const*restrict stra, size_t n,
		size_t *restrict found_i, const char *restrict print_type) {
	SAU_ScanFrame sf_begin = o->sf;
	const void *key = NULL;
	size_t len;
	SAU_Scanner_getsymstr(o, &key, &len);
	if (len == 0) {
		SAU_Scanner_warning(o, NULL, "%s missing", print_type);
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		if (stra[i] == key) {
			*found_i = i;
			return true;
		}
	}
	SAU_Scanner_warning(o, &sf_begin,
			"invalid %s; available types are:", print_type);
	fprintf(stderr, "\t%s", stra[0]);
	for (size_t i = 1; i < n; ++i) {
		fprintf(stderr, ", %s", stra[i]);
	}
	putc('\n', stderr);
	return false;
}

static bool scan_wavetype(SAU_Scanner *restrict o, size_t *restrict found_id) {
	ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names, SAU_WAVE_TYPES,
			found_id, "wave type");
}

static bool scan_ramp_state(SAU_Scanner *restrict o, NumSym_f scan_numsym,
		SAU_Ramp *restrict ramp, bool mult) {
	if (!scan_num(o, scan_numsym, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SAU_RAMP_STATE_RATIO;
	} else {
		ramp->flags &= ~SAU_RAMP_STATE_RATIO;
	}
	ramp->flags |= SAU_RAMP_STATE;
	return true;
}

static bool scan_ramp(SAU_Scanner *restrict o, NumSym_f scan_numsym,
		SAU_Ramp *restrict ramp, bool mult) {
	ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SAU_RAMP_TIME_SET) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t curve = ramp->curve; // has default
	if ((ramp->flags & SAU_RAMP_CURVE) != 0) {
		// allow partial change
		if (((ramp->flags & SAU_RAMP_CURVE_RATIO) != 0) == mult) {
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
			size_t type;
			if (scan_symafind(o, sl->rac_names, SAU_RAC_TYPES,
					&type, "curve type")) {
				curve = type;
			}
			break; }
		case 't':
			if (scan_time(o, &time_ms))
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
		SAU_Scanner_warning(o, NULL,
				"ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->curve = curve;
	ramp->flags |= SAU_RAMP_CURVE;
	if (mult)
		ramp->flags |= SAU_RAMP_CURVE_RATIO;
	else
		ramp->flags &= ~SAU_RAMP_CURVE_RATIO;
	if (time_set)
		ramp->flags |= SAU_RAMP_TIME_SET;
	else
		ramp->flags &= ~SAU_RAMP_TIME_SET;
	return true;
}

/*
 * Parser
 */

typedef struct SAU_Parser {
	ScanLookup sl;
	SAU_Scanner *sc;
	SAU_SymTab *st;
	SAU_MemPool *mp;
	uint32_t call_level;
	/* node state */
	SAU_ParseEvData *ev, *first_ev;
} SAU_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SAU_Parser *restrict o) {
	SAU_destroy_Scanner(o->sc);
	SAU_destroy_SymTab(o->st);
	SAU_destroy_MemPool(o->mp);
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
	SAU_MemPool *mp = SAU_create_MemPool(0);
	SAU_SymTab *st = SAU_create_SymTab(mp);
	SAU_Scanner *sc = SAU_create_Scanner(st);
	*o = (SAU_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mp = mp;
	if (!sc || !st || !mp) goto ERROR;
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
	SCOPE_TOP = 0,
	SCOPE_BLOCK,
	SCOPE_BIND,
	SCOPE_NEST,
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
	SDPL_BIND_MULTIPLE = 1<<0, // previous node interpreted as set of nodes
	SDPL_NESTED_SCOPE = 1<<1,
	SDPL_ACTIVE_EV = 1<<2,
	SDPL_ACTIVE_OP = 1<<3,
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
	uint8_t list_type;
	uint8_t last_list_type; /* FIXME: kludge */
	SAU_ParseEvData *event, *last_event;
	SAU_NodeRef *op_ref, *parent_op_ref;
	SAU_NodeRef *first_op_ref;
	SAU_ParseOpData *last_op;
	SAU_NodeList *op_list;
	const char *set_label; /* label assigned to next node */
	/* timing/delay */
	SAU_ParseEvData *group_from; /* where to begin for group_events() */
	SAU_ParseEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

static bool parse_waittime(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SAU_Scanner_tryc(sc, 't')) {
		if (!pl->last_op) {
			SAU_Scanner_warning(sc, NULL,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SAU_PDEV_ADD_WAIT_DURATION;
	} else {
		uint32_t wait_ms;
		if (scan_time(sc, &wait_ms)) {
			pl->next_wait_ms += wait_ms;
		}
	}
	return true;
}

/*
 * Node- and scope-handling functions
 */

static sauNoinline void end_operator(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_OP;
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_ParseOpData *op = pl->op_ref->data;
	if (SAU_Ramp_ENABLED(&op->freq))
		op->op_params |= SAU_POPP_FREQ;
	if (SAU_Ramp_ENABLED(&op->freq2))
		op->op_params |= SAU_POPP_FREQ2;
	if (SAU_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SAU_POPP_AMP;
		if (!(op->op_flags & SAU_PDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SAU_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SAU_POPP_AMP2;
		if (!(op->op_flags & SAU_PDOP_NESTED)) {
			op->amp2.v0 *= sl->sopt.ampmult;
			op->amp2.vt *= sl->sopt.ampmult;
		}
	}
	SAU_ParseOpData *pop = op->prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params |= SAU_POPP_WAVE |
			SAU_POPP_TIME |
			SAU_POPP_SILENCE |
			SAU_POPP_FREQ |
			SAU_POPP_FREQ2 |
			SAU_POPP_PHASE |
			SAU_POPP_AMP |
			SAU_POPP_AMP2;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SAU_POPP_WAVE;
		/* SAU_TIME set when time set */
		if (op->silence_ms != 0)
			op->op_params |= SAU_POPP_SILENCE;
		/* SAU_PHASE set when phase set */
	}
	pl->op_ref = NULL;
	pl->last_op = op;
}

static sauNoinline void end_event(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_EV;
	SAU_ParseEvData *e = pl->event;
	end_operator(pl);
	if (SAU_Ramp_ENABLED(&e->pan))
		e->vo_params |= SAU_PVOP_PAN;
	SAU_ParseEvData *pve = e->vo_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->vo_params |= SAU_PVOP_PAN;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl,
		SAU_NodeRef *restrict prev_op_ref,
		bool is_composite) {
	SAU_Parser *o = pl->o;
	end_event(pl);
	SAU_ParseEvData *e = SAU_MemPool_alloc(o->mp, sizeof(SAU_ParseEvData));
	pl->event = e;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	e->op_list.type = SAU_SDLT_GRAPH;
	SAU_Ramp_reset(&e->pan);
	if (prev_op_ref != NULL) {
		SAU_ParseOpData *pod = prev_op_ref->data;
		SAU_ParseEvData *pve = pod->event;
		if (is_composite) {
			if (!pl->composite) {
				pve->composite = e;
				pl->composite = pve;
			} else {
				pve->next = e;
			}
		}
		e->vo_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SAU_RAMP_STATE;
	}
	if (!pl->group_from)
		pl->group_from = e;
	if (!is_composite) {
		if (!o->first_ev)
			o->first_ev = e;
		else
			o->ev->next = e;
		o->ev = e;
		pl->composite = NULL;
	}
	pl->pl_flags |= SDPL_ACTIVE_EV;
}

/*
 * Add new operator to parent(s), ie. either to the
 * current event node, or to an operator node (ordinary or multiple)
 * in the case of operator linking/nesting.
 */
static SAU_NodeRef *list_operator(ParseLevel *restrict pl,
		SAU_ParseOpData *od, uint8_t ref_mode) {
	SAU_Parser *o = pl->o;
	SAU_ParseEvData *e = pl->event;
	SAU_NodeList *ol = pl->op_list;
	if (pl->list_type == SAU_SDLT_GRAPH ||
			!(ref_mode & SAU_SDRM_ADD)) {
		ol = &e->op_list;
	}
	SAU_NodeRef *ref = SAU_NodeList_add(ol, od, ref_mode, o->mp);
	pl->op_ref = ref;
	if (!pl->first_op_ref)
		pl->first_op_ref = ref;
	pl->last_list_type = pl->list_type; /* FIXME: kludge */
	return ref;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 */
static void begin_operator(ParseLevel *restrict pl,
		SAU_NodeRef *restrict prev_op_ref,
		uint8_t ref_mode, bool is_composite) {
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	if (!pl->event || /* not in event means previous implicitly ended */
			pl->location != SDPL_IN_EVENT ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, prev_op_ref, is_composite);
	SAU_ParseEvData *e = pl->event;
	end_operator(pl);
	SAU_ParseOpData *op = SAU_MemPool_alloc(o->mp, sizeof(SAU_ParseOpData));
	if (!is_composite && pl->last_op != NULL)
		pl->last_op->next_bound = op;
	SAU_NodeRef *ref = list_operator(pl, op, ref_mode);
	/*
	 * Initialize node.
	 */
	SAU_Ramp_reset(&op->freq);
	SAU_Ramp_reset(&op->freq2);
	SAU_Ramp_reset(&op->amp);
	SAU_Ramp_reset(&op->amp2);
	if (prev_op_ref != NULL) {
		SAU_ParseOpData *pop = prev_op_ref->data;
		op->prev = pop;
		op->op_flags = pop->op_flags &
			(SAU_PDOP_NESTED | SAU_PDOP_MULTIPLE);
		if (is_composite) {
			/*
			 * Context-sensitive time default.
			 */
			pop->op_flags |= SAU_PDOP_HAS_COMPOSITE;
			op->op_flags |= SAU_PDOP_TIME_DEFAULT;
		}
		op->time_ms = pop->time_ms;
		op->wave = pop->wave;
		op->phase = pop->phase;
		if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
			SAU_ParseOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time_ms)
					max_time = mpop->time_ms;
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SAU_PDOP_MULTIPLE;
			op->time_ms = max_time;
			pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 *
		 * time default: depends on context
		 */
		op->op_flags = SAU_PDOP_TIME_DEFAULT;
		op->time_ms = sl->sopt.def_time_ms;
		if (ref->list_type == SAU_SDLT_GRAPH) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SAU_PDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SAU_RAMP_STATE_RATIO;
		}
		op->freq.flags |= SAU_RAMP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SAU_RAMP_STATE;
	}
	op->event = e;
	/*
	 * Assign label. If no new label but previous node (for a non-composite)
	 * has one, update label to point to new node, but keep pointer in
	 * previous node.
	 */
	if (pl->set_label != NULL) {
		ref->label = pl->set_label;
		pl->set_label = NULL;
		SAU_SymTab_set(o->st, ref->label, strlen(ref->label), ref);
	} else if (!is_composite
			&& prev_op_ref != NULL && prev_op_ref->label != NULL) {
		ref->label = prev_op_ref->label;
		SAU_SymTab_set(o->st, ref->label, strlen(ref->label), ref);
	}
	pl->pl_flags |= SDPL_ACTIVE_OP;
}

static void begin_scope(SAU_Parser *restrict o, ParseLevel *restrict pl,
		ParseLevel *restrict parent_pl,
		uint8_t list_type, uint8_t newscope) {
	*pl = (ParseLevel){0};
	pl->o = o;
	pl->scope = newscope;
	pl->list_type = list_type;
	if (!parent_pl) {
		// handle newscope == SCOPE_TOP here
		pl->op_list = SAU_create_NodeList(list_type, o->mp);
		return;
	}
	pl->parent = parent_pl;
	pl->pl_flags = parent_pl->pl_flags &
		(SDPL_NESTED_SCOPE | SDPL_BIND_MULTIPLE);
	pl->location = parent_pl->location;
	pl->event = parent_pl->event;
	pl->op_ref = parent_pl->op_ref;
	pl->parent_op_ref = parent_pl->parent_op_ref;
	switch (newscope) {
	case SCOPE_BLOCK:
		pl->group_from = parent_pl->group_from;
		pl->op_list = parent_pl->op_list;
		break;
	case SCOPE_BIND:
		pl->group_from = parent_pl->group_from;
		pl->op_list = SAU_create_NodeList(list_type, o->mp);
		break;
	case SCOPE_NEST:
		pl->pl_flags |= SDPL_NESTED_SCOPE;
		pl->parent_op_ref = parent_pl->op_ref;
		pl->op_list = SAU_create_NodeList(list_type, o->mp);
		break;
	default:
		break;
	}
}

static void end_scope(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	end_operator(pl);
	if (pl->set_label != NULL) {
		SAU_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
	switch (pl->scope) {
	case SCOPE_TOP: {
		/*
		 * At end of top scope (ie. at end of script),
		 * end last event and adjust timing.
		 */
		SAU_ParseEvData *group_to;
		end_event(pl);
		group_to = (pl->composite) ? pl->composite : pl->last_event;
		if (group_to)
			group_to->groupfrom = pl->group_from;
		break; }
	case SCOPE_BLOCK:
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		if (pl->pl_flags & SDPL_ACTIVE_EV) {
			end_event(pl->parent);
			pl->parent->pl_flags |= SDPL_ACTIVE_EV;
			pl->parent->event = pl->event;
		}
		if (pl->last_event != NULL)
			pl->parent->last_event = pl->last_event;
		break;
	case SCOPE_BIND:
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_op_ref != NULL) {
			pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
			uint8_t list_type = pl->parent->list_type;
			pl->parent->list_type = pl->parent->last_list_type;
			begin_operator(pl->parent, pl->first_op_ref,
					SAU_SDRM_UPDATE, false);
			pl->parent->list_type = list_type;
		}
		break;
	case SCOPE_NEST: {
		if (!pl->parent_op_ref)
			break;
		SAU_ParseOpData *parent_op = pl->parent_op_ref->data;
		if (!parent_op->nest_lists)
			parent_op->nest_lists = pl->op_list;
		else
			parent_op->last_nest_list->next = pl->op_list;
		parent_op->last_nest_list = pl->op_list;
		break; }
	default:
		break;
	}
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_Scanner *sc = o->sc;
	pl->location = SDPL_IN_DEFAULTS;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case 'a':
			if (scan_num(sc, NULL, &sl->sopt.ampmult))
				sl->sopt.changed |= SAU_SOPT_AMPMULT;
			break;
		case 'f':
			if (scan_num(sc, scan_note, &sl->sopt.def_freq))
				sl->sopt.changed |= SAU_SOPT_DEF_FREQ;
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
				sl->sopt.changed |= SAU_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.changed |= SAU_SOPT_DEF_RATIO;
			break;
		case 't':
			if (scan_time(sc, &sl->sopt.def_time_ms)) {
				sl->sopt.changed |= SAU_SOPT_DEF_TIME;
			}
			break;
		default:
			goto UNKNOWN;
		}
	}
	return false;
UNKNOWN:
	SAU_Scanner_ungetc(sc);
	return true; /* let parse_level() take care of it */
}

static bool parse_level(SAU_Parser *restrict o, ParseLevel *restrict parent_pl,
		uint8_t list_type, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->op_ref->data;
	if (SAU_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, NULL, &op->amp, false);
	} else {
		scan_ramp_state(sc, NULL, &op->amp, false);
	}
	if (SAU_Scanner_tryc(sc, ',')) {
		if (SAU_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, NULL, &op->amp2, false);
		} else {
			scan_ramp_state(sc, NULL, &op->amp2, false);
		}
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, pl, SAU_SDLT_AMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->op_ref->data;
	if (rel_freq && !(op->op_flags & SAU_PDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	if (SAU_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, numsym_f, &op->freq, rel_freq);
	} else {
		scan_ramp_state(sc, numsym_f, &op->freq, rel_freq);
	}
	if (SAU_Scanner_tryc(sc, ',')) {
		if (SAU_Scanner_tryc(sc, '{')) {
			scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
		} else {
			scan_ramp_state(sc, numsym_f, &op->freq2, rel_freq);
		}
	}
	if (SAU_Scanner_tryc(sc, '~') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, pl, SAU_SDLT_FMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->op_ref->data;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SAU_POPP_PHASE;
	}
	if (SAU_Scanner_tryc(sc, '+') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, pl, SAU_SDLT_PMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_pan(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	SAU_Scanner *sc = o->sc;
	if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
		return true; // reject
	SAU_ParseEvData *e = pl->event;
	if (SAU_Scanner_tryc(sc, '{')) {
		scan_ramp(sc, NULL, &e->pan, false);
	} else {
		scan_ramp_state(sc, NULL, &e->pan, false);
	}
	return false;
}

static bool parse_step(ParseLevel *restrict pl) {
	SAU_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SAU_Scanner *sc = o->sc;
	if (!pl->op_ref) {
		SAU_error("parser", "parse_step() called with NULL op_ref");
		return false;
	}
	SAU_ParseOpData *op = pl->op_ref->data;
	pl->location = SDPL_IN_EVENT;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case 'P':
			if (parse_ev_pan(pl)) goto UNKNOWN;
			break;
		case '\\':
			if (parse_waittime(pl)) {
				begin_operator(pl, pl->op_ref,
						SAU_SDRM_UPDATE, false);
			}
			break;
		case 'a':
			if (parse_ev_amp(pl)) goto UNKNOWN;
			break;
		case 'f':
			if (parse_ev_freq(pl, false)) goto UNKNOWN;
			break;
		case 'p':
			if (parse_ev_phase(pl)) goto UNKNOWN;
			break;
		case 'r':
			if (parse_ev_freq(pl, true)) goto UNKNOWN;
			break;
		case 's':
			scan_time(sc, &op->silence_ms);
			break;
		case 't':
			if (SAU_Scanner_tryc(sc, '*')) {
				/* later fitted or kept at default value */
				op->op_flags |= SAU_PDOP_TIME_DEFAULT;
				op->time_ms = sl->sopt.def_time_ms;
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_PDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
				op->time_ms = SAU_TIME_INF;
			} else if (scan_time(sc, &op->time_ms)) {
				op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
			} else {
				break;
			}
			op->op_params |= SAU_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			break; }
		default:
			goto UNKNOWN;
		}
	}
	return false;
UNKNOWN:
	SAU_Scanner_ungetc(sc);
	return true; /* let parse_level() take care of it */
}

enum {
	HANDLE_DEFER = 1<<0,
	DEFERRED_STEP = 1<<1,
	DEFERRED_SETTINGS = 1<<2,
};
static bool parse_level(SAU_Parser *restrict o, ParseLevel *restrict parent_pl,
		uint8_t list_type, uint8_t newscope) {
	ParseLevel pl;
	const char *label;
	size_t label_len;
	uint8_t flags = 0;
	bool endscope = false;
	begin_scope(o, &pl, parent_pl, list_type, newscope);
	++o->call_level;
	SAU_Scanner *sc = o->sc;
	for (;;) {
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
				flags = 0;
				pl.location = SDPL_IN_NONE;
				pl.first_op_ref = NULL;
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
			label = scan_label(sc, &label_len, c);
			pl.set_label = label;
			break;
		case ';':
			if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
				goto INVALID;
			begin_operator(&pl, pl.op_ref, SAU_SDRM_UPDATE, true);
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break;
		case '@':
			if (SAU_Scanner_tryc(sc, '[')) {
				end_operator(&pl);
				if (parse_level(o, &pl, list_type, SCOPE_BIND))
					goto RETURN;
				/*
				 * Multiple-operator node now open.
				 */
				flags = parse_step(&pl) ?
					(HANDLE_DEFER | DEFERRED_STEP) :
					0;
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
			pl.location = SDPL_IN_NONE;
			label = scan_label(sc, &label_len, c);
			if (label_len > 0) {
				SAU_NodeRef *ref;
				ref = SAU_SymTab_get(o->st, label, label_len);
				if (!ref)
					SAU_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_operator(&pl, ref,
							SAU_SDRM_UPDATE, false);
					flags = parse_step(&pl) ?
						(HANDLE_DEFER | DEFERRED_STEP) :
						0;
				}
			}
			break;
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_operator(&pl, NULL, SAU_SDRM_ADD, false);
			SAU_ParseOpData *od = pl.op_ref->data;
			od->wave = wave;
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break; }
		case 'S':
			flags = parse_settings(&pl) ?
				(HANDLE_DEFER | DEFERRED_SETTINGS) :
				0;
			break;
		case '[':
			if (parse_level(o, &pl, list_type, SCOPE_BLOCK))
				goto RETURN;
			break;
		case '\\':
			if (pl.location == SDPL_IN_DEFAULTS ||
((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
				goto INVALID;
			parse_waittime(&pl);
			break;
		case ']':
			if (pl.scope == SCOPE_NEST) {
				end_operator(&pl);
			}
			if (pl.scope > SCOPE_TOP) {
				endscope = true;
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '|':
			if (pl.location == SDPL_IN_DEFAULTS ||
((pl.pl_flags & SDPL_NESTED_SCOPE) != 0 && pl.event != NULL))
				goto INVALID;
			if (!pl.event) {
				SAU_Scanner_warning(sc, NULL,
"end of sequence before any parts given");
				break;
			}
			if (pl.group_from != NULL) {
				SAU_ParseEvData *group_to = (pl.composite) ?
					pl.composite :
					pl.event;
				group_to->groupfrom = pl.group_from;
				pl.group_from = NULL;
			}
			end_event(&pl);
			flags &= ~DEFERRED_STEP;
			pl.location = SDPL_IN_NONE;
			break;
		case '}':
			warn_closing_without_opening(sc, '}', '{');
			break;
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
	if (newscope > SCOPE_TOP)
		warn_eof_without_closing(sc, ']');
RETURN:
	end_scope(&pl);
	--o->call_level;
	/* Should return from calling scope if/when parent scope is ended. */
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
	if (!SAU_Scanner_open(sc, script, is_path))
		return NULL;
	parse_level(o, NULL, SAU_SDLT_GRAPH, SCOPE_TOP);
	name = sc->f->path;
	SAU_Scanner_close(sc);
	return name;
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SAU_Parse* SAU_create_Parse(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SAU_Parser pr;
	if (!init_Parser(&pr))
		return NULL;
	SAU_Parse *o = NULL;
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	o = SAU_MemPool_alloc(pr.mp, sizeof(SAU_Parse));
	o->events = pr.first_ev;
	o->name = name;
	o->sopt = pr.sl.sopt;
	o->symtab = pr.st;
	o->mem = pr.mp;
	pr.st = NULL; // keep for result
	pr.mp = NULL; // keep for result
DONE:
	fini_Parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Parse(SAU_Parse *restrict o) {
	if (!o)
		return;
	SAU_destroy_SymTab(o->symtab);
	SAU_destroy_MemPool(o->mem);
}
