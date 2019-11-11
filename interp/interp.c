/* saugns: Audio program interpreter module.
 * Copyright (c) 2013-2014, 2018-2019 Joel K. Pettersson
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

#include "interp.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct SAU_Interp {
	SAU_Program *program;
	SAU_Result *result;
	uint32_t time_ms;
	size_t odata_id;
	size_t vdata_id;
} SAU_Interp;

static void end_program(SAU_Interp *restrict o) {
}

static void handle_event(SAU_Interp *restrict o, size_t i) {
	const SAU_ProgramEvent *pe = &o->program->events[i];
	SAU_ResultEvent *re = &o->result->events[i];
	o->time_ms += pe->wait_ms;
	re->wait_ms = pe->wait_ms;

	const SAU_ProgramOpData *pod = pe->op_data;
	const SAU_ProgramVoData *pvd = pe->vo_data;
	uint32_t vo_id = pe->vo_id;
	if (pod) {
		uint32_t op_id = pod->id;
		SAU_ResultOperatorData *rod;
	}
	if (pvd) {
		SAU_ResultVoiceData *rvd;
	}
}

static SAU_Result *SAU_Interp_run(SAU_Interp *restrict o,
		SAU_Program *restrict program) {
	size_t i;
	SAU_Result *res = NULL;

	/* init */
	o->program = program;
	i = program->ev_count;
	res = calloc(1, sizeof(SAU_Result));
	if (!res) {
		end_program(o);
		return NULL;
	}
	o->result = res;
	if (i > 0) {
		res->events = calloc(i, sizeof(SAU_ResultEvent));
		if (!res->events) {
			end_program(o);
			return NULL;
		}
	}
	res->ev_count = i;
	i = program->op_count;
	if (i > 0) {
	}
	res->op_count = i;
	i = program->vo_count;
	if (i > 0) {
	}
	res->vo_count = i;
	i = 0;//program->odata_count;
	if (i > 0) {
		res->odata_nodes = calloc(i, sizeof(SAU_ResultOperatorData));
		if (!res->odata_nodes) {
			end_program(o);
			return NULL;
		}
	}
	i = 0;//program->vdata_count;
	if (i > 0) {
		res->vdata_nodes = calloc(i, sizeof(SAU_ResultVoiceData));
		if (!res->vdata_nodes) {
			end_program(o);
			return NULL;
		}
	}
	res->mode = program->mode;
	res->name = program->name;
	o->time_ms = 0;
	o->odata_id = 0;
	o->vdata_id = 0;

	/* run */
	for (i = 0; i < program->ev_count; ++i) {
		handle_event(o, i);


	}

	/* cleanup */
	end_program(o);
	return res;
}

static SAU_Result *run_program(SAU_Program *restrict program) {
	SAU_Interp ip = {0};
	return SAU_Interp_run(&ip, program);
}

/**
 * Interpret the listed programs, adding each result (even if NULL)
 * to the result list. (NULL programs are ignored.)
 *
 * \return number of failures for non-NULL programs
 */
size_t SAU_interpret(const SAU_PtrList *restrict prg_objs,
		SAU_PtrList *restrict res_objs) {
	size_t fails = 0;
	SAU_Program **prgs = (SAU_Program**) SAU_PtrList_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		if (!prgs[i]) continue;
		SAU_Result *res = run_program(prgs[i]);
		if (!res) ++fails;
		SAU_PtrList_add(res_objs, res);
	}
	return fails;
}
