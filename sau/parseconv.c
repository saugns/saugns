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

static sauNoinline const sauProgramIDArr *
sau_create_ProgramIDArr(sauMempool *restrict mp,
		const sauScriptListData *restrict list_in) {
	uint32_t count = 0;
	for (sauScriptOpData *op = list_in->first_item; op; op = op->next)
		++count;
	if (!count)
		return &blank_idarr;
	sauProgramIDArr *idarr = sau_mpalloc(mp,
			sizeof(sauProgramIDArr) + sizeof(uint32_t) * count);
	if (!idarr)
		return NULL;
	idarr->count = count;
	uint32_t i = 0;
	for (sauScriptOpData *op = list_in->first_item; op; op = op->next)
		idarr->ids[i++] = op->obj_id;
	return idarr;
}

static sauNoinline const sauProgramIDArr *
sau_concat_ProgramIDArr(sauMempool *restrict mp,
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
 * Operator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct sauOpAllocState {
	const sauProgramIDArr *mods[SAU_POP_NAMED - 1];
	uint32_t flags;
} sauOpAllocState;

sauArrType(sauOpAlloc, sauOpAllocState, _)

/*
 * Update operator data for event and return an operator ID in \p op_id.
 *
 * Use the current operator if any, otherwise allocating a new one.
 * (TODO: Implement tracking of expired operators (requires look at
 * nesting and use of modulators by carriers), for reusing of IDs.)
 *
 * Only valid to call for single-operator nodes.
 *
 * \return true, or false on allocation failure
 */
static bool
sauOpAlloc_update(sauOpAlloc *restrict oa,
		sauScriptOpData *restrict od, uint32_t *restrict op_id) {
	if (od->prev_ref != NULL) {
		*op_id = od->obj_id;
		return true;
	}
	*op_id = od->obj_id;
	sauOpAllocState *oas = &oa->a[*op_id];
	for (int i = 1; i < SAU_POP_NAMED; ++i) {
		oas->mods[i - 1] = &blank_idarr;
	}
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
 * Voice state flags.
 */
enum {
	SAU_VOS_HAS_CARR = 1<<0,
	SAU_VOS_SET_GRAPH = 1<<1,
};

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
		const sauProgramEvent *restrict ev,
		sauMempool *restrict mp);

sauArrType(OpDataArr, sauProgramOpData, _)

typedef struct ParseConv {
	SAU_PEvArr ev_arr;
	sauOpAlloc oa;
	sauProgramEvent *ev;
	sauVoiceGraph ev_vo_graph;
	OpDataArr ev_op_data;
	sauMempool *mp;
	sauScript *parse;
} ParseConv;

/*
 * Convert data for an operator node to program operator data,
 * adding it to the list to be used for the current program event.
 *
 * \return true, or false on allocation failure
 */
static bool
ParseConv_convert_opdata(ParseConv *restrict o,
		const sauScriptOpData *restrict op, uint32_t op_id,
		uint8_t use_type) {
	sauOpAllocState *oas = &o->oa.a[op_id];
	sauProgramOpData *ood = _OpDataArr_add(&o->ev_op_data, NULL);
	if (!ood) goto MEM_ERR;
	sauScriptObjInfo *info = &o->parse->objects[op->obj_id];
	ood->id = op_id;
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
	ood->type = info->obj_type;
	ood->seed = info->seed;
	ood->wave = op->wave;
	ood->ras_opt = op->ras_opt;
	sauVoiceState *vos = &o->ev_vo_graph.vos[o->ev->vo_id];
	for (sauScriptListData *in_list = op->mods;
			in_list != NULL; in_list = in_list->next_list) {
		int type = in_list->use_type - 1;
		const sauProgramIDArr *arr;
		if (!(arr = sau_create_ProgramIDArr(o->mp, in_list)))
			goto MEM_ERR;
		if (in_list->flags & SAU_SDLI_APPEND) {
			if (arr == &blank_idarr) continue; // omit no-op
			if (!(arr = sau_concat_ProgramIDArr(o->mp,
					oas->mods[type], arr))) goto MEM_ERR;
		} else {
			if (arr == oas->mods[type]) continue; // omit no-op
		}
		oas->mods[type] = arr;
		vos->flags |= SAU_VOS_SET_GRAPH;
		switch (type + 1) {
		case SAU_POP_N_camod: ood->camods = oas->mods[type]; break;
		case SAU_POP_N_amod:  ood->amods  = oas->mods[type]; break;
		case SAU_POP_N_ramod: ood->ramods = oas->mods[type]; break;
		case SAU_POP_N_fmod:  ood->fmods  = oas->mods[type]; break;
		case SAU_POP_N_rfmod: ood->rfmods = oas->mods[type]; break;
		case SAU_POP_N_pmod:  ood->pmods  = oas->mods[type]; break;
		case SAU_POP_N_fpmod: ood->fpmods = oas->mods[type]; break;
		}
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
		sauScriptListData *restrict op_list) {
	if (op_list) for (sauScriptOpData *op = op_list->first_item;
			op; op = op->next) {
		// TODO: handle multiple operator nodes
		if ((op->op_flags & SAU_SDOP_MULTIPLE) != 0) continue;
		uint32_t op_id;
		if (!sauOpAlloc_update(&o->oa, op, &op_id))
			return false;
		for (sauScriptListData *in_list = op->mods;
				in_list != NULL; in_list = in_list->next_list) {
			if (!ParseConv_convert_ops(o, in_list))
				return false;
		}
		if (!ParseConv_convert_opdata(o, op, op_id, op_list->use_type))
			return false;
	}
	return true;
}

/*
 * Prepare voice graph for traversal related to event.
 */
static sauVoiceState *
sauVoiceGraph_prepare(sauVoiceGraph *restrict o,
		sauScriptOpData *restrict obj) {
	sauVoiceState *vos = &o->vos[obj->vo_id];
	vos->flags &= ~SAU_VOS_SET_GRAPH;
	return vos;
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
	for (int i = 1; i < SAU_POP_NAMED; ++i) {
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
		const sauProgramEvent *restrict ev,
		sauMempool *restrict mp) {
	sauVoiceState *vos = &o->vos[ev->vo_id];
	if (!(vos->flags & SAU_VOS_HAS_CARR)) goto DONE;
	sauProgramOpRef op_ref = {vos->carr_op_id, SAU_POP_N_carr, 0};
	if (!sauVoiceGraph_handle_op_node(o, &op_ref))
		return false;
	sauProgramVoData *vd = (sauProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph,
				(sauProgramOpRef**) &vd->op_list, mp))
		return false;
	vd->op_count = o->vo_graph.count;
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
	sauScriptOpData *obj = e->objs.first_item;
	sauVoiceState *vos = sauVoiceGraph_prepare(&o->ev_vo_graph, obj);
	sauProgramEvent *out_ev = SAU_PEvArr_add(&o->ev_arr, NULL);
	if (!out_ev) goto MEM_ERR;
	out_ev->wait_ms = e->wait_ms;
	out_ev->vo_id = obj->vo_id;
	o->ev = out_ev;
	if (!ParseConv_convert_ops(o, &e->objs)) goto MEM_ERR;
	if (o->ev_op_data.count > 0) {
		if (!_OpDataArr_mpmemdup(&o->ev_op_data,
					(sauProgramOpData**) &out_ev->op_data,
					o->mp)) goto MEM_ERR;
		out_ev->op_data_count = o->ev_op_data.count;
		o->ev_op_data.count = 0; // reuse allocation
	}
	if (e->ev_flags & SAU_SDEV_ASSIGN_VOICE) {
		sauScriptObjInfo *info = &o->parse->objects[obj->obj_id];
		vos->flags |= SAU_VOS_HAS_CARR | SAU_VOS_SET_GRAPH;
		vos->carr_op_id = info->root_obj_id;
	}
	if ((vos->flags & SAU_VOS_SET_GRAPH) != 0) {
		sauProgramVoData *ovd =
			sau_mpalloc(o->mp, sizeof(sauProgramVoData));
		if (!ovd) goto MEM_ERR;
		out_ev->vo_data = ovd;
		if (!sauVoiceGraph_set(&o->ev_vo_graph, out_ev, o->mp))
			goto MEM_ERR;
	}
	return true;
MEM_ERR:
	return false;
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
	prg->op_count = parse->object_count;
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
	o->parse = parse;
	o->oa.asize = parse->object_count;
	o->oa.a = calloc(parse->object_count, sizeof(sauOpAllocState));
	sau_init_VoiceGraph(&o->ev_vo_graph, parse, &o->oa);
	for (sauScriptEvData *e = parse->events; e; e = e->next) {
		if (!ParseConv_convert_event(o, e)) goto MEM_ERR;
	}
	if (!(prg = ParseConv_create_program(o, parse))) goto MEM_ERR;
	if (false)
	MEM_ERR: {
		sau_error("parseconv", "memory allocation failure");
	}
	sau_fini_VoiceGraph(&o->ev_vo_graph);
	_OpDataArr_clear(&o->ev_op_data);
	sauOpAlloc_clear(&o->oa);
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
	static const char *const uses[SAU_POP_NAMED] = {
		SAU_POP__ITEMS(SAU_POP__X_PRINT)
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
	case SAU_POPT_WAVE: type = 'W'; break;
	case SAU_POPT_RAS: type = 'R'; break;
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
		const sauProgramVoData *vd = ev->vo_data;
		sau_printf(
			"/%u \tEV %zu \t(VO %hu)",
			ev->wait_ms, ev_id, ev->vo_id);
		if (vd != NULL) {
			sau_printf(
				"\n\tvo %u", ev->vo_id);
			print_oplist(vd->op_list, vd->op_count);
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
