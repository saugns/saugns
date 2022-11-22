/* mgensys: Script data post-parse handling.
 * Copyright (c) 2011, 2019-2020 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "parser.h"

static void time_sound(MGS_ProgramNode *restrict n) {
	MGS_ProgramSoundData *sound = n->data;
	if (!(sound->time.flags & MGS_TIME_SET)) {
		if (n->base_id != sound->root->base_id)
			sound->time.flags |= MGS_TIME_SET;
	}
	// handle timing for sub-components here
	// handle timing for added silence here
}

/*
 * Adjust timing for a duration scope; the syntax for such time grouping is
 * only allowed on the top scope, so the algorithm only deals with this for
 * the nodes involved.
 */
static void time_durscope(MGS_ProgramDurData *restrict dur) {
	MGS_ProgramNode *n_after = dur->scope.last_node->next;
	double delay = 0.f, delaycount = 0.f;
	MGS_ProgramNode *step;
	for (step = dur->scope.first_node; step != n_after; ) {
		MGS_ProgramSoundData *sound;
		sound = MGS_ProgramNode_get_data(step, MGS_BASETYPE_SOUND);
		/*
		 * Skip unsupported nodes, and
		 * exclude nested nodes from duration.
		 */
		if (!sound || (step->base_id != sound->root->base_id)) {
			step = step->next;
			continue;
		}
		if (step->next == n_after) {
			/* accept pre-set default time for last node */
			sound->time.flags |= MGS_TIME_SET;
		}
		if (delay < sound->time.v)
			delay = sound->time.v;
		step = step->next;
		if (step != NULL) {
			delaycount += step->delay;
		}
	}
	for (step = dur->scope.first_node; step != n_after; ) {
		MGS_ProgramSoundData *sound;
		sound = MGS_ProgramNode_get_data(step, MGS_BASETYPE_SOUND);
		/*
		 * Skip unsupported nodes, and
		 * exclude nested nodes from duration.
		 */
		if (!sound || (step->base_id != sound->root->base_id)) {
			step = step->next;
			continue;
		}
		if (!(sound->time.flags & MGS_TIME_SET)) {
			/* fill in sensible default time */
			sound->time.v = delay + delaycount;
			sound->time.flags |= MGS_TIME_SET;
		}
		step = step->next;
		if (step != NULL) {
			delaycount -= step->delay;
		}
	}
	if (n_after != NULL)
		n_after->delay += delay;
}

void MGS_adjust_node_list(MGS_ProgramNode *restrict list) {
	MGS_ProgramNode *n = list;
	MGS_ProgramDurData *dur = NULL;
	while (n != NULL) {
		if (n->type == MGS_TYPE_DUR) {
			dur = n->data;
			n = n->next;
			continue;
		}
		if (n->base_type == MGS_BASETYPE_SOUND) time_sound(n);
		if (n == dur->scope.last_node) time_durscope(dur);
		n = n->next;
	}
}
