/* ssndgen: Script data to audio program converter.
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

static const SSG_ProgramOpList blank_oplist = {0};

static SSG__noinline const SSG_ProgramOpList
*create_ProgramOpList(const SSG_RefList *restrict op_list,
		SSG_MemPool *restrict mem) {
	size_t count = op_list->ref_count;
	if (!count)
		return &blank_oplist;
	SSG_ProgramOpList *o = SSG_MemPool_alloc(mem,
			sizeof(SSG_ProgramOpList) + sizeof(uint32_t) * count);
	if (!o)
		return NULL;
	o->count = count;
	size_t i = 0;
	for (SSG_RefItem *ref = op_list->refs;
			ref != NULL; ref = ref->next) {
		SSG_ScriptOpData *op = ref->data;
		o->ids[i++] = op->op_id;
	}
	return o;
}

/*
 * Returns the longest carrier duration for the voice event.
 */
static uint32_t voice_duration(const SSG_ScriptEvData *restrict ve) {
	if (!ve->carriers)
		return 0;
	uint32_t duration_ms = 0;
	for (SSG_RefItem *ref = ve->carriers->refs;
			ref != NULL; ref = ref->next) {
		SSG_ScriptOpData *op = ref->data;
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
static bool SSG_VoAlloc_get_id(SSG_VoAlloc *restrict va,
		const SSG_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->vo_prev != NULL) {
		*vo_id = e->vo_prev->vo_id;
		return true;
	}
	SSG_VoAllocState *vas;
	for (size_t id = 0; id < va->count; ++id) {
		vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SSG_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (SSG_VoAllocState){0};
			*vo_id = id;
			goto INIT;
		}
	}
	*vo_id = va->count;
	if (!_SSG_VoAlloc_add(va, NULL))
		return false;
	vas = &va->a[*vo_id];
INIT:
	vas->carriers = &blank_oplist;
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
static bool SSG_VoAlloc_update(SSG_VoAlloc *restrict va,
		SSG_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (size_t id = 0; id < va->count; ++id) {
		if (va->a[id].duration_ms < e->wait_ms)
			va->a[id].duration_ms = 0;
		else
			va->a[id].duration_ms -= e->wait_ms;
	}
	if (!SSG_VoAlloc_get_id(va, e, vo_id))
		return false;
	e->vo_id = *vo_id;
	SSG_VoAllocState *vas = &va->a[*vo_id];
	vas->last_ev = e;
	vas->flags &= ~SSG_VAS_GRAPH;
	if (e->ev_flags & SSG_SDEV_NEW_OPGRAPH)
		vas->duration_ms = voice_duration(e);
	return true;
}

/*
 * Clear voice allocator.
 */
static void SSG_VoAlloc_clear(SSG_VoAlloc *restrict o) {
	_SSG_VoAlloc_clear(o);
}

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool SSG_OpAlloc_get_id(SSG_OpAlloc *restrict oa,
		const SSG_ScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->prev_use != NULL) {
		*op_id = od->prev_use->op_id;
		return true;
	}
	SSG_OpAllocState *oas;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		oas = &oa->a[id];
//		if (!(oas->last_sod->op_flags & SSG_SDOP_LATER_USED)
//			&& oas->duration_ms == 0) {
//			*oas = (SSG_OpAllocState){0};
//			*op_id = id;
//			goto INIT;
//		}
//	}
	*op_id = oa->count;
	if (!_SSG_OpAlloc_add(oa, NULL))
		return false;
	oas = &oa->a[*op_id];
//INIT:
	for (size_t i = 0; i < SSG_POP_USES - 1; ++i)
		oas->mod_lists[i] = &blank_oplist;
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
static bool SSG_OpAlloc_update(SSG_OpAlloc *restrict oa,
		SSG_ScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	SSG_ScriptEvData *e = od->event;
//	for (size_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!SSG_OpAlloc_get_id(oa, od, op_id))
		return false;
	od->op_id = *op_id;
	SSG_OpAllocState *oas = &oa->a[*op_id];
	oas->last_sod = od;
	oas->flags = 0;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static void SSG_OpAlloc_clear(SSG_OpAlloc *restrict o) {
	_SSG_OpAlloc_clear(o);
}

SSG_DEF_ArrType(OpDataArr, SSG_ProgramOpData, _)

typedef struct ScriptConv {
	SSG_PtrArr ev_list;
	SSG_VoAlloc va;
	SSG_OpAlloc oa;
	SSG_ProgramEvent *ev;
	OpDataArr ev_op_data;
	uint32_t duration_ms;
	SSG_MemPool *mem;
} ScriptConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool OpDataArr_add_for(OpDataArr *restrict o,
		const SSG_ScriptOpData *restrict op, uint32_t op_id) {
	SSG_ProgramOpData *od = _OpDataArr_add(o, NULL);
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
static inline bool update_oplist(const SSG_ProgramOpList **restrict dstp,
		const SSG_RefList *restrict src,
		SSG_MemPool *restrict mem) {
	if (!src)
		return true; // nothing to do
	const SSG_ProgramOpList *dst = create_ProgramOpList(src, mem);
	if (!dst)
		return false;
	*dstp = dst;
	return true;
}

/*
 * Update node modulator lists for updated lists in script data.
 *
 * Ensures non-NULL list pointers for SSG_OpAllocState nodes, while for
 * output nodes, they are non-NULL initially and upon changes.
 *
 * \return true, or false on allocation failure
 */
static bool ScriptConv_update_modlists(ScriptConv *restrict o,
		SSG_ProgramOpData *restrict od) {
	SSG_OpAllocState *oas = &o->oa.a[od->id];
	const SSG_ScriptOpData *sod = oas->last_sod;
	const SSG_RefList *sub_lists[SSG_POP_USES - 1] = {0};
	for (const SSG_RefList *list = sod->mod_lists;
			list != NULL; list = list->next) {
		sub_lists[list->list_type - 1] = list;
	}
	SSG_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	for (size_t i = 0; i < SSG_POP_USES - 1; ++i) {
		if (!update_oplist(&oas->mod_lists[i], sub_lists[i], o->mem))
			return false;
	}
	vas->flags |= SSG_VAS_GRAPH;
	od->fmods = oas->mod_lists[SSG_POP_FMOD - 1];
	od->pmods = oas->mod_lists[SSG_POP_PMOD - 1];
	od->amods = oas->mod_lists[SSG_POP_AMOD - 1];
	return true;
}

/*
 * Convert the flat script operator data list in two stages,
 * adding all the operator data nodes, then filling in
 * the adjacency lists when all nodes are registered.
 *
 * \return true, or false on allocation failure
 */
static bool ScriptConv_convert_ops(ScriptConv *restrict o,
		SSG_NodeRange *restrict op_list) {
	SSG_ScriptOpData *op;
	for (op = op_list->first; op != NULL; op = op->range_next) {
		uint32_t op_id;
		if (!SSG_OpAlloc_update(&o->oa, op, &op_id)) goto MEM_ERR;
		if (!OpDataArr_add_for(&o->ev_op_data, op, op_id)) goto MEM_ERR;
	}
	for (size_t i = 0; i < o->ev_op_data.count; ++i) {
		SSG_ProgramOpData *od = &o->ev_op_data.a[i];
		if (od->params & SSG_POPP_ADJCS) {
			if (!ScriptConv_update_modlists(o, od)) goto MEM_ERR;
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
		SSG_ScriptEvData *restrict e) {
	uint32_t vo_id;
	uint32_t vo_params;
	if (!SSG_VoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	SSG_VoAllocState *vas = &o->va.a[vo_id];
	SSG_ProgramEvent *out_ev = SSG_MemPool_alloc(o->mem,
			sizeof(SSG_ProgramEvent));
	if (!out_ev || !SSG_PtrArr_add(&o->ev_list, out_ev)) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ScriptConv_convert_ops(o, &e->op_all)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(SSG_ProgramOpData**) &out_ev->op_data,
					o->mem)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	vo_params = e->vo_params;
	if (e->ev_flags & SSG_SDEV_NEW_OPGRAPH)
		vas->flags |= SSG_VAS_GRAPH;
	if (vas->flags & SSG_VAS_GRAPH)
		vo_params |= SSG_PVOP_GRAPH;
	if (vo_params != 0) {
		SSG_ProgramVoData *ovd = SSG_MemPool_alloc(o->mem,
				sizeof(SSG_ProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->params = vo_params;
		ovd->pan = e->pan;
		if (e->ev_flags & SSG_SDEV_NEW_OPGRAPH) {
			if (!update_oplist(&vas->carriers, e->carriers,
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
		SSG_Script *restrict script) {
	bool error = false;
	if (o->va.count > SSG_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %d\n",
			script->name, SSG_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SSG_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %d\n",
			script->name, SSG_POP_MAX_ID);
		error = true;
	}
	return !error;
}

static SSG_Program *ScriptConv_create_program(ScriptConv *restrict o,
		SSG_Script *restrict script) {
	SSG_Program *prg = SSG_MemPool_alloc(o->mem, sizeof(SSG_Program));
	if (!prg) goto MEM_ERR;
	if (!SSG_PtrArr_mpmemdup(&o->ev_list,
				(void***) &prg->events, o->mem)) goto MEM_ERR;
	prg->ev_count = o->ev_list.count;
	if (!(script->sopt.changed & SSG_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SSG_PMODE_AMP_DIV_VOICES;
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
static SSG_Program *ScriptConv_convert(ScriptConv *restrict o,
		SSG_Script *restrict script) {
	SSG_Program *prg = NULL;
	o->mem = SSG_create_MemPool(0);
	if (!o->mem) goto MEM_ERR;

	uint32_t remaining_ms = 0;
	for (SSG_ScriptEvData *e = script->events; e; e = e->next) {
		if (!ScriptConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SSG_VoAllocState *vas = &o->va.a[i];
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
		SSG_error("scriptconv", "memory allocation failure");
	}
	SSG_OpAlloc_clear(&o->oa);
	SSG_VoAlloc_clear(&o->va);
	_OpDataArr_clear(&o->ev_op_data);
	SSG_PtrArr_clear(&o->ev_list);
	SSG_destroy_MemPool(o->mem);
	return prg;
}

/**
 * Create internal program for the given script data.
 *
 * \return instance or NULL on error
 */
SSG_Program* SSG_build_Program(SSG_Script *restrict sd) {
	ScriptConv sc = (ScriptConv){0};
	SSG_Program *o = ScriptConv_convert(&sc, sd);
	return o;
}

/**
 * Destroy instance.
 */
void SSG_discard_Program(SSG_Program *restrict o) {
	if (!o)
		return;
	SSG_destroy_MemPool(o->mem);
}

static void print_linked(const char *restrict header,
		const char *restrict footer,
		const SSG_ProgramOpList *restrict list) {
	if (!list || !list->count)
		return;
	fprintf(stdout, "%s%d", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %d", list->ids[i]);
	fprintf(stdout, "%s", footer);
}

static void print_opline(const SSG_ProgramOpData *restrict od) {
	if (od->time.flags & SSG_TIMEP_LINKED) {
		fprintf(stdout,
			"\n\top %d \tt=INF   \t", od->id);
	} else {
		fprintf(stdout,
			"\n\top %d \tt=%-6d\t", od->id, od->time.v_ms);
	}
	if ((od->freq.flags & SSG_RAMPP_STATE) != 0) {
		if ((od->freq.flags & SSG_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f=%-6.1f->%-6.1f", od->freq.v0, od->freq.vt);
		else
			fprintf(stdout,
				"f=%-6.1f\t", od->freq.v0);
	} else {
		if ((od->freq.flags & SSG_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"f->%-6.1f\t", od->freq.vt);
		else
			fprintf(stdout,
				"\t\t");
	}
	if ((od->amp.flags & SSG_RAMPP_STATE) != 0) {
		if ((od->amp.flags & SSG_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"\ta=%-6.1f->%-6.1f", od->amp.v0, od->amp.vt);
		else
			fprintf(stdout,
				"\ta=%-6.1f", od->amp.v0);
	} else if ((od->amp.flags & SSG_RAMPP_GOAL) != 0) {
		fprintf(stdout,
			"\ta->%-6.1f", od->amp.vt);
	}
}

/**
 * Print program summary with name and numbers.
 */
void SSG_Program_print_info(const SSG_Program *restrict o,
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
void SSG_ProgramEvent_print_voice(const SSG_ProgramEvent *restrict ev) {
		const SSG_ProgramVoData *vd = ev->vo_data;
		if (!vd)
			return;
		fprintf(stdout,
			"\n\tvo %d", ev->vo_id);
}

/**
 * Print event data operator information.
 */
void SSG_ProgramEvent_print_operators(const SSG_ProgramEvent *restrict ev) {
	for (size_t i = 0; i < ev->op_data_count; ++i) {
		const SSG_ProgramOpData *od = &ev->op_data[i];
		print_opline(od);
		print_linked("\n\t    f~[", "]", od->fmods);
		print_linked("\n\t    p+[", "]", od->pmods);
		print_linked("\n\t    a~[", "]", od->amods);
	}
}
