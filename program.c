/* sgensys: Audio generation program definition/creation module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "program.h"
#include "parser.h"
#include "imp.h"
#include "garr.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
};

typedef struct ONState {
	uint32_t flags;
	const SGS_IMPOperatorData *in_odata;
	SGS_ProgramOperatorData *out_odata;
} ONState;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
	VN_EXEC = 1<<1
};

SGS_GArr_DEF(OpRefArr, SGS_ProgramOpRef)

typedef struct VNState {
	uint32_t flags;
	const SGS_IMPVoiceData *in_vdata;
	SGS_ProgramVoiceData *out_vdata;
} VNState;

typedef struct IMPConv {
	OpRefArr op_list;
	SGS_IMP *imp;
	SGS_Program *program;
	ONState *osa;
	VNState *vsa;
	uint32_t time_ms;
	size_t odata_id;
	size_t vdata_id;
} IMPConv;

static void fini_IMPConv(IMPConv *o);

static bool init_IMPConv(IMPConv *o, SGS_Program *prg, SGS_IMP *imp) {
	size_t i;

	/* init */
	*o = (IMPConv){0};
	o->imp = imp;
	o->program = prg;
	i = imp->op_count;
	if (i > 0) {
		o->osa = calloc(i, sizeof(ONState));
		if (!o->osa) {
			fini_IMPConv(o);
			return false;
		}
	}
	i = imp->vo_count;
	if (i > 0) {
		o->vsa = calloc(i, sizeof(VNState));
		if (!o->vsa) {
			fini_IMPConv(o);
			return false;
		}
	}

	return true;
}

static void fini_IMPConv(IMPConv *o) {
	OpRefArr_clear(&o->op_list);
	if (o->osa) {
		free(o->osa);
		o->osa = NULL;
	}
	if (o->vsa) {
		free(o->vsa);
		o->vsa = NULL;
	}
}

static void assign_blocks(IMPConv *o, uint32_t vo_id);
static void traverse_opgraph(IMPConv *o, SGS_ProgramOpRef *op,
		uint32_t block_count);

static void traverse_event(IMPConv *o, size_t i) {
	const SGS_IMPEvent **im_events, *im_e;
	SGS_ProgramEvent *e;
	im_events = (const SGS_IMPEvent**) SGS_PList_ITEMS(&o->imp->ev_list);
	im_e = im_events[i];
	e = &o->program->events[i];
	o->time_ms += im_e->wait_ms;
	e->wait_ms = im_e->wait_ms;
	e->params = im_e->params;

	const SGS_IMPOperatorData *im_od = im_e->op_data;
	const SGS_IMPVoiceData *im_vd = im_e->vo_data;
	uint32_t vo_id = im_e->vo_id;
	e->voice_id = vo_id;
	if (im_od) {
		uint32_t op_id = im_od->op_id;
		SGS_ProgramOperatorData *od;
		ONState *ostate = &o->osa[op_id];
		ostate->in_odata = im_od;
		od = &o->program->odata_nodes[o->odata_id++];
		od->operator_id = op_id;
		e->operator = od;
		ostate->out_odata = od;
	}
	if (im_vd) {
		SGS_ProgramVoiceData *vd;
		VNState *vstate = &o->vsa[vo_id];
		vstate->in_vdata = im_vd;
		vd = &o->program->vdata_nodes[o->vdata_id++];
		//vd->voice_id = vo_id;
		e->voice = vd;
		vstate->out_vdata = vd;
	}
	printf("test\n");
	assign_blocks(o, vo_id);
}

/*
 */
static void assign_blocks(IMPConv *o, uint32_t vo_id) {
	SGS_ProgramOpRef op = {0, SGS_OP_CARR};
	VNState *vs = &o->vsa[vo_id];
	const SGS_IMPVoiceData *im_vd = vs->in_vdata;
	uint32_t i;
	if (!im_vd || !im_vd->graph) return;
	printf("assign blocks\n");
	uint32_t old_block_count = o->program->block_count;
	o->op_list.count = 0;
	for (i = 0; i < im_vd->graph->opc; ++i) {
		op.id = im_vd->graph->ops[i];
		printf("visit node %d\n", op.id);
		traverse_opgraph(o, &op, 0);
	}
	printf("need %d blocks (old count %d)\n",
		o->program->block_count, old_block_count);
	if (vs->out_vdata->op_list) {
		free((SGS_ProgramOpRef*) vs->out_vdata->op_list);
		printf("DOUBLE OP_LIST\n");
	}
	if (o->op_list.count) {
		OpRefArr_dupa(&o->op_list,
			(SGS_ProgramOpRef**) &vs->out_vdata->op_list);
	}
	vs->out_vdata->op_count = o->op_list.count;
}

/*
 */
static void traverse_opgraph(IMPConv *o, SGS_ProgramOpRef *op,
		uint32_t block_count) {
	ONState *ostate = &o->osa[op->id];
	const SGS_IMPOperatorData *im_od = ostate->in_odata;
	uint32_t i;
	if ((ostate->flags & ON_VISITED) != 0) {
		fprintf(stderr,
"skipping operator %d; does not support circular references\n",
			op->id);
		return;
	}
	SGS_ProgramOperatorData *od = ostate->out_odata;
	od->output_block_id = block_count++;
	od->freq_block_id = block_count++;
	od->amp_block_id = block_count++;
	od->freq_mod_block_id = block_count++;
	od->phase_mod_block_id = block_count++;
	od->amp_mod_block_id = block_count++;
	if (im_od->adjcs) {
		SGS_ProgramOpRef mod_op;
		const uint32_t *mods = im_od->adjcs->adjcs;
		uint32_t modc = 0;
		ostate->flags |= ON_VISITED;
		i = 0;
		modc += im_od->adjcs->fmodc;
		for (; i < modc; ++i) {
			mod_op.id = mods[i];
			mod_op.use = SGS_OP_FMOD;
//			printf("visit fmod node %d\n", mod_op.id);
			traverse_opgraph(o, &mod_op, block_count - 3);
		}
		modc += im_od->adjcs->pmodc;
		for (; i < modc; ++i) {
			mod_op.id = mods[i];
			mod_op.use = SGS_OP_PMOD;
//			printf("visit pmod node %d\n", mod_op.id);
			traverse_opgraph(o, &mod_op, block_count - 2);
		}
		modc += im_od->adjcs->amodc;
		for (; i < modc; ++i) {
			mod_op.id = mods[i];
			mod_op.use = SGS_OP_AMOD;
//			printf("visit amod node %d\n", mod_op.id);
			traverse_opgraph(o, &mod_op, block_count - 1);
		}
		ostate->flags &= ~ON_VISITED;
	}
	if (block_count > o->program->block_count) {
		o->program->block_count = block_count;
	}
	OpRefArr_add(&o->op_list, op);
}

static SGS_Program *alloc_program(SGS_IMP *imp) {
	SGS_Program *o;
	SGS_ParseResult *parse = imp->parse;
	size_t i;

	o = calloc(1, sizeof(SGS_Program));
	if (!o) return NULL;
	i = imp->ev_list.count;
	if (i > 0) {
		o->events = calloc(i, sizeof(SGS_ProgramEvent));
		if (!o->events) {
			SGS_destroy_Program(o);
			return NULL;
		}
	}
	o->event_count = i;
	o->operator_count = imp->op_count;
	o->voice_count = imp->vo_count;
	i = imp->odata_count;
	if (i > 0) {
		o->odata_nodes = calloc(i, sizeof(SGS_ProgramOperatorData));
		if (!o->odata_nodes) {
			SGS_destroy_Program(o);
			return NULL;
		}
	}
	i = imp->vdata_count;
	if (i > 0) {
		o->vdata_nodes = calloc(i, sizeof(SGS_ProgramVoiceData));
		if (!o->vdata_nodes) {
			SGS_destroy_Program(o);
			return NULL;
		}
	}
	o->name = parse->name;
	if (!(parse->sopt.changed & SGS_PSSO_AMPMULT)) {
		/*
		 * Enable amplitude scaling (division) by voice count,
		 * handled by sound generator.
		 */
		o->flags |= SGS_PROG_AMP_DIV_VOICES;
	}

	return o;
}

/**
 * Create instance for the given parser output.
 *
 * Returns instance if successful, NULL on error.
 */
SGS_Program* SGS_create_Program(SGS_ParseResult *parse) {
	IMPConv ic;
	SGS_Program *o;
	SGS_IMP *imp;
	size_t i;

	imp = SGS_create_IMP(parse);
	if (!imp) return NULL;
	o = alloc_program(imp);
	if (!o) {
		SGS_destroy_IMP(imp);
		return NULL;
	}
	if (!init_IMPConv(&ic, o, imp)) {
		SGS_destroy_Program(o);
		SGS_destroy_IMP(imp);
		return NULL;
	}
	printf("begin traversal\n");
	for (i = 0; i < o->event_count; ++i) {
		printf("traverse event %ld\n", i);
		traverse_event(&ic, i);
	}
	fini_IMPConv(&ic);
	SGS_destroy_IMP(imp);
#if 1
	SGS_Program_print_info(o);
#endif
	return o;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Program(SGS_Program *o) {
	if (o->events) free(o->events);
	if (o->odata_nodes) free(o->odata_nodes);
	if (o->vdata_nodes) free(o->vdata_nodes);
}

static void print_linked(const char *header, const char *footer,
		uint32_t count, const uint32_t *nodes) {
	uint32_t i;
	if (!count) return;
	printf("%s%d", header, nodes[0]);
	for (i = 0; ++i < count; )
		printf(", %d", nodes[i]);
	printf("%s", footer);
}

/**
 * Print information about program contents. Useful for debugging.
 */
void SGS_Program_print_info(SGS_Program *o) {
	printf("Program: \"%s\"\n", o->name);
	printf("\tevents: %ld\tvoices: %hd\toperators: %d\n",
		o->event_count, o->voice_count, o->operator_count);
	for (size_t event_id = 0; event_id < o->event_count; ++event_id) {
		const SGS_ProgramEvent *oe;
		const SGS_ProgramVoiceData *ovo;
		const SGS_ProgramOperatorData *oop;
		oe = &o->events[event_id];
		ovo = oe->voice;
		oop = oe->operator;
		printf("\\%d \tEV %ld \t(VI %d)",
			oe->wait_ms, event_id, oe->voice_id);
		if (ovo) {
			const SGS_ProgramGraph *g = ovo->graph;
			printf("\n\tvo %d", oe->voice_id);
			if (g)
				print_linked("\n\t    {", "}", g->opc, g->ops);
		}
		if (oop) {
			const SGS_ProgramGraphAdjcs *ga = oop->adjcs;
			if (oop->time_ms == SGS_TIME_INF)
				printf("\n\top %d \tt=INF \tf=%.f",
					oop->operator_id, oop->freq);
			else
				printf("\n\top %d \tt=%d \tf=%.f",
					oop->operator_id, oop->time_ms, oop->freq);
			if (ga) {
				print_linked("\n\t    f!<", ">", ga->fmodc,
					ga->adjcs);
				print_linked("\n\t    p!<", ">", ga->pmodc,
					&ga->adjcs[ga->fmodc]);
				print_linked("\n\t    a!<", ">", ga->amodc,
					&ga->adjcs[ga->fmodc + ga->pmodc]);
			}
		}
		putchar('\n');
	}
}
