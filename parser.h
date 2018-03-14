/* sgensys: Script parser module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"
#include "ptrarr.h"

/*
 * Parsing nodes.
 */

enum {
	/* Operator node parse flags */
	POD_OPERATOR_LATER_USED = 1<<0,
	POD_MULTIPLE_OPERATORS = 1<<1,
	POD_OPERATOR_NESTED = 1<<2,
	POD_TIME_DEFAULT = 1<<3,
	POD_SILENCE_ADDED = 1<<4,
};

struct SGS_ParseOperatorData {
	struct SGS_ParseEventData *event;
	struct SGS_PtrArr on_next; /* all immediate forward refs for op(s) */
	struct SGS_ParseOperatorData *on_prev; /* preceding for same op(s) */
	struct SGS_ParseOperatorData *next_bound;
	uint32_t on_flags;
	const char *label;
	/* operator parameters */
	uint32_t operator_id; /* not used by parser; for builder */
	uint32_t operator_params;
	uint8_t attr;
	uint8_t wave;
	int32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	struct SGS_ProgramValit valitfreq, valitamp;
	/* node adjacents in operator linkage graph */
	struct SGS_PtrArr fmods, pmods, amods;
};

enum {
	/* Event node parse flags */
	PED_VOICE_LATER_USED = 1<<0,
	PED_ADD_WAIT_DURATION = 1<<1,
};

struct SGS_ParseEventData {
	struct SGS_ParseEventData *next;
	struct SGS_ParseEventData *groupfrom;
	struct SGS_ParseEventData *composite;
	int32_t wait_ms;
	struct SGS_PtrArr operators; /* operators included in event */
	uint32_t en_flags;
	/* voice parameters */
	uint32_t voice_id; /* not used by parser; for builder */
	uint32_t voice_params;
	struct SGS_ParseEventData *voice_prev; /* preceding event for voice */
	uint8_t voice_attr;
	float panning;
	struct SGS_ProgramValit valitpanning;
	struct SGS_PtrArr graph;
};

void SGS_event_node_destroy(struct SGS_ParseEventData *e);

struct SGS_ParseList {
	struct SGS_ParseEventData *events;
	struct SGS_ParseList *next;
};

struct SGS_Parser;
typedef struct SGS_Parser *SGS_Parser_t;

SGS_Parser_t SGS_create_parser(void);
void SGS_destroy_parser(SGS_Parser_t o);

struct SGS_ParseList *SGS_parser_process(SGS_Parser_t o, const char *filename);
struct SGS_ParseList *SGS_parser_get_results(SGS_Parser_t o);
void SGS_parser_clear(SGS_Parser_t o);
