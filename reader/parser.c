/* saugns: Script file parser.
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

typedef struct ScanLookup {
	SAU_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*ramp_names;
} ScanLookup;

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

static bool init_ScanLookup(ScanLookup *restrict o, SAU_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SAU_SymTab_pool_stra(st,
			SAU_Wave_names, SAU_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->ramp_names = SAU_SymTab_pool_stra(st,
			SAU_Ramp_names, SAU_RAMP_TYPES);
	if (!o->ramp_names)
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

typedef struct NumParser {
	SAU_Scanner *sc;
	SAU_ScanNumConst_f numconst_f;
	SAU_ScanFrame sf_start;
	bool has_infnum;
} NumParser;
enum {
	NUMEXP_SUB = 0,
	NUMEXP_ADT,
	NUMEXP_MLT,
	NUMEXP_POW,
	NUMEXP_NUM,
};
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
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else {
		size_t read_len;
		SAU_Scanner_ungetc(sc);
		SAU_Scanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0)
			return NAN;
	}
	if (isnan(num))
		return NAN;
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
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	return num;
}
static bool scan_num(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst, float *restrict var) {
	NumParser np = {o, scan_numconst, o->sf, false};
	uint8_t ws_level = o->ws_level;
	float num = scan_num_r(&np, NUMEXP_NUM, 0);
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

static bool scan_time_val(SAU_Scanner *restrict o,
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
	ScanLookup *sl = o->data;
	size_t len = 0, num_len;
	uint8_t c;
	float freq;
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	++len;
	c = SAU_File_GETC(f);
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		++len;
		c = SAU_File_GETC(f);
	}
	if (c < 'A' || c > 'G') {
		SAU_File_UNGETN(f, len);
		return 0;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	++len;
	c = SAU_File_GETC(f);
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else {
		SAU_File_DECP(f);
		--len;
	}
	SAU_Scanner_geti(o, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SAU_Scanner_warning(o, NULL,
"invalid octave specified for note, using 4 (valid range 0-10)");
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

static bool scan_symafind(SAU_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SAU_ScanFrame sf_begin = o->sf;
	SAU_SymStr *s = NULL;
	SAU_Scanner_get_symstr(o, &s);
	if (!s) {
		SAU_Scanner_warning(o, NULL,
				"%s type value missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SAU_Scanner_warning(o, &sf_begin,
			"invalid %s type value; available are:", print_type);
	SAU_print_names(stra, "\t", stderr);
	return false;
}

static bool scan_wavetype(SAU_Scanner *restrict o, size_t *restrict found_id) {
	ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names,
			found_id, "wave type");
}

static bool scan_fval_param(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		float *restrict fval, bool rel,
		SAU_ParamAttr *restrict attr, uint32_t flag) {
	if (!scan_num(o, scan_numconst, fval))
		return false;
	attr->set |= flag;
	if (rel)
		attr->rel |= flag;
	else
		attr->rel &= ~flag;
	return true;
}

static bool scan_ramp_param(SAU_Scanner *restrict o,
		SAU_ScanNumConst_f scan_numconst,
		SAU_Ramp *restrict ramp, bool rel,
		SAU_ParamAttr *restrict attr, uint32_t flag) {
	if (!SAU_Scanner_tryc(o, '{')) {
		if (!scan_fval_param(o, scan_numconst, &ramp->v0, rel,
					attr, flag))
			return false;
		if (rel) {
			ramp->flags |= SAU_RAMPP_STATE_RATIO;
		} else {
			ramp->flags &= ~SAU_RAMPP_STATE_RATIO;
		}
		ramp->flags |= SAU_RAMPP_STATE;
		return true;
	}
	ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SAU_RAMPP_TIME) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t type = ramp->type; // has default
	if ((ramp->flags & SAU_RAMPP_GOAL) != 0) {
		// allow partial change
		if (((ramp->flags & SAU_RAMPP_GOAL_RATIO) != 0) == rel) {
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
					&id, "ramp curve")) {
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
	if (rel)
		ramp->flags |= SAU_RAMPP_GOAL_RATIO;
	else
		ramp->flags &= ~SAU_RAMPP_GOAL_RATIO;
	if (time_set)
		ramp->flags |= SAU_RAMPP_TIME;
	else
		ramp->flags &= ~SAU_RAMPP_TIME;
	attr->set |= flag;
	if (rel)
		attr->rel |= flag;
	else
		attr->rel &= ~flag;
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
	struct ParseLevel *cur_pl;
	SAU_ParseDurGroup *cur_dur;
	SAU_ParseSublist *events;
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

typedef void (*ParseLevel_sub_f)(SAU_Parser *restrict o);

static void parse_in_event(SAU_Parser *restrict o);
static void parse_in_settings(SAU_Parser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_DEFERRED_SUB  = 1<<0, // \a sub_f exited to attempt handling above
	PL_BIND_MULTIPLE = 1<<1, // previous node interpreted as set of nodes
	PL_NESTED_SCOPE  = 1<<2,
	PL_OWN_SUBLIST   = 1<<3,
	PL_OWN_EVENT     = 1<<4,
	PL_OWN_DATA      = 1<<5,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 * Current instance pointed to by SAU_Parser instance.
 */
struct ParseLevel {
	struct ParseLevel *parent;
	ParseLevel_sub_f sub_f; // identifies "location" and implicit context
	uint32_t pl_flags;
	uint8_t scope;
	SAU_ParseSublist *sublist;
	SAU_ParseEvData *event, *last_event;
	SAU_ParseOpData *operator, *first_operator, *last_operator;
	SAU_ParseOpData *parent_op;
	SAU_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SAU_ParseEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
};

static bool parse_waittime(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SAU_Scanner_tryc(sc, 't')) {
		if (!pl->last_operator) {
			SAU_Scanner_warning(sc, NULL,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SAU_PDEV_ADD_WAIT_DURATION;
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

static void time_event(SAU_ParseEvData *restrict e);
static void time_durgroup(SAU_ParseEvData *restrict e_last);

static void new_durgroup(SAU_Parser *restrict o) {
	SAU_ParseDurGroup *dur = SAU_MemPool_alloc(o->mp,
			sizeof(SAU_ParseDurGroup));
	if (o->cur_dur != NULL)
		o->cur_dur->next = dur;
	o->cur_dur = dur;
}

static void end_ev_opdata(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_DATA))
		return;
	pl->pl_flags &= ~PL_OWN_DATA;
	ScanLookup *sl = &o->sl;
	SAU_ParseOpData *op = pl->operator;
	if (SAU_Ramp_ENABLED(&op->amp)) {
		if (!(op->op_flags & SAU_PDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SAU_Ramp_ENABLED(&op->amp2)) {
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
		op->params.set |= SAU_POP_PARAMS;
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_EVENT))
		return;
	pl->pl_flags &= ~PL_OWN_EVENT;
	SAU_ParseEvData *e = pl->event;
	end_ev_opdata(o);
	pl->last_event = e;
	pl->event = NULL;
	static int count = 0; ++count; printf("end_event() %d\n", count);
}

static void begin_event(SAU_Parser *restrict o,
		SAU_ParseOpData *restrict pop,
		bool is_composite) {
	struct ParseLevel *pl = o->cur_pl;
	end_event(o);
	SAU_ParseEvData *e = SAU_MemPool_alloc(o->mp, sizeof(SAU_ParseEvData));
	pl->event = e;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	if (pop != NULL) {
		SAU_ParseEvData *pope = pop->event;
		if (is_composite) {
			if (!pl->composite) {
				pope->composite = e;
				pl->composite = pope;
			} else {
				pope->next = e;
			}
		}
	}
	if (!is_composite) {
		/*
		 * Append to general list for current parse level.
		 */
		SAU_NodeRange *list = &pl->sublist->range;
		if (!list->first)
			list->first = e;
		else
			((SAU_ParseEvData*) list->last)->next = e;
		list->last = e;
		pl->composite = NULL;
	}
	if (!(pl->pl_flags & PL_NESTED_SCOPE)) {
		SAU_ParseDurGroup *dur = o->cur_dur;
		e->dur = dur;
		if (!dur->range.first)
			dur->range.first = e;
		dur->range.last = (pl->composite != NULL) ? pl->composite : e;
	}
	pl->pl_flags |= PL_OWN_EVENT;
}

static void begin_ev_opdata(SAU_Parser *restrict o,
		SAU_ParseOpData *restrict pop,
		bool is_composite) {
	struct ParseLevel *pl = o->cur_pl;
	ScanLookup *sl = &o->sl;
	SAU_ParseEvData *e = pl->event;
	SAU_ParseOpData *op = SAU_MemPool_alloc(o->mp, sizeof(SAU_ParseOpData));
	pl->operator = op;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	op->time.v_ms = sl->sopt.def_time_ms; /* time is not copied */
	SAU_Ramp_reset(&op->freq);
	SAU_Ramp_reset(&op->freq2);
	SAU_Ramp_reset(&op->amp);
	SAU_Ramp_reset(&op->amp2);
	SAU_Ramp_reset(&op->pan);
	if (pop != NULL) {
		op->root_event = pop->root_event; /* refs keep original root */
		op->use_type = pop->use_type;
		op->prev = pop;
		op->op_flags = pop->op_flags &
			(SAU_PDOP_NESTED | SAU_PDOP_MULTIPLE);
		if (is_composite) {
			pop->op_flags |= SAU_PDOP_HAS_COMPOSITE;
		} else {
			op->time.flags |= SAU_TIMEP_SET;
		}
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			SAU_ParseOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SAU_PDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~PL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->root_event = (pl->parent_op != NULL) ?
			pl->parent_op->event :
			e;
		op->use_type = pl->sublist->use_type;
		if (op->use_type == SAU_POP_CARR) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SAU_PDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SAU_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SAU_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SAU_RAMPP_STATE;
		op->pan.v0 = sl->sopt.def_chanmix;
		op->pan.flags |= SAU_RAMPP_STATE;
	}
	op->event = e;
	e->op_data = op;
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
	pl->pl_flags |= PL_OWN_DATA;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_event().
 */
static void begin_node(SAU_Parser *restrict o,
		SAU_ParseOpData *restrict pop,
		bool is_composite) {
//	struct ParseLevel *pl = o->cur_pl;
//	if (!pl->event || /* not in event parse means event now ended */
//			pl->sub_f != parse_in_event ||
//			pl->next_wait_ms ||
//			pl->event->op_data != NULL ||
//			is_composite)
		begin_event(o, pop, is_composite);
	begin_ev_opdata(o, pop, is_composite);
}

static void begin_sublist(SAU_Parser *restrict o, uint8_t use_type) {
	SAU_ParseSublist *list = SAU_MemPool_alloc(o->mp,
			sizeof(SAU_ParseSublist));
	if (!list)
		return;
	list->use_type = use_type;

	struct ParseLevel *pl = o->cur_pl;
	pl->pl_flags |= PL_OWN_SUBLIST;
	pl->sublist = list;
}

static void end_sublist(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_SUBLIST))
		return;
	pl->pl_flags &= ~PL_OWN_SUBLIST;
	end_event(o);

	SAU_ParseSublist *list = pl->sublist;
	SAU_ParseEvData *e = list->range.first;
	if (!e)
		return;
	SAU_ParseEvData *e_after = ((SAU_ParseEvData*)list->range.last)->next;
	for (; e != e_after; e = e->next) {
		time_event(e);
		if (e->dur != NULL && e == e->dur->range.last){
			time_durgroup(e);
		}
	}
}

static void enter_level(SAU_Parser *restrict o, struct ParseLevel *restrict pl,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel *parent_pl = o->cur_pl;
	*pl = (struct ParseLevel){0};
	pl->scope = newscope;
	o->cur_pl = pl;
	if (!parent_pl) {
		// handle newscope == SCOPE_TOP here
		if (!o->cur_dur) new_durgroup(o);
		begin_sublist(o, use_type);
		return;
	}
	pl->parent = parent_pl;
	pl->pl_flags = parent_pl->pl_flags &
		(PL_NESTED_SCOPE | PL_BIND_MULTIPLE);
	pl->sub_f = parent_pl->sub_f;
	pl->event = parent_pl->event;
	pl->operator = parent_pl->operator;
	pl->parent_op = parent_pl->parent_op;
	switch (newscope) {
	case SCOPE_TOP:
		break; // handled above
	case SCOPE_BLOCK:
		pl->sublist = parent_pl->sublist;
		break;
	case SCOPE_BIND:
		begin_sublist(o, use_type);
		break;
	case SCOPE_NEST:
		pl->pl_flags |= PL_NESTED_SCOPE;
		pl->parent_op = parent_pl->operator;
		begin_sublist(o, use_type);
		break;
	default:
		break;
	}
}

static void leave_level(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (pl->set_label != NULL) {
		SAU_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
	end_sublist(o);
	o->cur_pl = pl->parent;
	switch (pl->scope) {
	case SCOPE_TOP:
		o->events = pl->sublist;
		break;
	case SCOPE_BLOCK:
		if (pl->pl_flags & PL_OWN_EVENT) {
			end_event(o);
			pl->parent->pl_flags |= PL_OWN_EVENT;
			pl->parent->event = pl->event;
		}
		if (pl->last_event != NULL)
			pl->parent->last_event = pl->last_event;
		break;
	case SCOPE_BIND:
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= PL_BIND_MULTIPLE;
			begin_node(o, pl->first_operator, false);
		}
		break;
	case SCOPE_NEST: {
		if (!pl->parent_op)
			break;
		SAU_ParseOpData *parent_op = pl->parent_op;
		if (!parent_op->nest_scopes)
			parent_op->nest_scopes = pl->sublist;
		else
			parent_op->last_nest_scope->next = pl->sublist;
		parent_op->last_nest_scope = pl->sublist;
		break; }
	default:
		break;
	}
}

/*
 * Main parser functions
 */

static void parse_in_settings(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	ScanLookup *sl = &o->sl;
	SAU_Scanner *sc = o->sc;
	pl->sub_f = parse_in_settings;
	uint8_t c;
	for (;;) {
		c = SAU_Scanner_getc(sc);
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
DEFER:
	SAU_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static void parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope);

static bool parse_ev_amp(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
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
	SAU_ParseOpData *op = pl->operator;
	if (op->op_flags & SAU_PDOP_NESTED)
		return true; // reject
	scan_ramp_param(sc, scan_chanmix_const, &op->pan, false,
			&op->params, SAU_POPP_PAN);
	return false;
}

static bool parse_ev_freq(SAU_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SAU_PDOP_NESTED))
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
	SAU_ParseOpData *op = pl->operator;
	if (scan_fval_param(sc, NULL, &op->phase, false,
				&op->params, SAU_POPP_PHASE)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
	}
	if (SAU_Scanner_tryc(sc, '+') && SAU_Scanner_tryc(sc, '[')) {
		parse_level(o, SAU_POP_PMOD, SCOPE_NEST);
	}
	return false;
}

static void parse_in_event(SAU_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SAU_Scanner *sc = o->sc;
	SAU_ParseOpData *op = pl->operator;
	if (!op) {
		SAU_error("parser", "parse_in_event() called for a NULLity");
		return;
	}
	pl->sub_f = parse_in_event;
	uint8_t c;
	for (;;) {
		c = SAU_Scanner_getc(sc);
		switch (c) {
		case SAU_SCAN_SPACE:
			break;
		case '\\':
			if (parse_waittime(o) && pl->event != NULL) {
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
				op->params.set |= SAU_POPP_SILENCE;
			break;
		case 't':
			if (SAU_Scanner_tryc(sc, '*')) {
				/* later fitted or kept to default */
				op->time.v_ms = o->sl.sopt.def_time_ms;
				op->time.flags = 0;
			} else if (SAU_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SAU_PDOP_NESTED)) {
					SAU_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->time.flags |= SAU_TIMEP_SET
					| SAU_TIMEP_LINKED;
			} else {
				if (!scan_time_val(sc, &op->time.v_ms))
					break;
				op->time.flags = SAU_TIMEP_SET;
			}
			op->params.set |= SAU_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			op->params.set |= SAU_POPP_WAVE;
			break; }
		default:
			goto DEFER;
		}
	}
DEFER:
	SAU_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static void parse_level(SAU_Parser *restrict o,
		uint8_t use_type, uint8_t newscope) {
	struct ParseLevel pl;
	SAU_SymStr *label;
	enter_level(o, &pl, use_type, newscope);
	++o->call_level;
	SAU_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		c = SAU_Scanner_getc(sc);
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
				pl.first_operator = NULL;
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
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			begin_node(o, pl.operator, true);
			parse_in_event(o);
			break;
		case '<':
			parse_level(o, use_type, SCOPE_BLOCK);
			break;
		case '>':
			if (pl.scope == SCOPE_BLOCK) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@':
			if (SAU_Scanner_tryc(sc, '[')) {
				end_event(o);
				parse_level(o, use_type, SCOPE_BIND);
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
				SAU_ParseOpData *ref = label->data;
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
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			parse_waittime(o);
			break;
		case ']':
			if (pl.scope > SCOPE_BLOCK) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '{':
			warn_opening_disallowed(sc, '{');
			break;
		case '|':
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			end_event(o);
			if (!o->cur_dur->range.first) {
				SAU_Scanner_warning(sc, NULL,
"no sounds precede time separator");
				break;
			}
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
		/*
		 * Return to any sub-parsing routine.
		 */
		if (pl.sub_f != NULL && !(pl.pl_flags & PL_DEFERRED_SUB))
			pl.sub_f(o);
		pl.pl_flags &= ~PL_DEFERRED_SUB;
	}
FINISH:
	if (newscope > SCOPE_BLOCK)
		warn_eof_without_closing(sc, ']');
	else if (newscope == SCOPE_BLOCK)
		warn_eof_without_closing(sc, '>');
RETURN:
	leave_level(o);
	--o->call_level;
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
static void time_durgroup(SAU_ParseEvData *restrict e_last) {
	SAU_ParseDurGroup *dur = e_last->dur;
	SAU_ParseEvData *e, *e_after = e_last->next;
	uint32_t wait = 0, waitcount = 0;
	for (e = dur->range.first; e != e_after; ) {
		SAU_ParseOpData *op = e->op_data;
		if (op != NULL) {
			if (wait < op->time.v_ms)
				wait = op->time.v_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = dur->range.first; e != e_after; ) {
		SAU_ParseOpData *op = e->op_data;
		if (op != NULL) {
			if (!(op->time.flags & SAU_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = wait + waitcount;
				op->time.flags |= SAU_TIMEP_SET;
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static inline void time_ramp(SAU_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SAU_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static void time_opdata(SAU_ParseOpData *restrict op) {
	SAU_ParseEvData *e = op->event;
	if ((op->op_flags & SAU_PDOP_NESTED) != 0 &&
			!(op->time.flags & SAU_TIMEP_SET)) {
		if (!(op->op_flags & SAU_PDOP_HAS_COMPOSITE))
			op->time.flags |= SAU_TIMEP_LINKED;
		op->time.flags |= SAU_TIMEP_SET;
	}
	if (!(op->time.flags & SAU_TIMEP_LINKED)) {
		time_ramp(&op->freq, op->time.v_ms);
		time_ramp(&op->freq2, op->time.v_ms);
		time_ramp(&op->amp, op->time.v_ms);
		time_ramp(&op->amp2, op->time.v_ms);
		// op->pan.flags |= SAU_RAMPP_TIME; // TODO: revisit semantics
		if (!(op->op_flags & SAU_PDOP_SILENCE_ADDED)) {
			op->time.v_ms += op->silence_ms;
			op->op_flags |= SAU_PDOP_SILENCE_ADDED;
		}
	}
	if ((e->ev_flags & SAU_PDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			e->next->wait_ms += op->time.v_ms;
		e->ev_flags &= ~SAU_PDEV_ADD_WAIT_DURATION;
	}
	for (SAU_ParseSublist *scope = op->nest_scopes;
			scope != NULL; scope = scope->next) {
		SAU_ParseEvData *sub_e = scope->range.first;
		for (; sub_e != NULL; sub_e = sub_e->next) {
			SAU_ParseOpData *sub_op = sub_e->op_data;
			time_opdata(sub_op);
		}
	}
}

static void time_event(SAU_ParseEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	SAU_ParseOpData *op;
	op = e->op_data;
	if (op != NULL) {
		time_opdata(op);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SAU_ParseEvData *ce = e->composite;
		SAU_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = ce->op_data;
		ce_op_prev = ce_op->prev;
		e_op = ce_op_prev;
		e_op->time.flags |= SAU_TIMEP_SET; /* always used from now on */
		for (;;) {
			ce->wait_ms += ce_op_prev->time.v_ms;
			if (!(ce_op->time.flags & SAU_TIMEP_SET)) {
				ce_op->time.flags |= SAU_TIMEP_SET;
				if ((ce_op->op_flags &
(SAU_PDOP_NESTED | SAU_PDOP_HAS_COMPOSITE)) == SAU_PDOP_NESTED)
					ce_op->time.flags |= SAU_TIMEP_LINKED;
				else
					ce_op->time.v_ms = ce_op_prev->time.v_ms
						- ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time.flags & SAU_TIMEP_LINKED)
				e_op->time.flags |= SAU_TIMEP_LINKED;
			else if (!(e_op->time.flags & SAU_TIMEP_LINKED))
				e_op->time.v_ms += ce_op->time.v_ms +
					(ce->wait_ms - ce_op_prev->time.v_ms);
			ce_op->params.set &= ~SAU_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = ce->op_data;
		}
	}
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
	o->events = pr.events->range.first;
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
