/* saugns: Parse result to audio program converter.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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

#include "../script.h"
#include "../program.h"
#include "../arrtype.h"
#include "../mempool.h"
#include "../ptrarr.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static const SAU_ProgramOpList blank_oplist = {0};

static sauNoinline const SAU_ProgramOpList*
create_ProgramOpList(const SAU_ScriptListData *restrict list_in,
		SAU_MemPool *restrict mem) {
	uint32_t count = 0;
	const SAU_ScriptOpRef *op;
	for (op = list_in->first_item; op != NULL; op = op->next_item) {
		++count;
	}
	if (!count)
		return &blank_oplist;
	SAU_ProgramOpList *o = SAU_MemPool_alloc(mem,
			sizeof(SAU_ProgramOpList) + sizeof(uint32_t) * count);
	if (!o)
		return NULL;
	o->count = count;
	uint32_t i = 0;
	for (op = list_in->first_item; op != NULL; op = op->next_item) {
		o->ids[i++] = op->obj->obj_id;
	}
	return o;
}

/*
 * Voice allocation state flags.
 */
enum {
	SAU_VAS_GRAPH = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct SAU_VoAllocState {
	SAU_ScriptEvData *last_ev;
	const SAU_ProgramOpList *op_carrs;
	uint32_t flags;
	uint32_t duration_ms;
} SAU_VoAllocState;

sauArrType(SAU_VoAlloc, SAU_VoAllocState, _)

/*
 * Get voice ID for event, setting it to \p vo_id.
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_VoAlloc_get_id(SAU_VoAlloc *restrict va,
		const SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	if (e->root_ev != NULL) {
		*vo_id = e->root_ev->vo_id;
		return true;
	}
	for (size_t id = 0; id < va->count; ++id) {
		SAU_VoAllocState *vas = &va->a[id];
		if (!(vas->last_ev->ev_flags & SAU_SDEV_VOICE_LATER_USED)
			&& vas->duration_ms == 0) {
			*vas = (SAU_VoAllocState){0};
			*vo_id = id;
			goto ASSIGNED;
		}
	}
	*vo_id = va->count;
	if (!_SAU_VoAlloc_add(va, NULL))
		return false;
ASSIGNED:
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
static bool
SAU_VoAlloc_update(SAU_VoAlloc *restrict va,
		SAU_ScriptEvData *restrict e, uint32_t *restrict vo_id) {
	for (uint32_t id = 0; id < va->count; ++id) {
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
	vas->flags &= ~SAU_VAS_GRAPH;
	if ((e->ev_flags & SAU_SDEV_VOICE_SET_DUR) != 0)
		vas->duration_ms = e->dur_ms;
	return true;
}

/*
 * Clear voice allocator.
 */
static void
SAU_VoAlloc_clear(SAU_VoAlloc *restrict o) {
	_SAU_VoAlloc_clear(o);
}

/*
 * Operator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct SAU_OpAllocState {
	SAU_ScriptOpRef *last_pod;
	const SAU_ProgramOpList *fmods, *pmods, *amods;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

sauArrType(SAU_OpAlloc, SAU_OpAllocState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_OpAlloc_get_id(SAU_OpAlloc *restrict oa,
		const SAU_ScriptOpRef *restrict od, uint32_t *restrict op_id) {
	if (od->on_prev != NULL) {
		*op_id = od->obj->obj_id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[op_id].last_pod->op_flags & SAU_SDOP_LATER_USED)
//			&& oa->a[op_id].duration_ms == 0) {
//			oa->a[id] = (SAU_OpAllocState){0};
//			*op_id = id;
//			goto ASSIGNED;
//		}
//	}
	*op_id = oa->count;
	if (!_SAU_OpAlloc_add(oa, NULL))
		return false;
//ASSIGNED:
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
static bool
SAU_OpAlloc_update(SAU_OpAlloc *restrict oa,
		SAU_ScriptOpRef *restrict od,
		uint32_t *restrict op_id) {
//	SAU_ScriptEvData *e = od->event;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!SAU_OpAlloc_get_id(oa, od, op_id))
		return false;
	od->obj->obj_id = *op_id;
	SAU_OpAllocState *oas = &oa->a[*op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static void
SAU_OpAlloc_clear(SAU_OpAlloc *restrict o) {
	_SAU_OpAlloc_clear(o);
}

sauArrType(OpRefArr, SAU_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SAU_VoiceGraph {
	OpRefArr vo_graph;
	SAU_VoAlloc *va;
	SAU_OpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
	struct SAU_MemPool *mem;
} SAU_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void
SAU_init_VoiceGraph(SAU_VoiceGraph *restrict o,
		SAU_VoAlloc *restrict va,
		SAU_OpAlloc *restrict oa,
		struct SAU_MemPool *restrict mem) {
	o->va = va;
	o->oa = oa;
	o->mem = mem;
}

void SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o);

bool SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev);

typedef struct ParseConv {
	SAU_PtrArr ev_list;
	SAU_VoAlloc va;
	SAU_OpAlloc oa;
	SAU_ProgramEvent *ev;
	SAU_VoiceGraph ev_vo_graph;
	SAU_PtrArr ev_od_list;
	uint32_t duration_ms;
	SAU_MemPool *mem;
} ParseConv;

/*
 * Replace program operator list.
 *
 * \return true, or false on allocation failure
 */
static inline bool
set_oplist(const SAU_ProgramOpList **restrict dstp,
		const SAU_ScriptListData *restrict src,
		SAU_MemPool *restrict mem) {
	const SAU_ProgramOpList *dst = create_ProgramOpList(src, mem);
	if (!dst)
		return false;
	*dstp = dst;
	return true;
}

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_opdata(ParseConv *restrict o,
		const SAU_ScriptOpRef *restrict op, uint32_t op_id) {
	SAU_OpAllocState *oas = &o->oa.a[op_id];
	SAU_ProgramOpData *od = op->data;
	if (!SAU_PtrArr_add(&o->ev_od_list, od)) goto MEM_ERR;
	od->id = op_id;
	/* ...mods */
	SAU_VoAllocState *vas = &o->va.a[o->ev->vo_id];
	const SAU_ScriptListData *mods[SAU_POP_USES] = {0};
	for (SAU_ScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->next_list) {
		vas->flags |= SAU_VAS_GRAPH;
		mods[in_list->use_type] = in_list;
	}
	if (mods[SAU_POP_AMOD] != NULL) {
		if (!set_oplist(&oas->amods, mods[SAU_POP_AMOD], o->mem))
			goto MEM_ERR;
		od->amods = oas->amods;
	}
	if (mods[SAU_POP_FMOD] != NULL) {
		if (!set_oplist(&oas->fmods, mods[SAU_POP_FMOD], o->mem))
			goto MEM_ERR;
		od->fmods = oas->fmods;
	}
	if (mods[SAU_POP_PMOD] != NULL) {
		if (!set_oplist(&oas->pmods, mods[SAU_POP_PMOD], o->mem))
			goto MEM_ERR;
		od->pmods = oas->pmods;
	}
	return true;
MEM_ERR:
	return false;
}

/*
 * Visit each operator node in the list and recurse through each node's
 * sublists in turn, creating new output events as needed for the
 * operator data.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_ops(ParseConv *restrict o,
		SAU_ScriptListData *restrict op_list) {
	if (!op_list)
		return true;
	for (SAU_ScriptOpRef *op = op_list->first_item;
			op != NULL; op = op->next_item) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id;
		if (!SAU_OpAlloc_update(&o->oa, op, &op_id))
			return false;
		for (SAU_ScriptListData *in_list = op->mods;
				in_list != NULL; in_list = in_list->next_list) {
			if (!ParseConv_convert_ops(o, in_list))
				return false;
		}
		if (!ParseConv_convert_opdata(o, op, op_id))
			return false;
	}
	return true;
}

static bool
SAU_VoiceGraph_handle_op_node(SAU_VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_VoiceGraph_handle_op_list(SAU_VoiceGraph *restrict o,
		const SAU_ProgramOpList *restrict op_list, uint8_t mod_use) {
	if (!op_list)
		return true;
	SAU_ProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!SAU_VoiceGraph_handle_op_node(o, &op_ref))
			return false;
	}
	return true;
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool
SAU_VoiceGraph_handle_op_node(SAU_VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref) {
	SAU_OpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SAU_OAS_VISITED) {
		SAU_warning("voicegraph",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SAU_OAS_VISITED;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->amods, SAU_POP_AMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->fmods, SAU_POP_FMOD))
		return false;
	if (!SAU_VoiceGraph_handle_op_list(o, oas->pmods, SAU_POP_PMOD))
		return false;
	oas->flags &= ~SAU_OAS_VISITED;
	--o->op_nest_level;
	if (!OpRefArr_add(&o->vo_graph, op_ref))
		return false;
	return true;
}

/**
 * Create operator graph for voice using data built
 * during allocation, assigning an operator reference
 * list to the voice and block IDs to the operators.
 *
 * \return true, or false on allocation failure
 */
bool
SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev) {
	SAU_VoAllocState *vas = &o->va->a[ev->vo_id];
	if (!vas->op_carrs || !vas->op_carrs->count) goto DONE;
	if (!SAU_VoiceGraph_handle_op_list(o, vas->op_carrs, SAU_POP_CARR))
		return false;
	SAU_ProgramVoData *vd = (SAU_ProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(SAU_ProgramOpRef**) &vd->graph, o->mem))
		return false;
	vd->op_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
void
SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o) {
	OpRefArr_clear(&o->vo_graph);
}

/*
 * Convert all voice and operator data for a script event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_event(ParseConv *restrict o,
		SAU_ScriptEvData *restrict e) {
	uint32_t vo_id;
	if (!SAU_VoAlloc_update(&o->va, e, &vo_id)) goto MEM_ERR;
	SAU_VoAllocState *vas = &o->va.a[vo_id];
	SAU_ProgramEvent *out_ev = SAU_MemPool_alloc(o->mem,
			sizeof(SAU_ProgramEvent));
	if (!out_ev || !SAU_PtrArr_add(&o->ev_list, out_ev)) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = vo_id;
	o->ev = out_ev;
	if (!ParseConv_convert_ops(o, &e->main_refs)) goto MEM_ERR;
	if (o->ev_od_list.count > 0) {
		if (!SAU_PtrArr_mpmemdup(&o->ev_od_list,
					(void***) &out_ev->op_data,
					o->mem)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_od_list.count;
		o->ev_od_list.count = 0; // reuse allocation
	}
	if (!e->root_ev)
		vas->flags |= SAU_VAS_GRAPH;
	if ((vas->flags & SAU_VAS_GRAPH) != 0) {
		SAU_ProgramVoData *ovd = SAU_MemPool_alloc(o->mem,
				sizeof(SAU_ProgramVoData));
		if (!ovd) goto MEM_ERR;
		ovd->params = SAU_PVOP_GRAPH;
		if (!e->root_ev) {
			if (!set_oplist(&vas->op_carrs, &e->main_refs, o->mem))
				goto MEM_ERR;
		}
		out_ev->vo_data = ovd;
		if ((vas->flags & SAU_VAS_GRAPH) != 0) {
			if (!SAU_VoiceGraph_set(&o->ev_vo_graph, out_ev))
				goto MEM_ERR;
		}
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
static bool
ParseConv_check_validity(ParseConv *restrict o,
		SAU_Script *restrict script) {
	bool error = false;
	if (o->va.count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			script->name, SAU_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			script->name, SAU_POP_MAX_ID);
		error = true;
	}
	return !error;
}

static SAU_Program*
ParseConv_create_program(ParseConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = SAU_MemPool_alloc(o->mem, sizeof(SAU_Program));
	if (!prg) goto MEM_ERR;
	if (!SAU_PtrArr_mpmemdup(&o->ev_list,
				(void***) &prg->events, o->mem)) goto MEM_ERR;
	prg->ev_count = o->ev_list.count;
	if (!(script->sopt.set & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = o->va.count;
	prg->op_count = o->oa.count;
	prg->op_nest_depth = o->ev_vo_graph.op_nest_max;
	prg->duration_ms = o->duration_ms;
	prg->name = script->name;
	return prg;
MEM_ERR:
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static SAU_Program*
ParseConv_convert(ParseConv *restrict o,
		SAU_Script *restrict script) {
	SAU_Program *prg = NULL;
	o->mem = script->mem;
	if (!o->mem) goto MEM_ERR;
	SAU_init_VoiceGraph(&o->ev_vo_graph, &o->va, &o->oa, o->mem);

	uint32_t remaining_ms = 0;
	for (SAU_ScriptEvData *e = script->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
		o->duration_ms += e->wait_ms;
	}
	for (size_t i = 0; i < o->va.count; ++i) {
		SAU_VoAllocState *vas = &o->va.a[i];
		if (vas->duration_ms > remaining_ms)
			remaining_ms = vas->duration_ms;
	}
	o->duration_ms += remaining_ms;
	if (ParseConv_check_validity(o, script)) {
		prg = ParseConv_create_program(o, script);
		if (!prg) goto MEM_ERR;
	}

	if (false)
	MEM_ERR: {
		SAU_error("parseconv", "memory allocation failure");
	}
	SAU_OpAlloc_clear(&o->oa);
	SAU_VoAlloc_clear(&o->va);
	SAU_PtrArr_clear(&o->ev_od_list);
	SAU_PtrArr_clear(&o->ev_list);
	SAU_fini_VoiceGraph(&o->ev_vo_graph);
	return prg;
}

/**
 * Create internal program for the given script data.
 *
 * \return instance or NULL on error
 */
bool
SAU_build_Program(SAU_Script *restrict sd) {
	ParseConv pc = (ParseConv){0};
	SAU_Program *o = ParseConv_convert(&pc, sd);
	if (!o)
		return false;
	sd->program = o;
	return true;
}

static sauNoinline void
print_linked(const char *restrict header,
		const char *restrict footer,
		const SAU_ProgramOpList *restrict list) {
	if (!list || !list->count)
		return;
	fprintf(stdout, "%s%u", header, list->ids[0]);
	for (uint32_t i = 0; ++i < list->count; )
		fprintf(stdout, ", %u", list->ids[i]);
	fprintf(stdout, "%s", footer);
}

static void
print_graph(const SAU_ProgramOpRef *restrict graph,
		uint32_t count) {
	static const char *const uses[SAU_POP_USES] = {
		"CA",
		"AM",
		"FM",
		"PM",
	};
	if (!graph)
		return;

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", stdout);
	for (;;) {
		const uint32_t indent = graph[i].level * 2;
		if (indent > max_indent) max_indent = indent;
		fprintf(stdout, "%6u:  ", graph[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', stdout);
		fputs(uses[graph[i].use], stdout);
		if (++i == count) break;
		fputs("\n\t     ", stdout);
	}
	for (uint32_t j = max_indent; j > 0; --j)
		putc(' ', stdout);
	putc(']', stdout);
}

static sauNoinline void
print_ramp(const SAU_Ramp *restrict ramp, char c) {
	if (!ramp)
		return;
	putc('\t', stdout);
	putc(c, stdout);
	if ((ramp->flags & SAU_RAMPP_STATE) != 0) {
		if ((ramp->flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"=%-6.1f->%-6.1f", ramp->v0, ramp->vt);
		else
			fprintf(stdout,
				"=%-6.1f\t", ramp->v0);
	} else {
		if ((ramp->flags & SAU_RAMPP_GOAL) != 0)
			fprintf(stdout,
				"->%-6.1f\t", ramp->vt);
	}
}

static void
print_opline(const SAU_ProgramOpData *restrict od) {
	if (od->time.flags & SAU_TIMEP_IMPLICIT) {
		fprintf(stdout,
			"\n\top %u \tt=IMPL  ", od->id);
	} else {
		fprintf(stdout,
			"\n\top %u \tt=%-6u", od->id, od->time.v_ms);
	}
	print_ramp(od->freq, 'f');
	print_ramp(od->amp, 'a');
}

/**
 * Print information about program contents. Useful for debugging.
 */
void
SAU_Program_print_info(const SAU_Program *restrict o) {
	fprintf(stdout,
		"Program: \"%s\"\n", o->name);
	fprintf(stdout,
		"\tDuration: \t%u ms\n"
		"\tEvents:   \t%zu\n"
		"\tVoices:   \t%hu\n"
		"\tOperators:\t%u\n",
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const SAU_ProgramEvent *ev = o->events[ev_id];
		const SAU_ProgramVoData *vd = ev->vo_data;
		fprintf(stdout,
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			fprintf(stdout,
				"\n\tvo %u", ev->vo_id);
			print_graph(vd->graph, vd->op_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const SAU_ProgramOpData *od = ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    a~[", "]", od->amods);
			print_linked("\n\t    f~[", "]", od->fmods);
			print_linked("\n\t    p+[", "]", od->pmods);
		}
		putc('\n', stdout);
	}
}
