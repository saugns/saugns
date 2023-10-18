/* SAU library: Parse result to audio program converter.
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

#include <sau/script.h>
#include <sau/program.h>
#include <sau/mempool.h>
#include <sau/arrtype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Program construction from parse data.
 *
 * Allocation of events, voices, operators.
 */

static const sauProgramIDArr blank_idarr = {0};

static const sauProgramIDArr *
create_ProgramIDArr(sauMempool *restrict mp,
		const uint32_t *restrict ids, uint32_t count) {
	if (!count)
		return &blank_idarr;
	size_t size = count * sizeof(uint32_t);
	sauProgramIDArr *idarr = NULL;
	if (!(idarr = sau_mpalloc(mp, sizeof(sauProgramIDArr) + size)))
		return NULL;
	idarr->count = count;
	memcpy(idarr->ids, ids, size);
	return idarr;
}

static const sauProgramIDArr *
concat_ProgramIDArr(sauMempool *restrict mp,
		const sauProgramIDArr *arr0, const sauProgramIDArr *arr1) {
	if (!arr0 || arr0->count == 0)
		return arr1;
	if (!arr1 || arr1->count == 0)
		return arr0;
	size_t size0 = sizeof(uint32_t) * arr0->count;
	size_t size1 = sizeof(uint32_t) * arr1->count;
	sauProgramIDArr *idarr = sau_mpalloc(mp,
			sizeof(sauProgramIDArr) + size0 + size1);
	if (!idarr)
		return NULL;
	idarr->count = arr0->count + arr1->count;
	memcpy(idarr->ids, arr0->ids, size0);
	memcpy(&idarr->ids[arr0->count], arr1->ids, size1);
	return idarr;
}

/*
 * Per-list state used during program data allocation.
 */
typedef struct sauLiAllocState {
	sauScriptListData *last_in;
	const sauProgramIDArr *arr;
} sauLiAllocState;

sauArrType(sauLiAlloc, sauLiAllocState, _)

/*
 * Get list ID for event, setting it to \p ar_id.
 * TODO: Implement deduplication, etc.
 *
 * \return true, or false on allocation failure
 */
static bool
sauLiAlloc_get_id(sauLiAlloc *restrict la,
		const sauScriptListData *restrict ld,
		uint32_t *restrict ar_id) {
	if (ld->ref.prev != NULL) {
		*ar_id = ld->ref.info->id;
		return true;
	}
	*ar_id = la->count;
	if (!_sauLiAlloc_add(la, NULL))
		return false;
	ld->ref.info->id = *ar_id;
	return true;
}

/*
 * Voice allocation state flags.
 */
enum {
	SAU_VOS_HAS_CARR = 1<<0,
	SAU_VOS_SET_GRAPH = 1<<1,
};

/*
 * Operator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct sauOpAllocState {
	sauScriptOpData *last_pod;
	const sauProgramIDArr *mods[SAU_POP_USES - 1];
	uint32_t flags;
	//uint32_t duration_ms;
} sauOpAllocState;

sauArrType(sauOpAlloc, sauOpAllocState, _)

/*
 * Get operator ID for event, setting it to \p op_id.
 * (Tracking of expired operators for reuse of their IDs is currently
 * disabled.)
 *
 * \return true, or false on allocation failure
 */
static bool
sauOpAlloc_get_id(sauOpAlloc *restrict oa,
		const sauScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->ref.prev != NULL) {
		*op_id = od->ref.info->id;
		return true;
	}
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (!(oa->a[id].last_pod->op_flags & SAU_SDOP_LATER_USED)
//			&& oa->a[id].duration_ms == 0) {
//			oa->a[id] = (sauOpAllocState){0};
//			*op_id = id;
//			goto ASSIGNED;
//		}
//	}
	*op_id = oa->count;
	if (!_sauOpAlloc_add(oa, NULL))
		return false;
//ASSIGNED:
	sauOpAllocState *oas = &oa->a[*op_id];
	for (int i = 1; i < SAU_POP_USES; ++i) {
		oas->mods[i - 1] = &blank_idarr;
	}
	od->ref.info->id = *op_id;
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
sauOpAlloc_update(sauOpAlloc *restrict oa,
		sauScriptOpData *restrict od,
		uint32_t *restrict op_id) {
//	sauScriptEvData *e = od->event;
//	for (uint32_t id = 0; id < oa->count; ++id) {
//		if (oa->a[id].duration_ms < e->wait_ms)
//			oa->a[id].duration_ms = 0;
//		else
//			oa->a[id].duration_ms -= e->wait_ms;
//	}
	if (!sauOpAlloc_get_id(oa, od, op_id))
		return false;
	sauOpAllocState *oas = &oa->a[*op_id];
	oas->last_pod = od;
//	oas->duration_ms = od->time.v_ms;
	return true;
}

/*
 * Clear operator allocator.
 */
static inline void
sauOpAlloc_clear(sauOpAlloc *restrict o) {
	_sauOpAlloc_clear(o);
}

sauArrType(SAU_PEvArr, sauProgramEvent, )

sauArrType(OpRefArr, sauProgramOpRef, )

/*
 * Per-voice state used during program data allocation.
 */
typedef struct sauVoiceState {
	uint32_t carr_op_id;
	uint32_t flags;
} sauVoiceState;

/**
 * Voice data, held during program building and set per event.
 */
typedef struct sauVoiceGraph {
	OpRefArr vo_graph;
	sauVoiceState *vos;
	sauOpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
} sauVoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void
sau_init_VoiceGraph(sauVoiceGraph *restrict o,
		sauScript *restrict parse, sauOpAlloc *restrict oa) {
	o->vos = calloc(parse->voice_count, sizeof(*o->vos));
	o->oa = oa;
}

static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o);

static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauProgramEvent *restrict ev,
		sauMempool *restrict mp);

/*
 * Prepare voice graph for traversal related to event.
 */
static sauVoiceState *
sauVoiceGraph_prepare(sauVoiceGraph *restrict o,
		sauScriptEvData *restrict e) {
	sauVoiceState *vos = &o->vos[e->vo_id];
	vos->flags &= ~SAU_VOS_SET_GRAPH;
	return vos;
}

sauArrType(OpDataArr, sauProgramOpData, _)

typedef struct ParseConv {
	SAU_PEvArr ev_arr;
	sauUint32Arr uint32_arr;
	sauLiAlloc la;
	sauOpAlloc oa;
	sauProgramEvent *ev;
	const sauScriptEvData *in_ev;
	const sauScriptObjRef *ev_root_obj;
	sauVoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	uint32_t ev_vo_id;
	sauMempool *mp;
} ParseConv;

static sauNoinline const sauProgramIDArr *
ParseConv_convert_list(ParseConv *restrict o,
		const sauScriptListData *restrict list_in,
		uint8_t use_type);

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_opdata(ParseConv *restrict o,
		sauScriptOpData *restrict op, uint32_t *restrict op_id,
		uint8_t use_type) {
	if (!sauOpAlloc_update(&o->oa, op, op_id)) goto MEM_ERR;
	size_t ev_op_i = o->ev_op_data.count;
	sauProgramOpData *ood = _OpDataArr_add(&o->ev_op_data, NULL);
	if (!ood) goto MEM_ERR;
	ood->id = *op_id;
	ood->params = op->params;
	ood->time = op->time;
	ood->pan = op->pan;
	ood->amp = op->amp;
	ood->amp2 = op->amp2;
	ood->freq = op->freq;
	ood->freq2 = op->freq2;
	ood->phase = op->phase;
	ood->use_type = use_type;
	/* TODO: separation of types */
	ood->type = op->ref.info->type;
	ood->seed = op->ref.info->seed;
	ood->wave = op->wave;
	ood->ras_opt = op->ras_opt;
	sauVoiceState *vos = &o->ev_vo_graph.vos[o->ev->vo_id];
	for (sauScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->next_list) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = ParseConv_convert_list(o,
						in_list, in_list->use_type)))
			goto MEM_ERR;
		/*
		 * Addresses in resized arrays got here, after maybe changing.
		 */
		ood = &o->ev_op_data.a[ev_op_i];
		sauOpAllocState *oas = &o->oa.a[*op_id];
		if (in_list->flags & SAU_SDLI_APPEND) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = concat_ProgramIDArr(o->mp,
					oas->mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == oas->mods[type]) continue; // omit no-op
		}
		oas->mods[type] = arr;
		vos->flags |= SAU_VOS_SET_GRAPH;
		switch (type + 1) {
		case SAU_POP_CAMOD: ood->camods = oas->mods[type]; break;
		case SAU_POP_AMOD:  ood->amods  = oas->mods[type]; break;
		case SAU_POP_RAMOD: ood->ramods = oas->mods[type]; break;
		case SAU_POP_FMOD:  ood->fmods  = oas->mods[type]; break;
		case SAU_POP_RFMOD: ood->rfmods = oas->mods[type]; break;
		case SAU_POP_PMOD:  ood->pmods  = oas->mods[type]; break;
		case SAU_POP_FPMOD: ood->fpmods = oas->mods[type]; break;
		}
	}
	return true;
MEM_ERR:
	return false;
}

static bool
ParseConv_finish_event(ParseConv *restrict o) {
	const sauScriptEvData *e = o->in_ev;
	sauProgramEvent *pe = o->ev;
	if (!pe)
		return true;
	sauVoiceState *vos = &o->ev_vo_graph.vos[pe->vo_id];
	o->ev = NULL;
	o->in_ev = NULL;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(sauProgramOpData**) &pe->op_data,
					o->mp)) goto MEM_ERR;
		pe->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (e->obj_first_ev == NULL)
		vos->flags |= SAU_VOS_SET_GRAPH;
	if (e->carr_info) {
		if (vos->carr_op_id != e->carr_info->id ||
		    (e->obj_first_ev && e->obj_first_ev->vo_id != e->vo_id))
			vos->flags |= SAU_VOS_SET_GRAPH;
		vos->flags |= SAU_VOS_HAS_CARR;
		vos->carr_op_id = e->carr_info->id;
	}
	if ((vos->flags & SAU_VOS_SET_GRAPH) != 0) {
		const sauScriptObjRef *obj = o->ev_root_obj;
		if (e->obj_first_ev == NULL) {
			vos->flags |= SAU_VOS_HAS_CARR;
			vos->carr_op_id = obj->info->id;
		}
		if (!sauVoiceGraph_set(&o->ev_vo_graph, pe, o->mp))
			goto MEM_ERR;
	}
	return true;
MEM_ERR:
	return false;
}

static bool
ParseConv_prepare_event(ParseConv *restrict o, sauScriptEvData *restrict e,
		const sauScriptObjRef *restrict root_obj, bool is_split) {
	if (o->in_ev == e && !is_split)
		return true;
	if (!ParseConv_finish_event(o))
		return false;
	sauVoiceGraph_prepare(&o->ev_vo_graph, e);
	sauProgramEvent *pe = SAU_PEvArr_add(&o->ev_arr, NULL);
	o->ev = pe;
	if (!o->ev)
		return false;
	o->ev->vo_id = e->vo_id; //o->ev_vo_id;
	if (!is_split) o->ev->wait_ms = e->wait_ms;
	o->in_ev = e;
	o->ev_root_obj = root_obj ? root_obj : e->root_obj;
	return true;
}

/*
 * Loop and handle list and its contents, creating ID array for it.
 * Join adjacent lists if set up in the parser.
 *
 * The sauUint32Arr is used like a stack in this function on recursion.
 *
 * \return result, or NULL on allocation failure
 */
static sauNoinline const sauProgramIDArr *
ParseConv_convert_list(ParseConv *restrict o,
		const sauScriptListData *restrict list_in,
		uint8_t use_type) {
	const sauProgramIDArr *idarr = NULL;
	size_t offset = o->uint32_arr.count;
	bool split_ev = false;
NEW_LIST:
	for (sauScriptObjRef *obj = list_in->first_item;
	     obj; obj = obj->next_item) {
		if (sauScriptObjRef_is_listdata(obj)) {
			sauScriptListData *list = (sauScriptListData*)obj;
			const sauProgramIDArr *new_idarr = NULL;
			// use outer use_type for handling of modulators, etc.
			if (!(new_idarr = ParseConv_convert_list(o, list,
					    use_type)) ||
			    !(idarr = concat_ProgramIDArr(o->mp,
					    idarr, new_idarr)))
				goto MEM_ERR;
			continue;
		} else if (!sauScriptObjRef_is_opdata(obj)) continue;
		sauScriptOpData *op = (sauScriptOpData*)obj;
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		sauScriptEvData *e = list_in->ref.event ?
				list_in->ref.event :
				obj->event;
		size_t list_max_count = o->uint32_arr.asize / sizeof(uint32_t);
		if (o->uint32_arr.count == list_max_count) {
			if (!sauUint32Arr_upsize(&o->uint32_arr,
						list_max_count + 1024))
				goto MEM_ERR;
		}
		uint32_t old_ev_vo_id = o->ev_vo_id;
//		if (o->ev_vo_id == SAU_PVO_NO_ID) // allocate one for each op
//			if (!sauVoAlloc_update(&o->va, e, &o->ev_vo_id))
//				goto MEM_ERR;
		if (!ParseConv_prepare_event(o, e, obj, split_ev))
			goto MEM_ERR;
		if (use_type == SAU_POP_CARR) split_ev = true;
		uint32_t op_id;
		if (!ParseConv_convert_opdata(o, op, &op_id, use_type))
			goto MEM_ERR;
		o->uint32_arr.a[o->uint32_arr.count++] = op_id;
		o->ev_vo_id = old_ev_vo_id;
	}
	/*
	 * Handle list/IDArr bookkeeping. List ID is the same in replacements.
	 */
	uint32_t id;
	if (!sauLiAlloc_get_id(&o->la, list_in, &id)) goto MEM_ERR;
	if (list_in->flags & SAU_SDLI_REPLACE) {
		list_in = list_in->next_list;
		if (!(list_in->flags & SAU_SDLI_APPEND)) // unused replace mode
			o->uint32_arr.count = offset; // pop allocation used
		goto NEW_LIST;
	}
	const sauProgramIDArr *new_idarr;
	if (!(new_idarr = create_ProgramIDArr(o->mp,
			    &o->uint32_arr.a[offset],
			    o->uint32_arr.count - offset)) ||
	    !(idarr = concat_ProgramIDArr(o->mp, idarr, new_idarr)))
		goto MEM_ERR;
	sauLiAllocState *las = &o->la.a[id];
	las->arr = idarr;
MEM_ERR:
	o->uint32_arr.count = offset; // pop allocation used in this call
	return idarr;
}

static bool
sauVoiceGraph_handle_op_node(sauVoiceGraph *restrict o,
		sauProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool
sauVoiceGraph_handle_op_list(sauVoiceGraph *restrict o,
		const sauProgramIDArr *restrict op_list, uint8_t mod_use) {
	if (!op_list)
		return true;
	sauProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!sauVoiceGraph_handle_op_node(o, &op_ref))
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
sauVoiceGraph_handle_op_node(sauVoiceGraph *restrict o,
		sauProgramOpRef *restrict op_ref) {
	sauOpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SAU_OAS_VISITED) {
		sau_warning("voicegraph",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SAU_OAS_VISITED;
	for (int i = 1; i < SAU_POP_USES; ++i) {
		if (!sauVoiceGraph_handle_op_list(o, oas->mods[i - 1], i))
			return false;
	}
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
static bool
sauVoiceGraph_set(sauVoiceGraph *restrict o,
		sauProgramEvent *restrict ev,
		sauMempool *restrict mp) {
	sauVoiceState *vos = &o->vos[ev->vo_id];
	if (!(vos->flags & SAU_VOS_HAS_CARR)) goto DONE;
	sauProgramOpRef op_ref = {vos->carr_op_id, SAU_POP_CARR, 0};
	if (!sauVoiceGraph_handle_op_node(o, &op_ref))
		return false;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(sauProgramOpRef**) &ev->op_list, mp))
		return false;
	ev->op_list_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
static void
sau_fini_VoiceGraph(sauVoiceGraph *restrict o) {
	free(o->vos);
	OpRefArr_clear(&o->vo_graph);
}

/*
 * Convert all voice and operator data for a parse event node into a
 * series of output events.
 *
 * This is the "main" per-event conversion function.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_event(ParseConv *restrict o,
		sauScriptEvData *restrict e) {
	o->ev_vo_id = SAU_PVO_NO_ID; // reset
	sauScriptObjRef *obj = e->root_obj;
	if (sauScriptObjRef_is_listdata(obj)) {
		sauScriptListData *list = (sauScriptListData*)obj;
		bool insert = list->flags & SAU_SDLI_INSERT;
		if (!insert) {
//			if (!sauVoAlloc_update(&o->va, list->ref.event,
//						&o->ev_vo_id))
//				goto MEM_ERR;
			// tmp dummy, discardable
			//o->va.a[o->ev_vo_id].duration_ms = 0;
		} else {
			if (list->ref.prev) list = list->ref.prev;
		}
		if (!ParseConv_convert_list(o, list, SAU_POP_DEFAULT))
			goto MEM_ERR;
	} else if (sauScriptObjRef_is_opdata(obj)) {
//		if (!sauVoAlloc_update(&o->va, e, &o->ev_vo_id)) goto MEM_ERR;
		if (!ParseConv_prepare_event(o, e, obj, false)) goto MEM_ERR;
		uint32_t op_id;
		sauScriptOpData *op = (sauScriptOpData*)obj;
		if (!ParseConv_convert_opdata(o, op, &op_id, SAU_POP_DEFAULT))
			goto MEM_ERR;
	}
	if (!ParseConv_finish_event(o)) goto MEM_ERR;
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
		sauScript *restrict parse) {
	bool error = false;
	if (parse->voice_count > SAU_PVO_MAX_ID) {
		fprintf(stderr,
"%s: error: number of voices used cannot exceed %u\n",
			parse->name, SAU_PVO_MAX_ID);
		error = true;
	}
	if (o->oa.count > SAU_POP_MAX_ID) {
		fprintf(stderr,
"%s: error: number of operators used cannot exceed %u\n",
			parse->name, SAU_POP_MAX_ID);
		error = true;
	}
	return !error;
}

static sauProgram *
ParseConv_create_program(ParseConv *restrict o,
		sauScript *restrict parse) {
	sauProgram *prg = sau_mpalloc(o->mp, sizeof(sauProgram));
	if (!prg) goto MEM_ERR;
	if (!SAU_PEvArr_mpmemdup(&o->ev_arr,
				(sauProgramEvent**) &prg->events, o->mp))
		goto MEM_ERR;
	prg->ev_count = o->ev_arr.count;
	if (!(parse->sopt.set & SAU_SOPT_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by audio generator.
		 */
		prg->mode |= SAU_PMODE_AMP_DIV_VOICES;
	}
	prg->vo_count = parse->voice_count;
	prg->op_count = o->oa.count;
	prg->op_nest_depth = o->ev_vo_graph.op_nest_max;
	prg->duration_ms = parse->duration_ms;
	prg->name = parse->name;
	prg->mp = o->mp;
	prg->parse = parse;
	o->mp = NULL; // don't destroy
	return prg;
MEM_ERR:
	return NULL;
}

/*
 * Build program, allocating events, voices, and operators.
 */
static sauProgram *
ParseConv_convert(ParseConv *restrict o,
		sauScript *restrict parse) {
	sauProgram *prg = NULL;
	o->mp = parse->prg_mp;
	sau_init_VoiceGraph(&o->ev_vo_graph, parse, &o->oa);
	for (sauScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
	}
	if (ParseConv_check_validity(o, parse)) {
		prg = ParseConv_create_program(o, parse);
		if (!prg) goto MEM_ERR;
	}

	if (false)
	MEM_ERR: {
		sau_error("parseconv", "memory allocation failure");
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	sauOpAlloc_clear(&o->oa);
	_sauLiAlloc_clear(&o->la);
	sauUint32Arr_clear(&o->uint32_arr);
	SAU_PEvArr_clear(&o->ev_arr);
	return prg;
}

/**
 * Create internal program for the given script data. Includes a pointer
 * to \p parse as \a parse, unless \p keep_parse is false, in which case
 * the parse is destroyed after the conversion regardless of the result.
 *
 * \return instance or NULL on error
 */
sauProgram *
sau_build_Program(sauScript *restrict parse, bool keep_parse) {
	if (!parse)
		return NULL;
	ParseConv pc = (ParseConv){0};
	sauProgram *o = ParseConv_convert(&pc, parse);
	if (!keep_parse) {
		if (o) {
			parse->prg_mp = NULL;
			o->parse = NULL;
		}
		sau_discard_Script(parse);
	}
	return o;
}

/**
 * Destroy instance. Also free parse data if held.
 */
void
sau_discard_Program(sauProgram *restrict o) {
	if (!o)
		return;
	if (o->parse && o->parse->prg_mp == o->mp) // avoid double-destroy
		o->parse->prg_mp = NULL;
	sau_discard_Script(o->parse);
	sau_destroy_Mempool(o->mp);
}

static sauNoinline void
print_linked(const char *restrict header,
		const char *restrict footer,
		const sauProgramIDArr *restrict idarr) {
	if (!idarr || !idarr->count)
		return;
	sau_printf("%s%u", header, idarr->ids[0]);
	for (uint32_t i = 0; ++i < idarr->count; )
		sau_printf(", %u", idarr->ids[i]);
	sau_printf("%s", footer);
}

static void
print_oplist(const sauProgramOpRef *restrict list,
		uint32_t count) {
	if (!list)
		return;
	FILE *out = sau_print_stream();
	static const char *const uses[SAU_POP_USES] = {
		" CA",
		"cAM",
		" AM",
		"rAM",
		" FM",
		"rFM",
		" PM",
		"fPM",
	};

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", out);
	for (;;) {
		const uint32_t indent = list[i].level * 3;
		if (indent > max_indent) max_indent = indent;
		fprintf(out, "%6u:  ", list[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', out);
		fputs(uses[list[i].use], out);
		if (++i == count) break;
		fputs("\n\t     ", out);
	}
	for (uint32_t j = max_indent; j > 0; --j)
		putc(' ', out);
	putc(']', out);
}

static sauNoinline void
print_line(const sauLine *restrict line, char c) {
	if (!line)
		return;
	if ((line->flags & SAU_LINEP_STATE) != 0) {
		if ((line->flags & SAU_LINEP_GOAL) != 0)
			sau_printf("\t%c=%-6.2f->%-6.2f", c, line->v0, line->vt);
		else
			sau_printf("\t%c=%-6.2f\t", c, line->v0);
	} else {
		if ((line->flags & SAU_LINEP_GOAL) != 0)
			sau_printf("\t%c->%-6.2f\t", c, line->vt);
		else
			sau_printf("\t%c", c);
	}
}

static void
print_opline(const sauProgramOpData *restrict od) {
	char type = '?';
	switch (od->type) {
	case SAU_POBJT_WAVE: type = 'W'; break;
	case SAU_POBJT_RASG: type = 'R'; break;
	}
	if (od->time.flags & SAU_TIMEP_IMPLICIT) {
		sau_printf("\n\top %-2u %c t=IMPL  ", od->id, type);
	} else {
		sau_printf("\n\top %-2u %c t=%-6u",
				od->id, type, od->time.v_ms);
	}
	print_line(od->freq, 'f');
	print_line(od->amp, 'a');
}

/**
 * Print information about program contents. Useful for debugging.
 */
void
sauProgram_print_info(const sauProgram *restrict o) {
	sau_printf("Program: \"%s\"\n"
		"\tDuration: \t%u ms\n"
		"\tEvents:   \t%zu\n"
		"\tVoices:   \t%hu\n"
		"\tOperators:\t%u\n",
		o->name,
		o->duration_ms,
		o->ev_count,
		o->vo_count,
		o->op_count);
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const sauProgramEvent *ev = &o->events[ev_id];
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (ev->op_list != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(ev->op_list, ev->op_list_count);
		}
		for (size_t i = 0; i < ev->op_data_count; ++i) {
			const sauProgramOpData *od = &ev->op_data[i];
			print_opline(od);
			print_linked("\n\t    c[", "]", od->camods);
			print_linked("\n\t    a[", "]", od->amods);
			print_linked("\n\t    ar[", "]", od->ramods);
			print_linked("\n\t    f[", "]", od->fmods);
			print_linked("\n\t    fr[", "]", od->rfmods);
			print_linked("\n\t    p[", "]", od->pmods);
			print_linked("\n\t    pf[", "]", od->fpmods);
		}
		sau_printf("\n");
	}
}
