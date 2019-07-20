/* saugns: Script data to audio program converter.
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

#include "scriptconv.h"
#include "../ptrarr.h"
#include <stdio.h>

/*
 * Program construction from script data.
 *
 * Allocation of events, voices, operators.
 */

static const SAU_ProgramOpList blank_oplist = {0};

static sauNoinline const SAU_ProgramOpList
*create_ProgramOpList(const SAU_PtrArr *restrict op_list,
		SAU_MemPool *restrict mem) {
	if (!op_list->count)
		return &blank_oplist;
	uint32_t count = op_list->count;
	const SAU_ScriptOpData **ops;
	ops = (const SAU_ScriptOpData**) SAU_PtrArr_ITEMS(op_list);
	SAU_ProgramOpList *o;
	o = SAU_MemPool_alloc(mem,
			sizeof(SAU_ProgramOpList) + sizeof(uint32_t) * count);
	if (!o)
		return NULL;
	o->count = count;
	for (uint32_t i = 0; i < count; ++i) {
		o->ids[i] = ops[i]->op_id;
	}
	return o;
}

/*
 * Returns the longest carrier duration for the voice event.
 */
static uint32_t voice_duration(const SAU_ScriptEvData *restrict ve) {
	SAU_ScriptOpData **ops;
	uint32_t duration_ms = 0;
	/* FIXME: node list type? */
	ops = (SAU_ScriptOpData**) SAU_PtrArr_ITEMS(&ve->op_carriers);
	for (size_t i = 0; i < ve->op_carriers.count; ++i) {
		SAU_ScriptOpData *op = ops[i];
		if (op->time.v_ms > duration_ms)
			duration_ms = op->time.v_ms;
	}
	return duration_ms;
}

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true, or false on allocation failure
 */
static bool SAU_VoAlloc_get_id(SAU_VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->vo_prev != NULL) {
		*vo_id = e->vo_prev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SAU_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (SAU_VoAllocState){0};
			*vo_id = id;
			return true;
		}
	}
	*vo_id = va->count;
	if (!_SAU_VoAlloc_add(va, NULL))
		return false;
	return true;
}

/*
 * Update voices for event and return a voice ID for the event.
 *
 * Use the current voice if any, otherwise reusing an expired voice
 * if possible, or allocating a new if not.
 *
 * \return true, or false on allocation failure
 */
static bool SAU_VoAlloc_update(SAU_VoAlloc *restrict va,
		SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (size_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	if (!SAU_VoAlloc_get_id(va, e, vo_id))
		return false;
	e->vo_id = *vo_id;
	SAU_VoAllocState *vas = &va->a[*vo_id];
	vas->last_ev = e;
	vas->carriers = &blank_oplist;
	vas->flags &= ~SAU_VAS_GRAPH;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->duration_ms = voice_duration(e);
	return true;
}

/*
 * Clear voice allocator.
 */
static void SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
	_SAU_VoAlloc_clear(o);
}

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool SAU_OpAlloc_get_id(SAU_OpAlloc *restrict oa,
		const SAU_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->op_prev != NULL) {
		*op_id = od->op_prev->op_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		SAU_OpAllocState *oas = &oa->a[id];
//		if (!(oas->last_sod->op_flags & SAU_SDOP_LATER_USED)
//			&& oas->duration_ms == 0) {
//			*oas = (SAU_OpAllocState){0};
//			*op_id = id;
//			return true;
//		}
//	}
	*op_id = oa->count;
	if (!_SAU_OpAlloc_add(oa, NULL))
		return false;
	return true;
}

/*
 * Update operators for event and return an operator ID for the event.
 *
 * Use the current operator if any, otherwise allocating a new one.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * Only valid to call for single-operator nodes.
 *
 * \return true, or false on allocation failure
 */
static bool SAU_OpAlloc_update(SAU_OpAlloc *restrict oa,
		SAU_ScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	SAU_ScriptEvData *e = od->event;
//	for (size_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!SAU_OpAlloc_get_id(oa, od, op_id))
		return false;
	od->op_id = *op_id;
	SAU_OpAllocState *oas = &oa->a[*op_id];
	oas->last_sod = od;
	oas->fmods = &blank_oplist;
	oas->pmods = &blank_oplist;
	oas->amods = &blank_oplist;
	oas->flags = 0;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static void SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

sauArrType(OpDataArr, SAU_ProgramOpData, _)

typedef struct ScriptConv {
	SAU_PtrArr ev_list;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
	SAU_MemPool *mem;
} ScriptConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool OpDataArr_add_for(OpDataArr *restrict o,
		const SAU_ScriptOpData *restrict op, uint32_t op_id) {
	SAU_ProgramOpData *od = _OpDataArr_add(o, NULL);
	if (!od)
		return false;
	od->id = op_id;
	od->params = op->op_params;
	od->time = op->time;
	od->silence_ms = op->silence_ms;
	od->wave = op->wave;
	od->freq = op->freq;
	od->freq2 = op->freq2;
	od->amp = op->amp;
	od->amp2 = op->amp2;
	od->phase = op->phase;
	return true;
}

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool update_oplist(const SAU_ProgramOpList **restrict dstp,
		const SAU_PtrArr *restrict src,
		SAU_MemPool *restrict mem) {
	const SAU_ProgramOpList *dst = create_ProgramOpList(src, mem);
	if (!dst)
		return false;
	*dstp = dst;
	return true;
}

/*
 * Convert the flat script operator data list in two stages,
 * adding all the operator data nodes, then filling in
 * the adjacency lists when all nodes are registered.
 *
 * Ensures non-NULL list pointers for SAU_OpAllocState nodes, while for
 * output nodes, they are non-NULL initially and upon changes.
 *
 * \return true, or false on allocation failure
 */
static bool ScriptConv_convert_ops(ScriptConv *restrict o,
		SAU_PtrArr *restrict op_list) {
	SAU_ScriptOpData **ops;
	ops = (SAU_ScriptOpData**) SAU_PtrArr_ITEMS(op_list);
	for (size_t i = op_list->old_count; i < op_list->count; ++i) {
		SAU_ScriptOpData *op = ops[i];
		uint32_t op_id;
		// TODO: handle multiple operator nodes
		if (op->op_flags & SAU_SDOP_MULTIPLE) continue;
		if (!SAU_OpAlloc_update(&o->oa, op, &op_id)) goto MEM_ERR;
		if (!OpDataArr_add_for(&o->ev_op_data, op, op_id)) goto MEM_ERR;
	}
	for (size_t i = 0; i < o->ev_op_data.count; ++i) {
		SAU_ProgramOpData *od = &o->ev_op_data.a[i];
		SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
		SAU_OpAllocState *oas = &o->oa.a[od->id];
		SAU_ScriptOpData *sod = oas->last_sod;
		if (od->params & SAU_POPP_ADJCS) {
			vas->flags |= SAU_VAS_GRAPH;
			if (!update_oplist(&oas->fmods, &sod->fmods, o->mem))
				goto MEM_ERR;
			od->fmods = oas->fmods;
			if (!update_oplist(&oas->pmods, &sod->pmods, o->mem))
				goto MEM_ERR;
			od->pmods = oas->pmods;
			if (!update_oplist(&oas->amods, &sod->amods, o->mem))
				goto MEM_ERR;
			od->amods = oas->amods;
		}
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Convert all voice and operator data for a script event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 *
 * \return true, or false on allocation failure
 */
static bool ScriptConv_convert_event(ScriptConv *restrict o,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id;
	uint32_t vo_params;
	if (!SAU_VoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	SAU_VoAllocState *vas = &o->va.a[vo_id];
	SAU_ProgramEvent *out_ev = SAU_MemPool_alloc(o->mem,
			sizeof(SAU_ProgramEvent));
	if (!out_ev || !SAU_PtrArr_add(&o->ev_list, out_ev)) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ScriptConv_convert_ops(o, &e->op_all)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(SAU_ProgramOpData**) &out_ev->op_data,
					o->mem)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH)
		vas->flags |= SAU_VAS_GRAPH;
	if (vas->flags & SAU_VAS_GRAPH)
		vo_params |= SAU_PVOP_GRAPH;
	if (vo_params != 0) {
		SAU_ProgramVoData *ovd = SAU_MemPool_alloc(o->mem,
				sizeof(SAU_ProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if (e->ev_flags & SAU_SDEV_NEW_OPGRAPH) {
			if (!update_oplist(&vas->carriers, &e->op_carriers,
						o->mem)) goto MEM_ERR;
		}
		ovd->carriers = vas->carriers;
		out_ev->vo_data = ovd;
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Check whether program can be returned for use.
 *
 * \return true, unless invalid data detected
 */
static bool ScriptConv_check_validity(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	bool error = false;
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			script->name, SAU_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			script->name, SAU_POP_MAX_ID);
		error = true;
	}
	return !error;
}

static SAU_Program *ScriptConv_create_program(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = SAU_MemPool_alloc(o->mem, sizeof(SAU_Program));
	if (!prg) goto MEM_ERR;
	if (!SAU_PtrArr_mpmemdup(&o->ev_list,
				(void***) &prg->events, o->mem)) goto MEM_ERR;
	prg->ev_count = o->ev_list.count;
	if (!(script->sopt.changed & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = o->va.count;
	prg->op_count = o->oa.count;
	prg->duration_ms = o->duration_ms;
	prg->name = script->name;
	prg->mem = o->mem;
	o->mem = NULL; // pass on to program
	return prg;
MEM_ERR:
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program *ScriptConv_convert(ScriptConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = NULL;
	o->mem = SAU_create_MemPool(0);
	if (!o->mem) goto MEM_ERR;

	uint32_t remaining_ms = 0;
	for (SAU_ScriptEvData *e = script->events; e; e = e->next) {
		if (!ScriptConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;
	if (ScriptConv_check_validity(o, script)) {
		prg = ScriptConv_create_program(o, script);
		if (!prg) goto MEM_ERR;
	}

	if (false)
	MEM_ERR: {
		SAU_error("scriptconv", "memory allocation failure");
	}
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
	_OpDataArr_clear(&o->ev_op_data);
	SAU_PtrArr_clear(&o->ev_list);
	SAU_destroy_MemPool(o->mem);
	return prg;
}

/**
 * Create internal program for the given script data.
 *
 * \return instance or NULL on error
 */
SAU_Program* SAU_build_Program(SAU_Script *restrict sd) {
	ScriptConv sc = (ScriptConv){0};
	SAU_Program *o = ScriptConv_convert(&sc, sd);
	return o;
}

/**
 * Destroy instance.
 */
void SAU_discard_Program(SAU_Program *restrict o) {
	if (!o)
		return;
	SAU_destroy_MemPool(o->mem);
}

static void print_linked(const char *restrict header,
		const char *restrict footer,
		const SAU_ProgramOpList *restrict list) {
	if (!list || !list->count)
		return;
	fprintf(stdout, "%s%d", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %d", list->ids[i]);
	fprintf(stdout, "%s", footer);
}

static void print_opline(const SAU_ProgramOpData *restrict od) {
	if (od->time.flags & SAU_TIMEP_LINKED) {
		fprintf(stdout,
			"\n\top %d \tt=INF   \t", od->id);
	} else {
		fprintf(stdout,
			"\n\top %d \tt=%-6d\t", od->id, od->time.v_ms);
	}
	if ((od->freq.flags & SAU_RAMPP_STATE) != 0) {
		if ((od->freq.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f=%-6.1f->%-6.1f", od->freq.v0, od->freq.vt);
		else
			fprintf(stdout,
				"f=%-6.1f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f->%-6.1f\t", od->freq.vt);
		else
			fprintf(stdout,
				"\t\t");
	}
	if ((od->amp.flags & SAU_RAMPP_STATE) != 0) {
		if ((od->amp.flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"\ta=%-6.1f->%-6.1f", od->amp.v0, od->amp.vt);
		else
			fprintf(stdout,
				"\ta=%-6.1f", od->amp.v0);
	} else if ((od->amp.flags & SAU_RAMPP_GOAL) != 0) {
		fprintf(stdout,
			"\ta->%-6.1f", od->amp.vt);
	}
}

/**
 * Print program summary with name and numbers.
 */
void SAU_Program_print_info(const SAU_Program *restrict o,
		const char *restrict name_prefix,
		const char *restrict name_suffix) {
	if (!name_prefix) name_prefix = "";
	if (!name_suffix) name_suffix = "";
	fprintf(stdout,
		"%s%s%s\n", name_prefix, o->name, name_suffix);
	fprintf(stdout,
		"\tDuration: \t%d ms\n"
		"\tEvents:   \t%zd\n"
		"\tVoices:   \t%hd\n"
		"\tOperators:\t%d\n",
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
}

/**
 * Print event data voice information.
 */
void SAU_ProgramEvent_print_voice(const SAU_ProgramEvent *restrict ev) {
		const SAU_ProgramVoData *vd = ev->vo_data;
		if (!vd)
			return;
		fprintf(stdout,
			"\n\tvo %d", ev->vo_id);
}

/**
 * Print event data operator information.
 */
void SAU_ProgramEvent_print_operators(const SAU_ProgramEvent *restrict ev) {
	for (size_t i = 0; i < ev->op_data_count; ++i) {
		const SAU_ProgramOpData *od = &ev->op_data[i];
		print_opline(od);
		print_linked("\n\t    f~[", "]", od->fmods);
		print_linked("\n\t    p+[", "]", od->pmods);
		print_linked("\n\t    a~[", "]", od->amods);
	}
}
