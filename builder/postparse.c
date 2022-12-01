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

static void time_sound(mgsProgramData *restrict n) {
	mgsProgramSoundData *sound = (mgsProgramSoundData*) n;
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
static void time_durscope(mgsProgramDurData *restrict dur) {
	mgsProgramData *n_last = dur->last_node, *n_after = n_last->next;
	double delay = 0.f, delaycount = 0.f;
	mgsProgramData *step;
	for (step = dur->first_node; step != n_after; ) {
		mgsProgramSoundData *sound;
		sound = mgs_of_class(step, mgsProgramSoundData);
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
	for (step = dur->first_node; step != n_after; ) {
		mgsProgramSoundData *sound;
		sound = mgs_of_class(step, mgsProgramSoundData);
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

void mgs_adjust_node_list(mgsProgramData *restrict list) {
	mgsProgramData *n = list;
	mgsProgramDurData *cur_dur = NULL;
	while (n != NULL) {
		mgsProgramDurData *dur = mgs_of_class(n, mgsProgramDurData);
		if (dur) {
			cur_dur = dur;
			n = n->next;
			continue;
		}
		if (n->base_type == MGS_BASETYPE_SOUND) time_sound(n);
		if (n == cur_dur->last_node) time_durscope(cur_dur);
		n = n->next;
	}
}
