/* saugns: Parser output to script data converter.
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

#include "parser.h"
#include <stdlib.h>

/*
 * Script data construction from parse data.
 *
 * Adjust and replace data structures. The per-event
 * operator list becomes flat, with separate lists kept for
 * recursive traversal in scriptconv.
 */

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SAU_ParseEvData *restrict to) {
	SAU_ParseEvData *e, *e_after = to->next;
	size_t i;
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SAU_ParseOpData **ops;
		ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SAU_ParseOpData *op = ops[i];
			if (e->next == e_after &&
i == (e->operators.count - 1) && (op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				/* default for last node in group */
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
			}
			if (wait < op->time_ms)
				wait = op->time_ms;
		}
		e = e->next;
		if (e != NULL) {
			/*wait -= e->wait_ms;*/
			waitcount += e->wait_ms;
		}
	}
	for (e = to->groupfrom; e != e_after; ) {
		SAU_ParseOpData **ops;
		ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SAU_ParseOpData *op = ops[i];
			if ((op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				/* fill in sensible default time */
				op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				op->time_ms = wait + waitcount;
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

static void time_operator(SAU_ParseOpData *restrict op) {
	SAU_ParseEvData *e = op->event;
	if (op->freq.time_ms == SAU_TIME_DEFAULT)
		op->freq.time_ms = op->time_ms;
	if (op->amp.time_ms == SAU_TIME_DEFAULT)
		op->amp.time_ms = op->time_ms;
	if ((op->op_flags & (SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) ==
			(SAU_SDOP_TIME_DEFAULT | SAU_SDOP_NESTED)) {
		op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		op->time_ms = SAU_TIME_INF;
	}
	if (op->time_ms != SAU_TIME_INF &&
			!(op->op_flags & SAU_SDOP_SILENCE_ADDED)) {
		op->time_ms += op->silence_ms;
		op->op_flags |= SAU_SDOP_SILENCE_ADDED;
	}
	if ((e->ev_flags & SAU_SDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			((SAU_ParseEvData*)e->next)->wait_ms += op->time_ms;
		e->ev_flags &= ~SAU_SDEV_ADD_WAIT_DURATION;
	}
	size_t i;
	SAU_ParseOpData **ops;
	ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		time_operator(ops[i]);
	}
}

static void time_event(SAU_ParseEvData *restrict e) {
	/*
	 * Fill in blank ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	if (e->pan.time_ms == SAU_TIME_DEFAULT)
		e->pan.time_ms = 1000; /* FIXME! */
	size_t i;
	SAU_ParseOpData **ops;
	ops = (SAU_ParseOpData**) SAU_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		time_operator(ops[i]);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SAU_ParseEvData *ce = e->composite;
		SAU_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = (SAU_ParseOpData*) SAU_PtrList_GET(&ce->operators, 0);
		ce_op_prev = ce_op->op_prev;
		e_op = ce_op_prev;
		if ((e_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0)
			e_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
		for (;;) {
			ce->wait_ms += ce_op_prev->time_ms;
			if ((ce_op->op_flags & SAU_SDOP_TIME_DEFAULT) != 0) {
				ce_op->op_flags &= ~SAU_SDOP_TIME_DEFAULT;
				ce_op->time_ms =
((ce_op->op_flags & SAU_SDOP_NESTED) != 0 && !ce->next) ?
					SAU_TIME_INF :
					ce_op_prev->time_ms - ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time_ms == SAU_TIME_INF)
				e_op->time_ms = SAU_TIME_INF;
			else if (e_op->time_ms != SAU_TIME_INF)
				e_op->time_ms += ce_op->time_ms +
					(ce->wait_ms - ce_op_prev->time_ms);
			ce_op->op_params &= ~SAU_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = (SAU_ParseOpData*)
				SAU_PtrList_GET(&ce->operators, 0);
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
static void flatten_events(SAU_ParseEvData *restrict e) {
	SAU_ParseEvData *ce = e->composite;
	SAU_ParseEvData *se = e->next, *se_prev = e;
	int32_t wait_ms = 0;
	int32_t added_wait_ms = 0;
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
		if (se->next != NULL &&
(wait_ms + se->next->wait_ms) <= (ce->wait_ms + added_wait_ms)) {
			se_prev = se;
			se = se->next;
			continue;
		}
		/*
		 * Insert next composite before or after the next event
		 * of the ordinary sequence.
		 */
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			SAU_ParseEvData *ce_next = ce->next;
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
			ce = ce_next;
		} else {
			SAU_ParseEvData *se_next, *ce_next;
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

typedef struct ParseConv {
	SAU_ScriptEvData *ev, *first_ev;
} ParseConv;

/*
 * Convert data for an operator node to script operator data,
 * adding it to the list to be used for the current program event.
 */
static bool ParseConv_add_opdata(ParseConv *restrict o,
		SAU_ParseOpData *restrict pod) {
	SAU_ScriptOpData *od = calloc(1, sizeof(SAU_ScriptOpData));
	if (!od)
		return false;
	pod->op_conv = od;
	od->event = o->ev;
	/* next_bound */
	/* label */
	od->op_flags = pod->op_flags;
	od->op_params = pod->op_params;
	od->time_ms = pod->time_ms;
	od->silence_ms = pod->silence_ms;
	od->wave = pod->wave;
	od->freq = pod->freq;
	od->freq2 = pod->freq2;
	od->amp = pod->amp;
	od->amp2 = pod->amp2;
	od->phase = pod->phase;
	if (pod->op_prev != NULL) od->op_prev = pod->op_prev->op_conv;
	/* op_next */
	/* fmods */
	/* pmods */
	/* amods */
	if (!SAU_PtrList_add(&o->ev->op_all, od)) goto ERROR;
	return true;

ERROR:
	free(od);
	return false;
}

/*
 * Recursively create needed operator data nodes,
 * visiting new operator nodes as they branch out.
 */
static bool ParseConv_add_ops(ParseConv *restrict o,
		const SAU_PtrList *restrict pod_list) {
	SAU_ParseOpData **pods;
	pods = (SAU_ParseOpData**) SAU_PtrList_ITEMS(pod_list);
	for (size_t i = pod_list->old_count; i < pod_list->count; ++i) {
		SAU_ParseOpData *pod = pods[i];
		// TODO: handle multiple operator nodes
		//if (pod->op_flags & SAU_SDOP_MULTIPLE) continue;
		if (!ParseConv_add_opdata(o, pod)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->fmods)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->pmods)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->amods)) goto ERROR;
	}
	return true;

ERROR:
	return false;
}

/*
 * Recursively fill in lists for operator node graph,
 * visiting all operator nodes as they branch out.
 */
static bool ParseConv_link_ops(ParseConv *restrict o,
		SAU_PtrList *restrict od_list,
		const SAU_PtrList *restrict pod_list) {
	SAU_ParseOpData **pods;
	pods = (SAU_ParseOpData**) SAU_PtrList_ITEMS(pod_list);
	for (size_t i = 0; i < pod_list->count; ++i) {
		SAU_ParseOpData *pod = pods[i];
		// TODO: handle multiple operator nodes
		//if (pod->op_flags & SAU_SDOP_MULTIPLE) continue;
		SAU_ScriptOpData *od = pod->op_conv;
		if (!od) goto ERROR;
		SAU_ScriptEvData *e = od->event;
		if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
			// Handle linking for carriers separately
			if (od->op_flags & SAU_SDOP_NEW_CARRIER)
				SAU_PtrList_add(&e->op_graph, od);
		}
		if (od_list != NULL) SAU_PtrList_add(od_list, od);
		if (od->op_params & SAU_POPP_ADJCS) {
			if (!ParseConv_link_ops(o,
					&od->fmods, &pod->fmods)) goto ERROR;
			if (!ParseConv_link_ops(o,
					&od->pmods, &pod->pmods)) goto ERROR;
			if (!ParseConv_link_ops(o,
					&od->amods, &pod->amods)) goto ERROR;
		}
	}
	return true;

ERROR:
	SAU_error("parseconv", "converted node missing at some level");
	return false;
}

/*
 * Convert the given event data node and all associated operator data nodes.
 */
static bool ParseConv_add_event(ParseConv *restrict o,
		SAU_ParseEvData *restrict pe) {
	SAU_ScriptEvData *e = calloc(1, sizeof(SAU_ScriptEvData));
	if (!e)
		return false;
	pe->ev_conv = e;
	if (o->ev != NULL) o->ev->next = e;
	o->ev = e;
	if (!o->first_ev) o->first_ev = e;
	/* groupfrom */
	/* composite */
	e->wait_ms = pe->wait_ms;
	e->ev_flags = pe->ev_flags;
	e->vo_params = pe->vo_params;
	if (pe->vo_prev != NULL) e->vo_prev = pe->vo_prev->ev_conv;
	e->pan = pe->pan;
	if (!ParseConv_add_ops(o, &pe->operators)) goto ERROR;
	if (!ParseConv_link_ops(o, NULL, &pe->operators)) goto ERROR;
	return true;

ERROR:
	free(e);
	return false;
}

/*
 * Convert parser output to script data, performing
 * post-parsing passes - perform timing adjustments, flatten event list.
 *
 * Ideally, adjustments of parse data would be
 * more cleanly separated into the later stages.
 */
static SAU_Script *ParseConv_convert(ParseConv *restrict o,
		SAU_Parse *restrict p) {
	SAU_ParseEvData *pe;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		time_event(pe);
		if (pe->groupfrom != NULL) group_events(pe);
	}
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (pe->composite != NULL) flatten_events(pe);
	}
	/*
	 * Convert adjusted parser output to script data.
	 */
	SAU_Script *s = calloc(1, sizeof(SAU_Script));
	if (!s)
		return NULL;
	s->name = p->name;
	s->sopt = p->sopt;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (!ParseConv_add_event(o, pe)) goto ERROR;
	}
	s->events = o->first_ev;
	return s;

ERROR:
	SAU_discard_Script(s);
	return NULL;
}

/**
 * Create script data for the given script. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SAU_Script *SAU_load_Script(const char *restrict script_arg, bool is_path) {
	ParseConv pc = (ParseConv){0};
	SAU_Parse *p = SAU_create_Parse(script_arg, is_path);
	if (!p)
		return NULL;
	SAU_Script *o = ParseConv_convert(&pc, p);
	SAU_destroy_Parse(p);
	return o;
}

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SAU_ScriptOpData *restrict op) {
	SAU_PtrList_clear(&op->op_next);
	SAU_PtrList_clear(&op->fmods);
	SAU_PtrList_clear(&op->pmods);
	SAU_PtrList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SAU_ScriptEvData *restrict e) {
	size_t i;
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrList_ITEMS(&e->op_all);
	for (i = e->op_all.old_count; i < e->op_all.count; ++i) {
		destroy_operator(ops[i]);
	}
	SAU_PtrList_clear(&e->op_all);
	SAU_PtrList_clear(&e->op_graph);
	free(e);
}

/**
 * Destroy script data.
 */
void SAU_discard_Script(SAU_Script *restrict o) {
	if (!o)
		return;
	SAU_ScriptEvData *e;
	for (e = o->events; e != NULL; ) {
		SAU_ScriptEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
