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
#include "../mempool.h"

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
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SAU_NodeRef *op_ref = e->op_list.refs;
		for (; op_ref != NULL; op_ref = op_ref->next) {
			SAU_ParseOpData *op = op_ref->data;
			if (e->next == e_after &&
op_ref == e->op_list.last_ref && (op->op_flags & SAU_PDOP_TIME_DEFAULT) != 0) {
				/* default for last node in group */
				op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
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
		SAU_NodeRef *op_ref = e->op_list.refs;
		for (; op_ref != NULL; op_ref = op_ref->next) {
			SAU_ParseOpData *op = op_ref->data;
			if (op->op_flags & SAU_PDOP_TIME_DEFAULT) {
				/* fill in sensible default time */
				op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
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

static inline void time_ramp(SAU_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SAU_RAMP_TIME_SET))
		ramp->time_ms = default_time_ms;
}

static void time_operator(SAU_ParseOpData *restrict op) {
	SAU_ParseEvData *e = op->event;
	if ((op->op_flags & (SAU_PDOP_TIME_DEFAULT | SAU_PDOP_NESTED)) ==
			(SAU_PDOP_TIME_DEFAULT | SAU_PDOP_NESTED)) {
		op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
		if (!(op->op_flags & SAU_PDOP_HAS_COMPOSITE))
			op->time_ms = SAU_TIME_INF;
	}
	if (op->time_ms != SAU_TIME_INF) {
		time_ramp(&op->freq, op->time_ms);
		time_ramp(&op->freq2, op->time_ms);
		time_ramp(&op->amp, op->time_ms);
		time_ramp(&op->amp2, op->time_ms);
		if (!(op->op_flags & SAU_PDOP_SILENCE_ADDED)) {
			op->time_ms += op->silence_ms;
			op->op_flags |= SAU_PDOP_SILENCE_ADDED;
		}
	}
	if (e->ev_flags & SAU_PDEV_ADD_WAIT_DURATION) {
		if (e->next != NULL) e->next->wait_ms += op->time_ms;
		e->ev_flags &= ~SAU_PDEV_ADD_WAIT_DURATION;
	}
	for (SAU_NodeList *list = op->nest_lists;
			list != NULL; list = list->next) {
		SAU_NodeList_fornew(list, (SAU_NodeRef_data_f) time_operator);
	}
}

static void time_event(SAU_ParseEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	// e->pan.flags |= SAU_RAMP_TIME_SET; // TODO: revisit semantics
	SAU_NodeList_fornew(&e->op_list, (SAU_NodeRef_data_f) time_operator);
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SAU_ParseEvData *ce = e->composite;
		SAU_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = ce->op_list.refs->data;
		ce_op_prev = ce_op->prev;
		e_op = ce_op_prev;
		if (e_op->op_flags & SAU_PDOP_TIME_DEFAULT)
			e_op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
		for (;;) {
			ce->wait_ms += ce_op_prev->time_ms;
			if (ce_op->op_flags & SAU_PDOP_TIME_DEFAULT) {
				ce_op->op_flags &= ~SAU_PDOP_TIME_DEFAULT;
				ce_op->time_ms = ((ce_op->op_flags &
(SAU_PDOP_NESTED | SAU_PDOP_HAS_COMPOSITE)) == SAU_PDOP_NESTED) ?
					SAU_TIME_INF :
					(ce_op_prev->time_ms - ce_op_prev->silence_ms);
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
			ce_op = ce->op_list.refs->data;
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
		SAU_ParseEvData *ce_next = ce->next;
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
		} else {
			SAU_ParseEvData *se_next = se->next;
			ce->wait_ms -= wait_ms;
			added_wait_ms += ce->wait_ms;
			wait_ms = 0;
			se->next = ce;
			ce->next = se_next;
			se_prev = ce;
			se = se_next;
		}
		ce = ce_next;
	}
	e->composite = NULL;
}

typedef struct ParseConv {
	SAU_ScriptEvData *ev, *first_ev;
	SAU_MemPool *tmp; // for temporary data, borrowed from input SAU_Parse
	SAU_MemPool *omp; // for output data
} ParseConv;

/*
 * Per-operator context data for references, used during conversion.
 */
typedef struct OpContext {
	SAU_ParseOpData *newest; // most recent in time-ordered events
	/* current node adjacents in operator linkage graph */
	SAU_NodeList *fmod_list;
	SAU_NodeList *pmod_list;
	SAU_NodeList *amod_list;
} OpContext;

/*
 * Get operator context for node, updating associated data.
 *
 * If the node is ignored, the SAU_PDOP_IGNORED flag is set
 * before returning NULL. A NULL context means either error
 * or an ignored node.
 *
 * \return instance, or NULL on allocation failure or ignored node
 */
static OpContext *ParseConv_update_opcontext(ParseConv *restrict o,
		SAU_ScriptOpData *restrict od,
		SAU_ParseOpData *restrict pod) {
	OpContext *oc = NULL;
	if (!pod->prev) {
		oc = SAU_MemPool_alloc(o->tmp, sizeof(OpContext));
		if (!oc)
			return NULL;
	} else {
		oc = pod->prev->op_context;
		if (!oc) {
			/*
			 * This can happen for orphaned nodes,
			 * removed from the list in which created,
			 * yet referred to later (e.g. label use).
			 */
			pod->op_flags |= SAU_PDOP_IGNORED;
			return NULL;
		}
		SAU_ScriptOpData *od_prev = oc->newest->op_conv;
		od->op_prev = od_prev;
		od_prev->op_flags |= SAU_SDOP_LATER_USED;
	}
	oc->newest = pod;
	oc->fmod_list = NULL;
	oc->pmod_list = NULL;
	oc->amod_list = NULL;
	for (SAU_NodeList *list = pod->nest_lists;
			list != NULL; list = list->next) {
		switch (list->type) {
		case SAU_NLT_FMODS:
			oc->fmod_list = list;
			break;
		case SAU_NLT_PMODS:
			oc->pmod_list = list;
			break;
		case SAU_NLT_AMODS:
			oc->amod_list = list;
			break;
		default:
			break;
		}
	}
	pod->op_context = oc;
	return oc;
}

/*
 * Per-voice context data for references, used during conversion.
 */
typedef struct VoContext {
	SAU_ParseEvData *newest; // most recent in time-ordered events
} VoContext;

/*
 * Convert data for an operator node to script operator data,
 * adding it to the list to be used for the current script event.
 */
static bool ParseConv_add_opdata(ParseConv *restrict o,
		SAU_NodeRef *restrict pod_ref) {
	SAU_ParseOpData *pod = pod_ref->data;
	SAU_ScriptOpData *od = SAU_MemPool_alloc(o->omp,
			sizeof(SAU_ScriptOpData));
	if (!od)
		return false;
	SAU_ScriptEvData *e = o->ev;
	pod->op_conv = od;
	od->event = e;
	/* next_bound */
	/* op_flags */
	od->op_params = pod->op_params;
	od->time_ms = pod->time_ms;
	od->silence_ms = pod->silence_ms;
	od->wave = pod->wave;
	if (pod_ref->list_type == SAU_NLT_GRAPH &&
			(pod_ref->mode & SAU_NRM_ADD) != 0) {
		e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
		od->op_flags |= SAU_SDOP_NEW_CARRIER;
	}
	od->freq = pod->freq;
	od->freq2 = pod->freq2;
	od->amp = pod->amp;
	od->amp2 = pod->amp2;
	od->phase = pod->phase;
	od->fmods.type = SAU_NLT_FMODS;
	od->pmods.type = SAU_NLT_PMODS;
	od->amods.type = SAU_NLT_AMODS;
	if (!ParseConv_update_opcontext(o, od, pod)) goto ERROR;
	if (!SAU_NodeList_add(&e->op_all, od, SAU_NRM_UPDATE, o->omp))
		goto ERROR;
	return true;

ERROR:
	SAU_destroy_MemPool(o->omp);
	return false;
}

/*
 * Recursively create needed operator data nodes,
 * visiting new operator nodes as they branch out.
 */
static bool ParseConv_add_ops(ParseConv *restrict o,
		const SAU_NodeList *restrict pol) {
	if (!pol)
		return true;
	SAU_NodeRef *pod_ref = pol->new_refs;
	for (; pod_ref != NULL; pod_ref = pod_ref->next) {
		SAU_ParseOpData *pod = pod_ref->data;
		if (pod->op_flags & SAU_PDOP_MULTIPLE) {
			// TODO: handle multiple operator nodes
			pod->op_flags |= SAU_PDOP_IGNORED;
			continue;
		}
		if (!ParseConv_add_opdata(o, pod_ref)) {
			if (pod->op_flags & SAU_PDOP_IGNORED) continue;
			goto ERROR;
		}
		OpContext *oc = pod->op_context;
		if (!ParseConv_add_ops(o, oc->fmod_list)) goto ERROR;
		if (!ParseConv_add_ops(o, oc->pmod_list)) goto ERROR;
		if (!ParseConv_add_ops(o, oc->amod_list)) goto ERROR;
	}
	return true;

ERROR:
	return false;
}

/*
 * Recursively fill in lists for operator node graph,
 * visiting all linked operator nodes as they branch out.
 */
static bool ParseConv_link_ops(ParseConv *restrict o,
		SAU_NodeList *restrict ol,
		const SAU_NodeList *restrict pol) {
	if (!pol)
		return true;
	if (ol != NULL)
		SAU_NodeList_clear(ol); // rebuild, replacing shallow copy
	SAU_NodeRef *pod_ref = pol->refs;
	for (; pod_ref != NULL; pod_ref = pod_ref->next) {
		SAU_ParseOpData *pod = pod_ref->data;
		if (pod->op_flags & SAU_PDOP_IGNORED) continue;
		SAU_ScriptOpData *od = pod->op_conv;
		if (!od) goto ERROR;
		if (ol != NULL) {
			if (!SAU_NodeList_add(ol, od, SAU_NRM_ADD, o->omp))
				goto ERROR;
		}
		if (od->op_prev != NULL) {
			od->fmods.refs = od->op_prev->fmods.refs;
			od->pmods.refs = od->op_prev->pmods.refs;
			od->amods.refs = od->op_prev->amods.refs;
		}
		OpContext *oc = pod->op_context;
		if (!ParseConv_link_ops(o, &od->fmods, oc->fmod_list))
			goto ERROR;
		if (!ParseConv_link_ops(o, &od->pmods, oc->pmod_list))
			goto ERROR;
		if (!ParseConv_link_ops(o, &od->amods, oc->amod_list))
			goto ERROR;
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
	SAU_ScriptEvData *e = SAU_MemPool_alloc(o->omp,
			sizeof(SAU_ScriptEvData));
	if (!e)
		return false;
	pe->ev_conv = e;
	if (!o->first_ev)
		o->first_ev = e;
	else
		o->ev->next = e;
	o->ev = e;
	e->wait_ms = pe->wait_ms;
	/* ev_flags */
	e->op_all.type = SAU_NLT_GRAPH; // will do, only used for updates
	VoContext *vc;
	if (!pe->vo_prev) {
		vc = SAU_MemPool_alloc(o->tmp, sizeof(VoContext));
		if (!vc) goto ERROR;
		e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
	} else {
		vc = pe->vo_prev->vo_context;
		SAU_ScriptEvData *vo_prev = vc->newest->ev_conv;
		e->vo_prev = vo_prev;
		vo_prev->ev_flags |= SAU_SDEV_VOICE_LATER_USED;
	}
	vc->newest = pe;
	pe->vo_context = vc;
	e->vo_params = pe->vo_params;
	e->pan = pe->pan;
	if (!ParseConv_add_ops(o, &pe->op_list)) goto ERROR;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
		e->op_graph = SAU_create_NodeList(SAU_NLT_GRAPH, o->omp);
		if (!e->op_graph) goto ERROR;
	}
	if (!ParseConv_link_ops(o, e->op_graph, &pe->op_list)) goto ERROR;
	return true;

ERROR:
	return false;
}

/*
 * Convert parser output to script data, performing
 * post-parsing passes - perform timing adjustments,
 * flatten event list.
 *
 * Ideally, adjustments of parse data would be
 * more cleanly separated into the later stages.
 */
static SAU_Script *ParseConv_convert(ParseConv *restrict o,
		SAU_Parse *restrict p) {
	o->tmp = p->mem;
	o->omp = SAU_create_MemPool(0);
	if (!o->omp)
		return NULL;
	SAU_ParseEvData *pe;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		time_event(pe);
		if (pe->groupfrom != NULL) group_events(pe);
	}
	SAU_Script *s = SAU_MemPool_alloc(o->omp, sizeof(SAU_Script));
	if (!s) goto ERROR;
	s->name = p->name;
	s->sopt = p->sopt;
	s->mem = o->omp;
	/*
	 * Convert events, flattening the remaining list while proceeding.
	 * Flattening must be done following the timing adjustment pass;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (!ParseConv_add_event(o, pe)) goto ERROR;
		if (pe->composite != NULL) flatten_events(pe);
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

/**
 * Destroy script data.
 */
void SAU_discard_Script(SAU_Script *restrict o) {
	if (!o)
		return;
	SAU_destroy_MemPool(o->mem);
}
