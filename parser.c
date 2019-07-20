/* sgensys: Script parser module.
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

#include "scanner.h"
#include "script.h"
#include "help.h"
#include "math.h"
#include "arrtype.h"
#include <string.h>
#include <stdlib.h>
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
	SGS_SYM_VAR = 0,
	SGS_SYM_RAMP_ID,
	SGS_SYM_WAVE_ID,
	SGS_SYM_TYPES
};

static const char *const scan_sym_labels[SGS_SYM_TYPES] = {
	"variable",
	"ramp type",
	"wave type",
};

struct ScanLookup {
	SGS_ScriptOptions sopt;
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
};

static bool init_ScanLookup(struct ScanLookup *restrict o,
		SGS_Symtab *restrict st) {
	o->sopt = def_sopt;
	if (!SGS_Symtab_add_stra(st, SGS_Ramp_names, SGS_RAMP_TYPES,
			SGS_SYM_RAMP_ID) ||
	    !SGS_Symtab_add_stra(st, SGS_Wave_names, SGS_WAVE_TYPES,
			SGS_SYM_WAVE_ID))
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
	const char *warn_str = SGS_IS_ASCIIVISIBLE(c) ?
		(IS_UPPER(c) ?
		"invalid or misplaced typename '%c'" :
		(IS_LOWER(c) ?
		"invalid or misplaced subname '%c'" :
		"misplaced or unrecognized '%c'")) :
		"invalid character (value 0x%02hhX)";
	SGS_Scanner_warning(o, NULL, warn_str, c);
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

static SGS_Symitem *scan_sym(SGS_Scanner *restrict o, uint32_t type_id,
		const char *const*restrict help_stra) {
	const char *type_label = scan_sym_labels[type_id];
	SGS_ScanFrame sf_begin = o->sf;
	SGS_Symstr *s = NULL;
	SGS_Scanner_get_symstr(o, &s);
	if (!s) {
		SGS_Scanner_warning(o, NULL, "%s name missing", type_label);
		return NULL;
	}
	SGS_Symitem *item = SGS_Symtab_find_item(o->symtab, s, type_id);
	if (!item && type_id == SGS_SYM_VAR)
		item = SGS_Symtab_add_item(o->symtab, s, SGS_SYM_VAR);
	if (!item && help_stra != NULL) {
		SGS_Scanner_warning(o, &sf_begin,
				"invalid %s name '%s'; available are:",
				type_label, s->key);
		SGS_print_names(help_stra, "\t", stderr);
		return NULL;
	}
	return item;
}

typedef double (*NumSym_f)(SGS_Scanner *restrict o);

struct NumParser {
	SGS_Scanner *sc;
	NumSym_f numsym_f;
	SGS_ScanFrame sf_start;
	bool has_infnum;
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
		num = scan_num_r(o, NUMEXP_ADT, level);
		if (isnan(num)) goto DEFER;
		if (c == '-') num = -num;
	} else if (c == '$') {
		SGS_Symitem *var = scan_sym(sc, SGS_SYM_VAR, NULL);
		if (!var) goto REJECT;
		if (var->data_use != SGS_SYM_DATA_NUM) {
			SGS_Scanner_warning(sc, NULL,
"variable '$%s' in numerical expression doesn't hold a number", var->sstr->key);
			goto REJECT;
		}
		num = var->data.num;
	} else if (o->numsym_f && IS_ALPHA(c)) {
		SGS_Scanner_ungetc(sc);
		num = o->numsym_f(sc);
		if (isnan(num)) goto REJECT;
	} else {
		size_t read_len;
		SGS_Scanner_ungetc(sc);
		SGS_Scanner_getd(sc, &num, false, &read_len);
		if (read_len == 0) goto REJECT;
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
		if (isnan(num)) goto DEFER;
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
		NumSym_f scan_numsym, double *restrict var) {
	struct NumParser np = {o, scan_numsym, o->sf, false, false};
	double num = scan_num_r(&np, NUMEXP_SUB, 0);
	if (isnan(num))
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
	double val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		SGS_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = SGS_ui32rint(val_s * 1000.f);
	return true;
}

#define OCTAVES 11
static double scan_note(SGS_Scanner *restrict o) {
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
	struct ScanLookup *sl = o->data;
	double freq;
	uint8_t c = SGS_Scanner_getc(o);
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	size_t read_len;
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		c = SGS_Scanner_getc(o);
	}
	if (c < 'A' || c > 'G') {
		SGS_Scanner_warning(o, NULL,
"invalid note specified - should be C, D, E, F, G, A or B");
		return NAN;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	c = SGS_Scanner_getc(o);
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else
		SGS_Scanner_ungetc(o);
	SGS_Scanner_geti(o, &octave, false, &read_len);
	if (read_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SGS_Scanner_warning(o, NULL,
"invalid octave specified for note - valid range 0-10");
		octave = 4;
	}
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

static bool scan_wavetype(SGS_Scanner *restrict o, size_t *restrict found_id) {
	SGS_Symitem *sym = scan_sym(o, SGS_SYM_WAVE_ID, SGS_Wave_names);
	if (!sym)
		return false;
	*found_id = sym->data.id;
	return true;
}

static bool scan_ramp_state(SGS_Scanner *restrict o,
		NumSym_f scan_numsym,
		SGS_Ramp *restrict ramp, bool ratio) {
	double v0;
	if (!scan_num(o, scan_numsym, &v0))
		return false;
	ramp->v0 = v0;
	if (ratio) {
		ramp->flags |= SGS_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SGS_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SGS_RAMPP_STATE;
	return true;
}

/*
 * Parser
 */

struct NestScope {
	SGS_ScriptListData *list;
	SGS_ScriptOpData *last_op;
	SGS_ScriptOptions sopt_save; /* save/restore on nesting */
	/* values passed for outer parameter */
	SGS_Ramp *op_sweep;
	NumSym_f numsym_f;
	bool num_ratio;
};

sgsArrType(NestArr, struct NestScope, )

typedef struct SGS_Parser {
	struct ScanLookup sl;
	SGS_Scanner *sc;
	SGS_Symtab *st;
	SGS_Mempool *mp;
	NestArr nest;
	/* node state */
	struct ParseLevel *cur_pl;
	SGS_ScriptEvData *events;
	SGS_ScriptEvData *last_event;
	SGS_ScriptEvData *group_start, *group_end;
} SGS_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SGS_Parser *restrict o) {
	SGS_destroy_Scanner(o->sc);
	SGS_destroy_Mempool(o->mp);
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
static bool init_Parser(SGS_Parser *restrict o) {
	SGS_Mempool *mp = SGS_create_Mempool(0);
	SGS_Symtab *st = SGS_create_Symtab(mp);
	SGS_Scanner *sc = SGS_create_Scanner(st);
	*o = (SGS_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mp = mp;
	if (!sc) goto ERROR;
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
	SCOPE_SAME = 0, // specially handled inner copy of parent scope (unused)
	SCOPE_GROUP,    // '{...}' or top scope
	SCOPE_BIND,     // '@[...]'
	SCOPE_NEST,     // '[...]'
};

typedef void (*ParseLevel_sub_f)(SGS_Parser *restrict o);
static void parse_in_settings(SGS_Parser *restrict o);
static void parse_in_op_step(SGS_Parser *restrict o);
static void parse_in_par_sweep(SGS_Parser *restrict o);

/*
 * Parse level flags.
 */
enum {
	PL_BIND_MULTIPLE  = 1<<0, // previous node interpreted as set of nodes
	PL_NEW_EVENT_FORK = 1<<1,
	PL_OWN_EV         = 1<<2,
	PL_OWN_OP         = 1<<3,
	PL_SET_SWEEP      = 1<<4, /* parameter sweep set in subscope */
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
	SGS_ScriptEvData *event;
	SGS_ScriptOpData *operator, *ev_last;
	SGS_ScriptOpData *parent_on, *on_prev;
	SGS_Symitem *set_var;
	/* timing/delay */
	SGS_ScriptEvData *main_ev; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
	float used_ampmult; /* update on node creation */
};

typedef struct SGS_ScriptEvBranch {
	SGS_ScriptEvData *events;
	struct SGS_ScriptEvBranch *prev;
} SGS_ScriptEvBranch;

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
	if (!(pl->pl_flags & PL_OWN_OP))
		return;
	pl->pl_flags &= ~PL_OWN_OP;
	SGS_ScriptOpData *op = pl->operator;
	if (SGS_Ramp_ENABLED(&op->freq))
		op->op_params |= SGS_POPP_FREQ;
	if (SGS_Ramp_ENABLED(&op->freq2))
		op->op_params |= SGS_POPP_FREQ2;
	if (SGS_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SGS_POPP_AMP;
		op->amp.v0 *= pl->used_ampmult;
		op->amp.vt *= pl->used_ampmult;
	}
	if (SGS_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SGS_POPP_AMP2;
		op->amp2.v0 *= pl->used_ampmult;
		op->amp2.vt *= pl->used_ampmult;
	}
	SGS_ScriptOpData *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params = SGS_POP_PARAMS;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SGS_POPP_WAVE;
		/* SGS_TIME set when time set */
		/* SGS_PHASE set when phase set */
	}
	pl->operator = NULL;
}

static void end_event(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	if (!(pl->pl_flags & PL_OWN_EV))
		return;
	pl->pl_flags &= ~PL_OWN_EV;
	SGS_ScriptEvData *e = pl->event;
	end_operator(o);
	pl->ev_last = NULL;
	if (SGS_Ramp_ENABLED(&e->pan))
		e->vo_params |= SGS_PVOP_PAN;
	SGS_ScriptEvData *pve = e->voice_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
		e->vo_params = SGS_PVO_PARAMS & ~SGS_PVOP_OPLIST;
	}
	pl->event = NULL;
}

static void begin_event(SGS_Parser *restrict o, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e, *pve;
	end_event(o);
	pl->event = SGS_mpalloc(o->mp, sizeof(SGS_ScriptEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SGS_Ramp_reset(&e->pan);
	if (pl->on_prev != NULL) {
		SGS_ScriptEvBranch *fork;
		if (pl->on_prev->op_flags & SGS_SDOP_NESTED)
			e->ev_flags |= SGS_SDEV_IMPLICIT_TIME;
		pve = pl->on_prev->event;
		pve->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
		fork = pve->forks;
		if (is_compstep) {
			if (pl->pl_flags & PL_NEW_EVENT_FORK) {
				if (!pl->main_ev)
					pl->main_ev = pve;
				else
					fork = pl->main_ev->forks;
				pl->main_ev->forks = calloc(1,
						sizeof(SGS_ScriptEvBranch));
				pl->main_ev->forks->events = e;
				pl->main_ev->forks->prev = fork;
				pl->pl_flags &= ~PL_NEW_EVENT_FORK;
			} else {
				pve->next = e;
			}
		} else while (fork != NULL) {
			SGS_ScriptEvData *last_ce;
			for (last_ce = fork->events; last_ce->next;
					last_ce = last_ce->next) ;
			last_ce->ev_flags |= SGS_SDEV_VOICE_LATER_USED;
			fork = fork->prev;
		}
		e->voice_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SGS_RAMPP_STATE;
	}
	if (!is_compstep) {
		if (!o->events)
			o->events = e;
		else
			o->last_event->next = e;
		o->last_event = e;
		pl->main_ev = NULL;
	}
	SGS_ScriptEvData *group_e = (pl->main_ev != NULL) ? pl->main_ev : e;
	if (!o->group_start)
		o->group_start = group_e;
	o->group_end = group_e;
	pl->pl_flags |= PL_OWN_EV;
}

static void begin_operator(SGS_Parser *restrict o, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptEvData *e = pl->event;
	SGS_ScriptOpData *op, *pop = pl->on_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(o);
	pl->operator = SGS_mpalloc(o->mp, sizeof(SGS_ScriptOpData));
	op = pl->operator;
	if (!is_compstep)
		pl->pl_flags |= PL_NEW_EVENT_FORK;
	pl->used_ampmult = o->sl.sopt.ampmult;
	/*
	 * Initialize node.
	 */
	SGS_Ramp_reset(&op->freq);
	SGS_Ramp_reset(&op->freq2);
	SGS_Ramp_reset(&op->amp);
	SGS_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		pop->op_flags |= SGS_SDOP_LATER_USED;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SGS_SDOP_NESTED | SGS_SDOP_MULTIPLE);
		op->time = (SGS_Time){pop->time.v_ms,
			(pop->time.flags & SGS_TIMEP_IMPLICIT)};
		op->wave = pop->wave;
		op->phase = pop->phase;
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			SGS_ScriptOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
			} while ((mpop = mpop->next) != NULL);
			op->op_flags |= SGS_SDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~PL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->time = (SGS_Time){o->sl.sopt.def_time_ms, 0};
		if (pl->use_type == SGS_POP_CARR) {
			op->freq.v0 = o->sl.sopt.def_freq;
		} else {
			op->op_flags |= SGS_SDOP_NESTED;
			op->freq.v0 = o->sl.sopt.def_relfreq;
			op->freq.flags |= SGS_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SGS_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SGS_RAMPP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	struct NestScope *nest = NestArr_tip(&o->nest);
	if (pop || !nest) {
		if (!e->operators.first_on)
			e->operators.first_on = op;
		else
			pl->ev_last->next = op;
		pl->ev_last = op;
		if (!pop) {
			e->ev_flags |= SGS_SDEV_NEW_OPGRAPH;
			if (!e->op_graph.first_on)
				e->op_graph.first_on = op;
			++e->op_graph.count;
		}
	} else {
		if (!nest->list->first_on)
			nest->list->first_on = op;
		else
			nest->last_op->next = op;
		nest->last_op = op;
		++nest->list->count;
	}
	/*
	 * Assign to variable?
	 */
	if (pl->set_var != NULL) {
		pl->set_var->data_use = SGS_SYM_DATA_OBJ;
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
static void begin_node(SGS_Parser *restrict o,
		SGS_ScriptOpData *restrict previous, bool is_compstep) {
	struct ParseLevel *pl = o->cur_pl;
	pl->on_prev = previous;
	if (!pl->event || pl->next_wait_ms > 0 ||
			/* previous event implicitly ended */
			(previous || pl->use_type <= SGS_POP_CARR) ||
			is_compstep)
		begin_event(o, is_compstep);
	begin_operator(o, is_compstep);
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
		pl->parent_on = parent_pl->parent_on;
		if (newscope == SCOPE_BIND) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = SGS_mpalloc(o->mp, sizeof(*nest->list));
			pl->parent_on = parent_pl->operator;
			pl->sub_f = NULL;
		} else if (newscope == SCOPE_NEST) {
			struct NestScope *nest = NestArr_tip(&o->nest);
			nest->list = SGS_mpalloc(o->mp, sizeof(*nest->list));
			pl->parent_on = parent_pl->operator;
			pl->sub_f = nest->op_sweep ? parse_in_par_sweep : NULL;
			SGS_ScriptListData **list = NULL;
			switch (use_type) {
			case SGS_POP_AMOD: list = &pl->parent_on->amods; break;
			case SGS_POP_FMOD: list = &pl->parent_on->fmods; break;
			case SGS_POP_PMOD: list = &pl->parent_on->pmods; break;
			}
			if (list) {
				nest->list->prev = *list;
				*list = nest->list;
			}
			/*
			 * Push script options, then prepare for new context.
			 */
			nest->sopt_save = o->sl.sopt;
			o->sl.sopt.set = 0;
			o->sl.sopt.ampmult = def_sopt.ampmult; // new each list
		}
	}
	pl->use_type = use_type;
}

static void leave_level(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	end_operator(o);
	if (pl->set_var != NULL) {
		SGS_Scanner_warning(o->sc, NULL,
				"ignoring variable assignment without object");
	}
	if (!pl->parent) {
		/*
		 * At end of top scope (i.e. at end of script),
		 * end last event and adjust timing.
		 */
		end_event(o);
		flush_durgroup(o);
	}
	if (pl->scope == SCOPE_GROUP) {
		end_event(o);
	} else if (pl->scope == SCOPE_BIND) {
	} else if (pl->scope == SCOPE_NEST) {
		struct NestScope *nest = NestArr_tip(&o->nest);
		pl->parent->pl_flags |= pl->pl_flags & PL_SET_SWEEP;
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
	SGS_Scanner *sc = o->sc; \
	if (!(GuardCond)) { pl->sub_f = NULL; return; } \
	pl->sub_f = (Name); \
	for (;;) { \
		uint8_t c = SGS_Scanner_getc(sc); \
		/* switch (c) { ... default: ... goto DEFER; } */

#define PARSE_IN__TAIL() \
		/* switch (c) { ... default: ... goto DEFER; } */ \
	} \
	return; \
DEFER: \
	SGS_Scanner_ungetc(sc); /* let parse_level() take care of it */

static void parse_in_settings(SGS_Parser *restrict o) {
	PARSE_IN__HEAD(parse_in_settings, true)
		double val;
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
		case 'a':
			if (scan_num(sc, NULL, &val)) {
				o->sl.sopt.ampmult = val;
				o->sl.sopt.set |= SGS_SOPT_AMPMULT;
			}
			break;
		case 'f':
			if (scan_num(sc, scan_note, &val)) {
				o->sl.sopt.def_freq = val;
				o->sl.sopt.set |= SGS_SOPT_DEF_FREQ;
			}
			if (SGS_Scanner_tryc(sc, ',') &&
			    SGS_Scanner_tryc(sc, 'n')) {
				if (scan_num(sc, NULL, &val)) {
					if (val < 1.f) {
						SGS_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
						break;
					}
					o->sl.sopt.A4_freq = val;
					o->sl.sopt.set |= SGS_SOPT_A4_FREQ;
				}
			}
			break;
		case 'r':
			if (scan_num(sc, NULL, &val)) {
				o->sl.sopt.def_relfreq = val;
				o->sl.sopt.set |= SGS_SOPT_DEF_RELFREQ;
			}
			break;
		case 't':
			if (scan_time_val(sc, &o->sl.sopt.def_time_ms))
				o->sl.sopt.set |= SGS_SOPT_DEF_TIME;
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_level(SGS_Parser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c);

static void parse_in_par_sweep(SGS_Parser *restrict o) {
	struct NestScope *nest = NestArr_tip(&o->nest);
	SGS_Ramp *ramp = nest->op_sweep;
	PARSE_IN__HEAD(parse_in_par_sweep, true)
		if (!(pl->pl_flags & PL_SET_SWEEP)) {
			pl->pl_flags |= PL_SET_SWEEP;
			ramp->type = SGS_RAMP_LIN; /* initial default */
		}
		if (!(ramp->flags & SGS_RAMPP_TIME))
			ramp->time_ms = o->sl.sopt.def_time_ms;
		double val;
		switch (c) {
		case 'c': {
			SGS_Symitem *sym = scan_sym(sc, SGS_SYM_RAMP_ID,
					SGS_Ramp_names);
			if (sym) {
				ramp->type = sym->data.id;
			}
			break; }
		case 't':
			if (scan_time_val(sc, &ramp->time_ms))
				ramp->flags |= SGS_RAMPP_TIME;
			break;
		case 'v':
			if (scan_num(sc, nest->numsym_f, &val)) {
				ramp->vt = val;
				ramp->flags |= SGS_RAMPP_GOAL;
				if (nest->num_ratio)
					ramp->flags |= SGS_RAMPP_GOAL_RATIO;
				else
					ramp->flags &= ~SGS_RAMPP_GOAL_RATIO;
			}
			break;
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_par_list(SGS_Parser *restrict o,
		NumSym_f numsym_f,
		SGS_Ramp *restrict op_sweep, bool ratio,
		uint8_t use_type) {
	struct ParseLevel *pl = o->cur_pl;
	struct NestScope *nest = NestArr_add(&o->nest, NULL);
	bool clear = SGS_Scanner_tryc(o->sc, '-');
	bool empty = true;
	nest->op_sweep = op_sweep;
	nest->numsym_f = numsym_f;
	nest->num_ratio = ratio;
	while (SGS_Scanner_tryc(o->sc, '[')) {
		empty = false;
		parse_level(o, use_type, SCOPE_NEST, ']');
		nest = NestArr_tip(&o->nest); // re-get, array may have changed
		if (clear) clear = false;
		else {
			if (nest->list->prev)
				nest->list->count += nest->list->prev->count;
			nest->list->append = true;
		}
	}
	NestArr_drop(&o->nest);
	pl->pl_flags &= ~PL_SET_SWEEP;
	return empty;
}

static bool parse_op_amp(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptOpData *op = pl->operator;
	scan_ramp_state(o->sc, NULL, &op->amp, false);
	parse_par_list(o, NULL, &op->amp, false, 0);
	if (SGS_Scanner_tryc(o->sc, ',') && SGS_Scanner_tryc(o->sc, 'w')) {
		scan_ramp_state(o->sc, NULL, &op->amp2, false);
		parse_par_list(o, NULL, &op->amp2, false, SGS_POP_AMOD);
	}
	return false;
}

static bool parse_op_freq(SGS_Parser *restrict o, bool rel_freq) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SGS_SDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	scan_ramp_state(o->sc, numsym_f, &op->freq, rel_freq);
	parse_par_list(o, numsym_f, &op->freq, rel_freq, 0);
	if (SGS_Scanner_tryc(o->sc, ',') && SGS_Scanner_tryc(o->sc, 'w')) {
		scan_ramp_state(o->sc, numsym_f, &op->freq2, rel_freq);
		parse_par_list(o, numsym_f, &op->freq2, rel_freq, SGS_POP_FMOD);
	}
	return false;
}

static bool parse_op_phase(SGS_Parser *restrict o) {
	struct ParseLevel *pl = o->cur_pl;
	SGS_ScriptOpData *op = pl->operator;
	double val;
	if (scan_num(o->sc, NULL, &val)) {
		op->phase = SGS_cyclepos_dtoui32(val);
		op->op_params |= SGS_POPP_PHASE;
	}
	parse_par_list(o, NULL, NULL, false, SGS_POP_PMOD);
	return false;
}

static void parse_in_op_step(SGS_Parser *restrict o) {
	PARSE_IN__HEAD(parse_in_op_step, pl->operator)
		SGS_ScriptEvData *e = pl->event;
		SGS_ScriptOpData *op = pl->operator;
		switch (c) {
		case SGS_SCAN_SPACE:
			break;
		case 'P':
			if (pl->use_type != SGS_POP_CARR)
				goto DEFER;
			scan_ramp_state(sc, NULL, &e->pan, false);
			parse_par_list(o, NULL, &e->pan, false, 0);
			break;
		case '/':
			if (parse_waittime(o)) {
				// FIXME: Buggy update node handling
				// for carriers etc. if enabled.
				//begin_node(o, pl->operator, false);
			}
			break;
		case '\\':
			if (parse_waittime(o)) {
				begin_node(o, pl->operator, true);
				pl->event->ev_flags |= SGS_SDEV_FROM_GAPSHIFT;
			}
			break;
		case 'a':
			if (parse_op_amp(o)) goto DEFER;
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
			if (SGS_Scanner_tryc(sc, 'd')) {
				op->time = (SGS_Time){o->sl.sopt.def_time_ms,
					0};
			} else if (SGS_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SGS_SDOP_NESTED)) {
					SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) for non-nested operator");
					break;
				}
				op->time = (SGS_Time){o->sl.sopt.def_time_ms,
					SGS_TIMEP_SET | SGS_TIMEP_IMPLICIT};
			} else {
				uint32_t time_ms;
				if (!scan_time_val(sc, &time_ms))
					break;
				op->time = (SGS_Time){time_ms, SGS_TIMEP_SET};
			}
			op->op_params |= SGS_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			break; }
		default:
			goto DEFER;
		}
	PARSE_IN__TAIL()
}

static bool parse_level(SGS_Parser *restrict o,
		uint8_t use_type, uint8_t newscope, uint8_t close_c) {
	struct ParseLevel pl;
	bool endscope = false;
	enter_level(o, &pl, use_type, newscope, close_c);
	SGS_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		/* Use sub-parsing routine? May also happen in nested calls. */
		if (pl.sub_f) pl.sub_f(o);
		c = SGS_Scanner_getc(sc);
		switch (c) {
		case SGS_SCAN_SPACE:
		case SGS_SCAN_LNBRK:
			break;
		case '\'':
			/*
			 * Variable assignment, part 1; set to what follows.
			 */
			if (pl.set_var != NULL) {
				SGS_Scanner_warning(sc, NULL,
"ignoring variable assignment to variable assignment");
				break;
			}
			pl.set_var = scan_sym(sc, SGS_SYM_VAR, NULL);
			break;
		case '/':
			if (pl.use_type != SGS_POP_CARR && pl.event != NULL)
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
			if ((pl.operator->time.flags &
			     (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT)) ==
			    (SGS_TIMEP_SET|SGS_TIMEP_IMPLICIT))
				SGS_Scanner_warning(sc, NULL,
"ignoring 'ti' (implicit time) before ';' separator");
			begin_node(o, pl.operator, true);
			pl.event->ev_flags |= SGS_SDEV_WAIT_PREV_DUR;
			pl.sub_f = parse_in_op_step;
			break;
		case '<':
			warn_opening_disallowed(sc, '<');
			break;
		case '=': {
			SGS_Symitem *var = pl.set_var;
			if (!var) goto INVALID;
			pl.set_var = NULL; // used here
			if (scan_num(sc, NULL, &var->data.num))
				var->data_use = SGS_SYM_DATA_NUM;
			else
				SGS_Scanner_warning(sc, NULL,
"missing right-hand value for \"'%s=\"", var->sstr->key);
			break; }
		case '>':
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@': {
			if (SGS_Scanner_tryc(sc, '[')) {
				end_operator(o);
				NestArr_add(&o->nest, NULL);
				if (parse_level(o, pl.use_type, SCOPE_BIND,']'))
					goto RETURN;
				struct NestScope *nest = NestArr_drop(&o->nest);
				if (!nest || !nest->list->first_on) break;
				pl.pl_flags |= PL_BIND_MULTIPLE;
				begin_node(o, nest->list->first_on, false);
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
			SGS_Symitem *var = scan_sym(sc, SGS_SYM_VAR, NULL);
			if (var != NULL) {
				if (var->data_use == SGS_SYM_DATA_OBJ) {
					SGS_ScriptOpData *ref = var->data.obj;
					begin_node(o, ref, false);
					ref = pl.operator;
					var->data.obj = ref; /* update */
					pl.sub_f = parse_in_op_step;
				} else {
					SGS_Scanner_warning(sc, NULL,
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
				SGS_Scanner_warning(sc, NULL, "modulators not supported here");
				break;
			}
			begin_node(o, 0, false);
			pl.operator->wave = wave;
			pl.sub_f = parse_in_op_step;
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			pl.sub_f = parse_in_settings;
			break;
		case '[':
			warn_opening_disallowed(sc, '[');
			break;
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
			break;
		case '|':
			if (pl.use_type != SGS_POP_CARR && pl.event != NULL)
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
			if (c == close_c) goto RETURN;
			warn_closing_without_opening(sc, '}', '{');
			break;
		default:
		INVALID:
			if (!handle_unknown_or_eof(sc, c)) goto FINISH;
			break;
		}
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
static const char *parse_file(SGS_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SGS_Scanner *sc = o->sc;
	const char *name;
	if (!SGS_Scanner_open(sc, script, is_path)) {
		return NULL;
	}
	parse_level(o, SGS_POP_CARR, SCOPE_GROUP, 0);
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
		for (SGS_ScriptOpData *op = e->operators.first_on; op;
				op = op->next) {
			if (!(op->time.flags & SGS_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = cur_longest + wait_sum;
				op->time.flags |= SGS_TIMEP_SET;
				if (e->dur_ms < op->time.v_ms)
					e->dur_ms = op->time.v_ms;
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
	if (!(ramp->flags & SGS_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static uint32_t time_operator(SGS_ScriptOpData *restrict op) {
	uint32_t dur_ms = op->time.v_ms;
	if (!(op->op_params & SGS_POPP_TIME))
		op->event->ev_flags &= ~SGS_SDEV_VOICE_SET_DUR;
	if (!(op->time.flags & SGS_TIMEP_SET)) {
		op->time.flags |= SGS_TIMEP_DEFAULT;
		if (op->op_flags & SGS_SDOP_NESTED) {
			op->time.flags |= SGS_TIMEP_IMPLICIT;
			op->time.flags |= SGS_TIMEP_SET; /* no durgroup yet */
		}
	}
	if (!(op->time.flags & SGS_TIMEP_IMPLICIT)) {
		time_ramp(&op->freq, op->time.v_ms);
		time_ramp(&op->freq2, op->time.v_ms);
		time_ramp(&op->amp, op->time.v_ms);
		time_ramp(&op->amp2, op->time.v_ms);
	}
	if (op->amods) for (SGS_ScriptOpData *subop = op->amods->first_on;
			subop; subop = subop->next) {
		time_operator(subop);
	}
	if (op->fmods) for (SGS_ScriptOpData *subop = op->fmods->first_on;
			subop; subop = subop->next) {
		time_operator(subop);
	}
	if (op->pmods) for (SGS_ScriptOpData *subop = op->pmods->first_on;
			subop; subop = subop->next) {
		time_operator(subop);
	}
	return dur_ms;
}

static uint32_t time_event(SGS_ScriptEvData *restrict e) {
	uint32_t dur_ms = 0;
	// e->pan.flags |= SGS_RAMPP_TIME; // TODO: revisit semantics
	for (SGS_ScriptOpData *op = e->operators.first_on; op; op = op->next) {
		uint32_t sub_dur_ms = time_operator(op);
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
		SGS_ScriptOpData *ne_op = ne->operators.first_on,
				 *ne_op_prev = ne_op->on_prev,
				 *e_op = ne_op_prev;
		uint32_t first_time_ms = e_op->time.v_ms;
		SGS_Time def_time = {
			e_op->time.v_ms,
			(e_op->time.flags & SGS_TIMEP_IMPLICIT)
		};
		e->dur_ms = first_time_ms; /* for first value in series */
		if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
		for (;;) {
			wait_sum_ms += ne->wait_ms;
			if (!(ne_op->time.flags & SGS_TIMEP_SET)) {
				ne_op->time = def_time;
				if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT)
					ne_op->time.flags |= SGS_TIMEP_SET |
						SGS_TIMEP_DEFAULT;
			}
			time_event(ne);
			def_time = (SGS_Time){
				ne_op->time.v_ms,
				(ne_op->time.flags & SGS_TIMEP_IMPLICIT)
			};
			if (ne->ev_flags & SGS_SDEV_FROM_GAPSHIFT) {
				if (ne_op_prev->time.flags & SGS_TIMEP_DEFAULT
				    && !(ne_prev->ev_flags &
					    SGS_SDEV_FROM_GAPSHIFT)) {
					ne_op_prev->time = (SGS_Time){ // gap
						0,
						SGS_TIMEP_SET|SGS_TIMEP_DEFAULT
					};
				}
			}
			if (ne->ev_flags & SGS_SDEV_WAIT_PREV_DUR) {
				ne->wait_ms += ne_op_prev->time.v_ms;
				ne_op_prev->time.flags &= ~SGS_TIMEP_IMPLICIT;
			}
			if (nest_dur_ms < wait_sum_ms + ne->dur_ms)
				nest_dur_ms = wait_sum_ms + ne->dur_ms;
			first_time_ms += ne->dur_ms +
				(ne->wait_ms - ne_prev->dur_ms);
			ne_op->time.flags |= SGS_TIMEP_SET;
			ne_op->op_params |= SGS_POPP_TIME;
			ne_op_prev = ne_op;
			ne_prev = ne;
			ne = ne->next;
			if (!ne) break;
			ne_op = ne->operators.first_on;
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
	SGS_ScriptEvBranch *fork = e->forks;
	SGS_ScriptEvData *ne = fork->events;
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
	e->forks = fork->prev;
	free(fork);
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SGS_Parser *restrict o) {
	SGS_ScriptEvData *e;
	for (e = o->events; e; e = e->next) {
		if (!(e->ev_flags & SGS_SDEV_IMPLICIT_TIME))
			e->ev_flags |= SGS_SDEV_VOICE_SET_DUR;
		time_event(e);
		if (e->group_backref != NULL) time_durgroup(e);
	}
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (e = o->events; e; e = e->next) {
		while (e->forks != NULL) flatten_events(e);
	}
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SGS_Script* SGS_read_Script(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SGS_Parser pr;
	SGS_Script *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;
	postparse_passes(&pr);
	o = SGS_mpalloc(pr.mp, sizeof(SGS_Script));
	o->mp = pr.mp;
	o->st = pr.st;
	o->events = pr.events;
	o->name = name;
	o->sopt = pr.sl.sopt;
	pr.mp = NULL; // keep with result

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
	SGS_destroy_Mempool(o->mp);
}
