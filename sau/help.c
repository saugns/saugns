/* SAU library: Help data and printout code.
 * Copyright (c) 2020-2023 Joel K. Pettersson
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

#include <sau/help.h>
#include <sau/math.h>
#include <sau/line.h>
#include <sau/wave.h>
#include <sau/program.h>
#include <string.h>

const char *const sauHelp_names[SAU_HELP_NAMED + 1] = {
	SAU_HELP__ITEMS(SAU_HELP__X_NAME)
	NULL
};

#define SAU_HELP__X_CASE(NAME, ARRAY) \
	case SAU_HELP_N_##NAME: return SAU_HELP__X_ARRAY(NAME, ARRAY);

/**
 * Get name array for \p str help category.
 *
 * \return predefined array or NULL if none
 */
const char *const *sau_find_help(const char *restrict str) {
	size_t i;
	if (!sau_find_name(sauHelp_names, str, &i))
		return NULL;
	switch (i) {
	SAU_HELP__ITEMS(SAU_HELP__X_CASE)
	}
	return NULL;
}

/**
 * Find \p str in \p namearr, setting \p id to the index.
 * If not found, simply return false.
 *
 * \return true if found
 */
bool sau_find_name(const char *const *restrict namearr,
		const char *restrict str, size_t *restrict id) {
	if (!str)
		return false;
	for (size_t i = 0; namearr[i] != NULL; ++i) {
		if (!strcmp(namearr[i], str)) {
			*id = i;
			return true;
		}
	}
	return false;
}

/**
 * Print strings from \p namearr until a NULL entry is reached.
 *
 * The format for items printed is a comma-separated list. If
 * any items are printed, \p headstr is printed before them,
 * and a newline after them. If no items are printed, nothing
 * is. (A NULL \p headstr is interpreted as an empty string.)
 *
 * \return true if any items printed
 */
bool sau_print_names(const char *const *restrict namearr,
		const char *restrict headstr,
		FILE *restrict out) {
	if (!namearr[0])
		return false;
	size_t i = 0, len = 0;
	if (!headstr)
		headstr = "";
	for (const char *name; (name = namearr[i]) != NULL; ++i) {
		if (len > 0 && len < 56)
			len += fprintf(out, ", %s", name);
		else
			len = fprintf(out, (i > 0) ? ",\n%s%s" : "%s%s",
					headstr, name);
	}
	putc('\n', out);
	return true;
}
