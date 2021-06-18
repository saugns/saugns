/* ssndgen: Script file parser.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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

#include "symtab.h"
#include "file.h"
#include "../script.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

static uint8_t filter_symchar(SSG_File *restrict f SSG__maybe_unused,
		uint8_t c) {
	return IS_SYMCHAR(c) ? c : 0;
}

/*
 * Parser scanner
 */

typedef struct ScanLookup {
	SSG_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*ramp_names;
} ScanLookup;

typedef struct PScanner {
	SSG_File *f;
	uint32_t line;
	uint8_t c, next_c;
	bool newline;
	bool stash;
	SSG_SymTab *st;
	ScanLookup *sl;
} PScanner;

static void init_scanner(PScanner *restrict o,
		SSG_File *restrict f,
		SSG_SymTab *restrict st,
		ScanLookup *restrict sl) {
	*o = (PScanner){0};
	o->f = f;
	o->line = 1;
	o->st = st;
	o->sl = sl;
}

static void fini_scanner(PScanner *restrict o) {
	SSG_File_close(o->f);
	o->f = NULL; // freed by invoker
}

/*
 * Common warning printing function for script errors; requires that o->c
 * is set to the character where the error was detected.
 */
static void SSG__noinline scan_warning(PScanner *restrict o,
		const char *restrict str) {
	SSG_File *f = o->f;
	uint8_t c = o->c;
	if (IS_VISIBLE(c)) {
		fprintf(stderr, "warning: %s [line %d, at '%c'] - %s\n",
				f->path, o->line, c, str);
	} else if (SSG_File_AT_EOF(f)) {
		fprintf(stderr, "warning: %s [line %d, at EOF] - %s\n",
				f->path, o->line, str);
	} else {
		fprintf(stderr, "warning: %s [line %d, at 0x%02hhX] - %s\n",
				f->path, o->line, c, str);
	}
}

#define SCAN_NEWLINE '\n'
static uint8_t scan_getc(PScanner *restrict o) {
	SSG_File *f = o->f;
	uint8_t c;
	if (o->newline) {
		++o->line;
		o->newline = false;
	}
	SSG_File_skipspace(f);
	if (o->stash) {
		o->stash = false;
		c = o->next_c;
	} else {
		c = SSG_File_GETC(f);
	}
	if (c == '#') {
		SSG_File_skipline(f);
		c = SSG_File_GETC(f);
	}
	if (c == '\n') {
		SSG_File_TRYC(f, '\r');
		c = SCAN_NEWLINE;
		o->newline = true;
	} else if (c == '\r') {
		c = SCAN_NEWLINE;
		o->newline = true;
	} else {
		SSG_File_skipspace(f);
	}
	o->c = c;
	return c;
}

static bool scan_stashc(PScanner *restrict o, uint8_t c) {
	if (o->stash) {
		SSG_warning("PScanner", "only one stashed character supported");
		return false;
	}
	o->next_c = c;
	o->stash = true;
	return true;
}

static void scan_ws(PScanner *restrict o) {
	SSG_File *f = o->f;
	for (;;) {
		uint8_t c = SSG_File_GETC(f);
		if (IS_SPACE(c))
			continue;
		if (c == '\n') {
			++o->line;
			SSG_File_TRYC(f, '\r');
		} else if (c == '\r') {
			++o->line;
		} else if (c == '#') {
			SSG_File_skipline(f);
			c = SSG_File_GETC(f);
		} else {
			SSG_File_UNGETC(f);
			break;
		}
	}
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool handle_unknown_or_end(PScanner *restrict o) {
	SSG_File *f = o->f;
	if (SSG_File_AT_EOF(f) || SSG_File_AFTER_EOF(f)) {
		return false;
	}
	scan_warning(o, "invalid character");
	return true;
}

typedef float (*NumSym_f)(PScanner *restrict o);

typedef struct NumParser {
	PScanner *sc;
	NumSym_f numsym_f;
	bool has_infnum;
} NumParser;
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
	PScanner *sc = o->sc;
	SSG_File *f = sc->f;
	double num;
	bool minus = false;
	uint8_t c;
	if (level > 0) scan_ws(sc);
	c = SSG_File_GETC(f);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		scan_ws(sc);
		c = SSG_File_GETC(f);
	}
	if (c == '(') {
		num = scan_num_r(o, 255, level+1);
		if (minus) num = -num;
		if (level == 0) return num;
		goto EVAL;
	}
	if (o->numsym_f && IS_ALPHA(c)) {
		SSG_File_UNGETC(f);
		num = o->numsym_f(sc);
		if (isnan(num))
			return NAN;
		if (minus) num = -num;
	} else {
		size_t read_len;
		SSG_File_UNGETC(f);
		SSG_File_getd(f, &num, false, &read_len);
		if (read_len == 0)
			return NAN;
		if (minus) num = -num;
	}
EVAL:
	if (pri == 0)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		if (level > 0) scan_ws(sc);
		c = SSG_File_GETC(f);
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
			if (pri <= 2) goto DEFER;
			num += scan_num_r(o, 2, level);
			break;
		case '-':
			if (pri <= 2) goto DEFER;
			num -= scan_num_r(o, 2, level);
			break;
		default:
			if (pri == 255) {
				scan_warning(sc,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SSG_File_UNGETC(f);
	return num;
}
static bool SSG__noinline scan_num(PScanner *restrict o,
		NumSym_f scan_numsym, float *restrict var) {
	NumParser np = {o, scan_numsym, false};
	float num = scan_num_r(&np, 0, 0);
	if (isnan(num))
		return false;
	if (isinf(num)) np.has_infnum = true;
	if (np.has_infnum) {
		scan_warning(o, "discarding expression with infinite number");
		return false;
	}
	*var = num;
	return true;
}

#define OCTAVES 11
static float scan_note(PScanner *restrict o) {
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
	SSG_File *f = o->f;
	float freq;
	o->c = SSG_File_GETC(f);
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	size_t read_len;
	if (o->c >= 'a' && o->c <= 'g') {
		subnote = o->c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		o->c = SSG_File_GETC(f);
	}
	if (o->c < 'A' || o->c > 'G') {
		scan_warning(o,
"invalid note specified - should be C, D, E, F, G, A or B");
		return NAN;
	}
	note = o->c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	o->c = SSG_File_GETC(f);
	if (o->c == 's')
		semitone = 2;
	else if (o->c == 'f')
		semitone = 0;
	else
		SSG_File_UNGETC(f);
	SSG_File_geti(f, &octave, false, &read_len);
	if (read_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		scan_warning(o,
"invalid octave specified for note - valid range 0-10");
		octave = 4;
	}
	freq = o->sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	return freq;
}

#define LABEL_BUFLEN 80
#define LABEL_MAXLEN_S "79"
typedef char LabelBuf[LABEL_BUFLEN];
static size_t scan_label(PScanner *restrict o, LabelBuf label, char op) {
	char nolabel_msg[] = "ignoring ? without label name";
	SSG_File *f = o->f;
	size_t len = 0;
	bool truncated;
	nolabel_msg[9] = op; /* replace ? */
	truncated = !SSG_File_getstr(f, label, LABEL_BUFLEN, &len, filter_symchar);
	if (len == 0) {
		scan_warning(o, nolabel_msg);
	}
	if (truncated) {
		o->c = SSG_File_RETC(f);
		scan_warning(o,
"limiting label name to "LABEL_MAXLEN_S" characters");
		SSG_File_skipstr(f, filter_symchar);
	}
	o->c = SSG_File_RETC(f);
	return len;
}

static bool scan_symafind(PScanner *restrict o,
		const char *const*restrict stra,
		size_t n, size_t *restrict found_i) {
	LabelBuf buf;
	SSG_File *f = o->f;
	size_t len = 0;
	bool truncated;
	truncated = !SSG_File_getstr(f, buf, LABEL_BUFLEN, &len, filter_symchar);
	if (len == 0) {
		scan_warning(o, "label missing");
		return false;
	}
	if (truncated) {
		scan_warning(o,
"limiting label name to "LABEL_MAXLEN_S" characters");
		SSG_File_skipstr(f, filter_symchar);
	}
	const char *key = SSG_SymTab_pool_str(o->st, buf, len);
	for (size_t i = 0; i < n; ++i) {
		if (stra[i] == key) {
			o->c = SSG_File_RETC(f);
			*found_i = i;
			return true;
		}
	}
	return false;
}

static bool scan_wavetype(PScanner *restrict o, size_t *restrict found_id) {
	const char *const *names = o->sl->wave_names;
	if (scan_symafind(o, names, SSG_WAVE_TYPES, found_id))
		return true;

	scan_warning(o, "invalid wave type; available types are:");
	size_t i = 0;
	fprintf(stderr, "\t%s", names[i]);
	while (++i < SSG_WAVE_TYPES) {
		fprintf(stderr, ", %s", names[i]);
	}
	putc('\n', stderr);
	return false;
}

static bool scan_ramp_state(PScanner *restrict o,
		NumSym_f scan_numsym,
		SSG_Ramp *restrict ramp, bool mult) {
	if (!scan_num(o, scan_numsym, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SSG_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SSG_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SSG_RAMPP_STATE;
	return true;
}

static bool scan_ramp(PScanner *restrict o, NumSym_f scan_numsym,
		SSG_Ramp *restrict ramp, bool mult) {
	bool goal = false;
	bool time_set = (ramp->flags & SSG_RAMPP_TIME) != 0;
	float vt;
	uint32_t time_ms = o->sl->sopt.def_time_ms;
	uint8_t type = ramp->type; // has default
	if ((ramp->flags & SSG_RAMPP_GOAL) != 0) {
		// allow partial change
		if (((ramp->flags & SSG_RAMPP_GOAL_RATIO) != 0) == mult) {
			goal = true;
			vt = ramp->vt;
		}
		time_ms = ramp->time_ms;
	}
	for (;;) {
		uint8_t c = scan_getc(o);
		switch (c) {
		case SCAN_NEWLINE:
			break;
		case 'c': {
			const char *const *names = o->sl->ramp_names;
			size_t id;
			if (!scan_symafind(o, names, SSG_RAMP_TYPES, &id)) {
				scan_warning(o,
"invalid ramp curve; available types are:");
				size_t i = 0;
				fprintf(stderr, "\t%s", names[i]);
				while (++i < SSG_RAMP_TYPES) {
					fprintf(stderr, ", %s", names[i]);
				}
				putc('\n', stderr);
				break;
			}
			type = id;
			break; }
		case 't': {
			float time;
			if (scan_num(o, 0, &time)) {
				if (time < 0.f) {
					scan_warning(o,
"ignoring 't' with sub-zero time");
					break;
				}
				time_ms = lrint(time * 1000.f);
				time_set = true;
			}
			break; }
		case 'v':
			if (scan_num(o, scan_numsym, &vt))
				goal = true;
			break;
		case '}':
			goto RETURN;
		default:
			if (!handle_unknown_or_end(o)) goto FINISH;
			break;
		}
	}
FINISH:
	scan_warning(o, "end of file without closing '}'");
RETURN:
	if (!goal) {
		scan_warning(o, "ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->type = type;
	ramp->flags |= SSG_RAMPP_GOAL;
	if (mult)
		ramp->flags |= SSG_RAMPP_GOAL_RATIO;
	else
		ramp->flags &= ~SSG_RAMPP_GOAL_RATIO;
	if (time_set)
		ramp->flags |= SSG_RAMPP_TIME;
	else
		ramp->flags &= ~SSG_RAMPP_TIME;
	return true;
}

/*
 * Parser
 */

typedef struct SSG_Parser {
	PScanner sc;
	ScanLookup sl;
	SSG_SymTab *st;
	uint32_t call_level;
	uint32_t scope_id;
	/* node state */
	SSG_ScriptEvData *events;
	SSG_ScriptEvData *last_event;
} SSG_Parser;

/*
 * Default script options, used until changed in a script.
 */
static const SSG_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
};

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 */
static void init_parser(SSG_Parser *restrict o) {
	*o = (SSG_Parser){0};
	o->st = SSG_create_SymTab();
	o->sl.sopt = def_sopt;
	o->sl.wave_names = SSG_SymTab_pool_stra(o->st,
			SSG_Wave_names, SSG_WAVE_TYPES);
	o->sl.ramp_names = SSG_SymTab_pool_stra(o->st,
			SSG_Ramp_names, SSG_RAMP_TYPES);
}

/*
 * Finalize parser instance.
 */
static void fini_parser(SSG_Parser *restrict o) {
	SSG_destroy_SymTab(o->st);
}

/*
 * Scope values.
 */
enum {
	SCOPE_TOP = 0,
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
	SSG_Parser *o;
	struct ParseLevel *parent;
	uint32_t pl_flags;
	uint8_t location;
	uint8_t scope;
	SSG_ScriptEvData *event, *last_event;
	SSG_ScriptOpData *operator, *first_operator, *last_operator;
	SSG_ScriptOpData *parent_on, *on_prev;
	uint8_t linktype;
	uint8_t last_linktype; /* FIXME: kludge */
	const char *set_label; /* label assigned to next node */
	/* timing/delay */
	SSG_ScriptEvData *group_from; /* where to begin for group_events() */
	SSG_ScriptEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
} ParseLevel;

static bool parse_waittime(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	/* FIXME: ADD_WAIT_DURATION */
	if (SSG_File_TRYC(f, 't')) {
		if (!pl->last_operator) {
			scan_warning(sc,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SSG_SDEV_ADD_WAIT_DURATION;
	} else {
		float wait;
		uint32_t wait_ms;
		scan_num(sc, 0, &wait);
		if (wait < 0.f) {
			scan_warning(sc, "ignoring '\\' with sub-zero time");
			return false;
		}
		wait_ms = lrint(wait * 1000.f);
		pl->next_wait_ms += wait_ms;
	}
	return true;
}

/*
 * Node- and scope-handling functions
 */

enum {
	/* node list/node link types */
	NL_REFER = 0,
	NL_GRAPH,
	NL_FMODS,
	NL_PMODS,
	NL_AMODS,
};

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SSG_ScriptOpData *restrict op) {
	SSG_PtrList_clear(&op->on_next);
	size_t i;
	SSG_ScriptOpData **ops;
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SSG_PtrList_clear(&op->fmods);
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SSG_PtrList_clear(&op->pmods);
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		destroy_operator(ops[i]);
	}
	SSG_PtrList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SSG_ScriptEvData *restrict e) {
	size_t i;
	SSG_ScriptOpData **ops;
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		destroy_operator(ops[i]);
	}
	SSG_PtrList_clear(&e->operators);
	SSG_PtrList_clear(&e->op_graph);
	free(e);
}

static void end_operator(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_OP;
	SSG_Parser *o = pl->o;
	SSG_ScriptOpData *op = pl->operator;
	if (SSG_Ramp_ENABLED(&op->freq))
		op->op_params |= SSG_POPP_FREQ;
	if (SSG_Ramp_ENABLED(&op->freq2))
		op->op_params |= SSG_POPP_FREQ2;
	if (SSG_Ramp_ENABLED(&op->amp)) {
		op->op_params |= SSG_POPP_AMP;
		if (!(op->op_flags & SSG_SDOP_NESTED)) {
			op->amp.v0 *= o->sl.sopt.ampmult;
			op->amp.vt *= o->sl.sopt.ampmult;
		}
	}
	if (SSG_Ramp_ENABLED(&op->amp2)) {
		op->op_params |= SSG_POPP_AMP2;
		if (!(op->op_flags & SSG_SDOP_NESTED)) {
			op->amp2.v0 *= o->sl.sopt.ampmult;
			op->amp2.vt *= o->sl.sopt.ampmult;
		}
	}
	SSG_ScriptOpData *pop = op->on_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params |= SSG_POPP_ADJCS |
			SSG_POPP_WAVE |
			SSG_POPP_TIME |
			SSG_POPP_SILENCE |
			SSG_POPP_FREQ |
			SSG_POPP_FREQ2 |
			SSG_POPP_PHASE |
			SSG_POPP_AMP |
			SSG_POPP_AMP2;
	} else {
		if (op->wave != pop->wave)
			op->op_params |= SSG_POPP_WAVE;
		/* SSG_TIME set when time set */
		if (op->silence_ms != 0)
			op->op_params |= SSG_POPP_SILENCE;
		/* SSG_PHASE set when phase set */
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static void end_event(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & SDPL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~SDPL_ACTIVE_EV;
	SSG_ScriptEvData *e = pl->event;
	end_operator(pl);
	if (SSG_Ramp_ENABLED(&e->pan))
		e->vo_params |= SSG_PVOP_PAN;
	SSG_ScriptEvData *pve = e->voice_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->ev_flags |= SSG_SDEV_NEW_OPGRAPH;
		e->vo_params |= SSG_PVOP_PAN;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SSG_Parser *o = pl->o;
	SSG_ScriptEvData *e, *pve;
	end_event(pl);
	pl->event = calloc(1, sizeof(SSG_ScriptEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SSG_Ramp_reset(&e->pan);
	if (pl->on_prev != NULL) {
		pve = pl->on_prev->event;
		pve->ev_flags |= SSG_SDEV_VOICE_LATER_USED;
		if (pve->composite != NULL && !is_composite) {
			SSG_ScriptEvData *last_ce;
			for (last_ce = pve->composite; last_ce->next;
					last_ce = last_ce->next) ;
			last_ce->ev_flags |= SSG_SDEV_VOICE_LATER_USED;
		}
		e->voice_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = 0.5f; /* center */
		e->pan.flags |= SSG_RAMPP_STATE;
	}
	if (!pl->group_from)
		pl->group_from = e;
	if (is_composite) {
		if (!pl->composite) {
			pve->composite = e;
			pl->composite = pve;
		} else {
			pve->next = e;
		}
	} else {
		if (!o->events)
			o->events = e;
		else
			o->last_event->next = e;
		o->last_event = e;
		pl->composite = NULL;
	}
	pl->pl_flags |= SDPL_ACTIVE_EV;
}

static void begin_operator(ParseLevel *restrict pl, uint8_t linktype,
		bool is_composite) {
	SSG_Parser *o = pl->o;
	SSG_ScriptEvData *e = pl->event;
	SSG_ScriptOpData *op, *pop = pl->on_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(pl);
	pl->operator = calloc(1, sizeof(SSG_ScriptOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	SSG_Ramp_reset(&op->freq);
	SSG_Ramp_reset(&op->freq2);
	SSG_Ramp_reset(&op->amp);
	SSG_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		pop->op_flags |= SSG_SDOP_LATER_USED;
		op->on_prev = pop;
		op->op_flags = pop->op_flags &
			(SSG_SDOP_NESTED | SSG_SDOP_MULTIPLE);
		if (is_composite) {
			pop->op_flags |= SSG_SDOP_HAS_COMPOSITE;
			op->op_flags |= SSG_SDOP_TIME_DEFAULT;
			/* default: previous or infinite time */
		}
		op->time_ms = pop->time_ms;
		op->wave = pop->wave;
		op->phase = pop->phase;
		SSG_PtrList_soft_copy(&op->fmods, &pop->fmods);
		SSG_PtrList_soft_copy(&op->pmods, &pop->pmods);
		SSG_PtrList_soft_copy(&op->amods, &pop->amods);
		if ((pl->pl_flags & SDPL_BIND_MULTIPLE) != 0) {
			SSG_ScriptOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time_ms)
					max_time = mpop->time_ms;
				SSG_PtrList_add(&mpop->on_next, op);
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SSG_SDOP_MULTIPLE;
			op->time_ms = max_time;
			pl->pl_flags &= ~SDPL_BIND_MULTIPLE;
		} else {
			SSG_PtrList_add(&pop->on_next, op);
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->op_flags = SSG_SDOP_TIME_DEFAULT;
		/* default: depends on context */
		op->time_ms = o->sl.sopt.def_time_ms;
		if (!(pl->pl_flags & SDPL_NESTED_SCOPE)) {
			op->freq.v0 = o->sl.sopt.def_freq;
		} else {
			op->op_flags |= SSG_SDOP_NESTED;
			op->freq.v0 = o->sl.sopt.def_relfreq;
			op->freq.flags |= SSG_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SSG_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SSG_RAMPP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	if (linktype == NL_REFER ||
			linktype == NL_GRAPH) {
		SSG_PtrList_add(&e->operators, op);
		if (linktype == NL_GRAPH) {
			e->ev_flags |= SSG_SDEV_NEW_OPGRAPH;
			SSG_PtrList_add(&e->op_graph, op);
		}
	} else {
		SSG_PtrList *list = NULL;
		switch (linktype) {
		case NL_FMODS:
			list = &pl->parent_on->fmods;
			break;
		case NL_PMODS:
			list = &pl->parent_on->pmods;
			break;
		case NL_AMODS:
			list = &pl->parent_on->amods;
			break;
		}
		pl->parent_on->op_params |= SSG_POPP_ADJCS;
		SSG_PtrList_add(list, op);
	}
	/*
	 * Assign label. If no new label but previous node
	 * (for a non-composite) has one, update label to
	 * point to new node, but keep pointer in previous node.
	 */
	if (pl->set_label != NULL) {
		SSG_SymTab_set(o->st, pl->set_label, strlen(pl->set_label), op);
		op->label = pl->set_label;
		pl->set_label = NULL;
	} else if (!is_composite && pop != NULL && pop->label != NULL) {
		SSG_SymTab_set(o->st, pop->label, strlen(pop->label), op);
		op->label = pop->label;
	}
	pl->pl_flags |= SDPL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
		SSG_ScriptOpData *restrict previous,
		uint8_t linktype, bool is_composite) {
	pl->on_prev = previous;
	if (!pl->event ||
			pl->location != SDPL_IN_EVENT
			/* previous event implicitly ended */ ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, linktype, is_composite);
	begin_operator(pl, linktype, is_composite);
	pl->last_linktype = linktype; /* FIXME: kludge */
}

static void begin_scope(SSG_Parser *restrict o, ParseLevel *restrict pl,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	*pl = (ParseLevel){0};
	pl->o = o;
	pl->scope = newscope;
	if (parent_pl != NULL) {
		pl->parent = parent_pl;
		pl->pl_flags = parent_pl->pl_flags &
			(SDPL_NESTED_SCOPE | SDPL_BIND_MULTIPLE);
		pl->location = parent_pl->location;
		pl->event = parent_pl->event;
		pl->operator = parent_pl->operator;
		pl->parent_on = parent_pl->parent_on;
		if (newscope == SCOPE_BIND)
			pl->group_from = parent_pl->group_from;
		if (newscope == SCOPE_NEST) {
			pl->pl_flags |= SDPL_NESTED_SCOPE;
			pl->parent_on = parent_pl->operator;
		}
	}
	pl->linktype = linktype;
}

static void end_scope(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	end_operator(pl);
	if (pl->scope == SCOPE_BIND) {
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= SDPL_BIND_MULTIPLE;
			begin_node(pl->parent, pl->first_operator, pl->parent->last_linktype, false);
		}
	} else if (!pl->parent) {
		/*
		 * At end of top scope, ie. at end of script -
		 * end last event and adjust timing.
		 */
		SSG_ScriptEvData *group_to;
		end_event(pl);
		group_to = (pl->composite) ? pl->composite : pl->last_event;
		if (group_to)
			group_to->groupfrom = pl->group_from;
	}
	if (pl->set_label != NULL) {
		scan_warning(&o->sc,
"ignoring label assignment without operator");
	}
}

/*
 * Main parser functions
 */

static bool parse_settings(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	pl->location = SDPL_IN_DEFAULTS;
	for (;;) {
		uint8_t c = scan_getc(sc);
		switch (c) {
		case 'a':
			if (scan_num(sc, NULL, &o->sl.sopt.ampmult)) {
				o->sl.sopt.changed |= SSG_SOPT_AMPMULT;
			}
			break;
		case 'f':
			if (scan_num(sc, scan_note, &o->sl.sopt.def_freq)) {
				o->sl.sopt.changed |= SSG_SOPT_DEF_FREQ;
			}
			break;
		case 'n': {
			float freq;
			if (scan_num(sc, NULL, &freq)) {
				if (freq < 1.f) {
					scan_warning(sc,
"ignoring tuning frequency (Hz) below 1.0");
					break;
				}
				o->sl.sopt.A4_freq = freq;
				o->sl.sopt.changed |= SSG_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &o->sl.sopt.def_relfreq)) {
				o->sl.sopt.changed |= SSG_SOPT_DEF_RATIO;
			}
			break;
		case 't': {
			float time;
			if (scan_num(sc, 0, &time)) {
				if (time < 0.f) {
					scan_warning(sc,
"ignoring 't' with sub-zero time");
					break;
				}
				o->sl.sopt.def_time_ms = lrint(time * 1000.f);
				o->sl.sopt.changed |= SSG_SOPT_DEF_TIME;
			}
			break; }
		default:
		/*UNKNOWN:*/
			scan_stashc(sc, c);
			return true; /* let parse_level() take care of it */
		}
	}
	return false;
}

static void parse_level(SSG_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	SSG_ScriptOpData *op = pl->operator;
	if (SSG_File_TRYC(f, '{')) {
		scan_ramp(sc, NULL, &op->amp, false);
	} else {
		scan_ramp_state(sc, NULL, &op->amp, false);
	}
	if (SSG_File_TRYC(f, ',')) {
		if (SSG_File_TRYC(f, '{')) {
			scan_ramp(sc, NULL, &op->amp2, false);
		} else {
			scan_ramp_state(sc, NULL, &op->amp2, false);
		}
	}
	if (SSG_File_TRYC(f, '~') && SSG_File_TRYC(f, '[')) {
		if (op->amods.count > 0) {
			op->op_params |= SSG_POPP_ADJCS;
			SSG_PtrList_clear(&op->amods);
		}
		parse_level(o, pl, NL_AMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	SSG_ScriptOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SSG_SDOP_NESTED))
		return true; // reject
	NumSym_f numsym_f = rel_freq ? NULL : scan_note;
	if (SSG_File_TRYC(f, '{')) {
		scan_ramp(sc, numsym_f, &op->freq, rel_freq);
	} else {
		scan_ramp_state(sc, numsym_f, &op->freq, rel_freq);
	}
	if (SSG_File_TRYC(f, ',')) {
		if (SSG_File_TRYC(f, '{')) {
			scan_ramp(sc, numsym_f, &op->freq2, rel_freq);
		} else {
			scan_ramp_state(sc, numsym_f, &op->freq2, rel_freq);
		}
	}
	if (SSG_File_TRYC(f, '~') && SSG_File_TRYC(f, '[')) {
		if (op->fmods.count > 0) {
			op->op_params |= SSG_POPP_ADJCS;
			SSG_PtrList_clear(&op->fmods);
		}
		parse_level(o, pl, NL_FMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	SSG_ScriptOpData *op = pl->operator;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SSG_POPP_PHASE;
	}
	if (SSG_File_TRYC(f, '+') && SSG_File_TRYC(f, '[')) {
		if (op->pmods.count > 0) {
			op->op_params |= SSG_POPP_ADJCS;
			SSG_PtrList_clear(&op->pmods);
		}
		parse_level(o, pl, NL_PMODS, SCOPE_NEST);
	}
	return false;
}

static bool parse_step(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	SSG_ScriptEvData *e = pl->event;
	SSG_ScriptOpData *op = pl->operator;
	pl->location = SDPL_IN_EVENT;
	for (;;) {
		uint8_t c = scan_getc(sc);
		switch (c) {
		case 'P':
			if ((pl->pl_flags & SDPL_NESTED_SCOPE) != 0)
				goto UNKNOWN;
			if (SSG_File_TRYC(f, '{')) {
				scan_ramp(sc, NULL, &e->pan, false);
			} else {
				scan_ramp_state(sc, NULL, &e->pan, false);
			}
			break;
		case '\\':
			if (parse_waittime(pl)) {
				// FIXME: Buggy update node handling
				// for carriers etc. if enabled.
				//begin_node(pl, pl->operator, NL_REFER, false);
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
		case 's': {
			float silence;
			scan_num(sc, 0, &silence);
			if (silence < 0.f) {
				scan_warning(sc,
"ignoring 's' with sub-zero time");
				break;
			}
			op->silence_ms = lrint(silence * 1000.f);
			break; }
		case 't':
			if (SSG_File_TRYC(f, '*')) {
				op->op_flags |= SSG_SDOP_TIME_DEFAULT;
				/* later fitted or kept to default */
				op->time_ms = o->sl.sopt.def_time_ms;
			} else if (SSG_File_TRYC(f, 'i')) {
				if (!(op->op_flags & SSG_SDOP_NESTED)) {
					scan_warning(sc,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
				op->time_ms = SSG_TIME_INF;
			} else {
				float time;
				scan_num(sc, 0, &time);
				if (time < 0.f) {
					scan_warning(sc,
"ignoring 't' with sub-zero time");
					break;
				}
				op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
				op->time_ms = lrint(time * 1000.f);
			}
			op->op_params |= SSG_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			break; }
		default:
		UNKNOWN:
			scan_stashc(sc, c);
			return true; /* let parse_level() take care of it */
		}
	}
	return false;
}

enum {
	HANDLE_DEFER = 1<<1,
	DEFERRED_STEP = 1<<2,
	DEFERRED_SETTINGS = 1<<4
};
static void parse_level(SSG_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t linktype, uint8_t newscope) {
	LabelBuf label;
	ParseLevel pl;
	size_t label_len;
	uint8_t flags = 0;
	begin_scope(o, &pl, parent_pl, linktype, newscope);
	++o->call_level;
	PScanner *sc = &o->sc;
	SSG_File *f = sc->f;
	for (;;) {
		uint8_t c = scan_getc(sc);
		switch (c) {
		case SCAN_NEWLINE:
			if (pl.scope == SCOPE_TOP) {
				/*
				 * On top level of script,
				 * each line has a new "subscope".
				 */
				if (o->call_level > 1)
					goto RETURN;
				flags = 0;
				pl.location = SDPL_IN_NONE;
				pl.first_operator = NULL;
			}
			break;
		case '\'':
			/*
			 * Label assignment (set to what follows).
			 */
			if (pl.set_label != NULL) {
				scan_warning(sc,
"ignoring label assignment to label assignment");
				break;
			}
			label_len = scan_label(sc, label, c);
			pl.set_label = SSG_SymTab_pool_str(o->st, label, label_len);
			break;
		case ';':
			if (pl.location == SDPL_IN_DEFAULTS || !pl.event)
				goto INVALID;
			begin_node(&pl, pl.operator, NL_REFER, true);
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break;
		case '@':
			if (SSG_File_TRYC(f, '[')) {
				end_operator(&pl);
				parse_level(o, &pl, pl.linktype, SCOPE_BIND);
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
				scan_warning(sc,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.location = SDPL_IN_NONE;
			label_len = scan_label(sc, label, c);
			if (label_len > 0) {
				SSG_ScriptOpData *ref =
					SSG_SymTab_get(o->st, label, label_len);
				if (!ref)
					scan_warning(sc,
"ignoring reference to undefined label");
				else {
					begin_node(&pl, ref, NL_REFER, false);
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
			begin_node(&pl, 0, pl.linktype, false);
			pl.operator->wave = wave;
			flags = parse_step(&pl) ?
				(HANDLE_DEFER | DEFERRED_STEP) :
				0;
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			flags = parse_settings(&pl) ?
				(HANDLE_DEFER | DEFERRED_SETTINGS) :
				0;
			break;
		case '[':
			scan_warning(sc, "opening '[' out of place");
			break;
		case '\\':
			if (pl.location == SDPL_IN_DEFAULTS ||
					((pl.pl_flags & SDPL_NESTED_SCOPE) != 0
					 && pl.event != NULL))
				goto INVALID;
			parse_waittime(&pl);
			break;
		case ']':
			if (pl.scope == SCOPE_BIND) {
				goto RETURN;
			}
			if (pl.scope == SCOPE_NEST) {
				end_operator(&pl);
				goto RETURN;
			}
			scan_warning(sc, "closing ']' without opening '['");
			break;
		case '{':
			scan_warning(sc, "opening '{' out of place");
			break;
		case '|':
			if (pl.location == SDPL_IN_DEFAULTS ||
					((pl.pl_flags & SDPL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			if (!pl.event) {
				scan_warning(sc,
"no sounds precede time separator");
				break;
			}
			if (pl.group_from != NULL) {
				SSG_ScriptEvData *group_to = (pl.composite) ?
					pl.composite :
					pl.event;
				group_to->groupfrom = pl.group_from;
				pl.group_from = NULL;
			}
			end_event(&pl);
			pl.location = SDPL_IN_NONE;
			break;
		case '}':
			scan_warning(sc, "closing '}' without opening '{'");
			break;
		default:
		INVALID:
			if (!handle_unknown_or_end(sc)) goto FINISH;
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
	if (newscope == SCOPE_NEST || newscope == SCOPE_BIND)
		scan_warning(sc, "end of file without closing ']'s");
RETURN:
	end_scope(&pl);
	--o->call_level;
}

/*
 * Process file.
 *
 * The file is closed after parse,
 * but the SSG_File instance is not destroyed.
 *
 * \return true if completed, false on error preventing parse
 */
static bool parse_file(SSG_Parser *restrict o, SSG_File *restrict f) {
	init_scanner(&o->sc, f, o->st, &o->sl);
	parse_level(o, 0, NL_GRAPH, SCOPE_TOP);
	fini_scanner(&o->sc);
	return true;
}

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SSG_ScriptEvData *restrict to) {
	SSG_ScriptEvData *e, *e_after = to->next;
	size_t i;
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ScriptOpData **ops;
		ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SSG_ScriptOpData *op = ops[i];
			if (wait < op->time_ms)
				wait = op->time_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ScriptOpData **ops;
		ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SSG_ScriptOpData *op = ops[i];
			if ((op->op_flags & SSG_SDOP_TIME_DEFAULT) != 0) {
				op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
				op->time_ms = wait + waitcount;
				/* fill in sensible default time */
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	to->groupfrom = NULL;
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static inline void time_ramp(SSG_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SSG_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static void time_operator(SSG_ScriptOpData *restrict op) {
	SSG_ScriptEvData *e = op->event;
	if ((op->op_flags & (SSG_SDOP_TIME_DEFAULT | SSG_SDOP_NESTED)) ==
			(SSG_SDOP_TIME_DEFAULT | SSG_SDOP_NESTED)) {
		op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
		if (!(op->op_flags & SSG_SDOP_HAS_COMPOSITE))
			op->time_ms = SSG_TIME_INF;
	}
	if (op->time_ms != SSG_TIME_INF) {
		time_ramp(&op->freq, op->time_ms);
		time_ramp(&op->freq2, op->time_ms);
		time_ramp(&op->amp, op->time_ms);
		time_ramp(&op->amp2, op->time_ms);
		if (!(op->op_flags & SSG_SDOP_SILENCE_ADDED)) {
			op->time_ms += op->silence_ms;
			op->op_flags |= SSG_SDOP_SILENCE_ADDED;
		}
	}
	if ((e->ev_flags & SSG_SDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			e->next->wait_ms += op->time_ms;
		e->ev_flags &= ~SSG_SDEV_ADD_WAIT_DURATION;
	}
	size_t i;
	SSG_ScriptOpData **ops;
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		time_operator(ops[i]);
	}
}

static void time_event(SSG_ScriptEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	// e->pan.flags |= SSG_RAMPP_TIME; // TODO: revisit semantics
	size_t i;
	SSG_ScriptOpData **ops;
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		time_operator(ops[i]);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SSG_ScriptEvData *ce = e->composite;
		SSG_ScriptOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = (SSG_ScriptOpData*) SSG_PtrList_GET(&ce->operators, 0),
		ce_op_prev = ce_op->on_prev,
		e_op = ce_op_prev;
		if ((e_op->op_flags & SSG_SDOP_TIME_DEFAULT) != 0)
			e_op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
		for (;;) {
			ce->wait_ms += ce_op_prev->time_ms;
			if ((ce_op->op_flags & SSG_SDOP_TIME_DEFAULT) != 0) {
				ce_op->op_flags &= ~SSG_SDOP_TIME_DEFAULT;
				if ((ce_op->op_flags & (SSG_SDOP_NESTED | SSG_SDOP_HAS_COMPOSITE)) == SSG_SDOP_NESTED)
					ce_op->time_ms = SSG_TIME_INF;
				else
					ce_op->time_ms = ce_op_prev->time_ms
						- ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time_ms == SSG_TIME_INF)
				e_op->time_ms = SSG_TIME_INF;
			else if (e_op->time_ms != SSG_TIME_INF)
				e_op->time_ms += ce_op->time_ms +
					(ce->wait_ms - ce_op_prev->time_ms);
			ce_op->op_params &= ~SSG_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = (SSG_ScriptOpData*)
				SSG_PtrList_GET(&ce->operators, 0);
		}
	}
}

/*
 * Deals with events that are "composite" (attached to a main event as
 * successive "sub-events" rather than part of the big, linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SSG_ScriptEvData *restrict e) {
	SSG_ScriptEvData *ce = e->composite;
	SSG_ScriptEvData *se = e->next, *se_prev = e;
	uint32_t wait_ms = 0;
	uint32_t added_wait_ms = 0;
	while (ce != NULL) {
		if (!se) {
			/*
			 * No more events in the ordinary sequence,
			 * so append all composites.
			 */
			se_prev->next = ce;
			break;
		}
		/*
		 * If several events should pass in the ordinary sequence
		 * before the next composite is inserted, skip ahead.
		 */
		wait_ms += se->wait_ms;
		if (se->next && (wait_ms + se->next->wait_ms)
				<= (ce->wait_ms + added_wait_ms)) {
			se_prev = se;
			se = se->next;
			continue;
		}
		/*
		 * Insert next composite before or after
		 * the next event of the ordinary sequence.
		 */
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			SSG_ScriptEvData *ce_next = ce->next;
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
			ce = ce_next;
		} else {
			SSG_ScriptEvData *se_next, *ce_next;
			se_next = se->next;
			ce_next = ce->next;
			ce->wait_ms -= wait_ms;
			added_wait_ms += ce->wait_ms;
			wait_ms = 0;
			se->next = ce;
			ce->next = se_next;
			se_prev = ce;
			se = se_next;
			ce = ce_next;
		}
	}
	e->composite = NULL;
}

/*
 * Post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, this function wouldn't exist, all post-parse processing
 * instead being done when creating the sound generation program.
 */
static void postparse_passes(SSG_Parser *restrict o) {
	SSG_ScriptEvData *e;
	for (e = o->events; e; e = e->next) {
		time_event(e);
		if (e->groupfrom != NULL) group_events(e);
	}
	/*
	 * Must be separated into pass following timing adjustments for events;
	 * otherwise, flattening will fail to arrange events in the correct
	 * order in some cases.
	 */
	for (e = o->events; e; e = e->next) {
		if (e->composite != NULL) flatten_events(e);
	}
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SSG_Script* SSG_load_Script(SSG_File *restrict f) {
	if (!f) return NULL;

	SSG_Parser pr;
	init_parser(&pr);
	const char *name = f->path;
	SSG_Script *o = NULL;
	if (!parse_file(&pr, f)) {
		goto DONE;
	}

	postparse_passes(&pr);
	o = calloc(1, sizeof(SSG_Script));
	o->events = pr.events;
	o->name = name;
	o->sopt = pr.sl.sopt;

DONE:
	fini_parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SSG_discard_Script(SSG_Script *restrict o) {
	SSG_ScriptEvData *e;
	for (e = o->events; e; ) {
		SSG_ScriptEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
