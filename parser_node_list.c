/* sgensys script parser module.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

/*
 * Add a node to the given node list.
 */
void SGS_node_list_add(struct SGSNodeList *list, void *node) {
  if (list->count == 0) {
    list->count = 1;
    list->alloc = 1;
    list->data = malloc(sizeof(void *));
    list->data[0] = node;
    return;
  }

  list->alloc <<= 1;
  if (list->count == list->inherit_count) {
    void *old_list = list->data;
    list->data = malloc(sizeof(void *) * list->alloc);
    memcpy(list->data, old_list,
           sizeof(void *) * (list->count - 1));
  } else {
    list->data = realloc(list->data,
                         sizeof(struct SGSOperatorNode*) * list->alloc);
  }
  ++list->count;
  list->data[list->count - 1] = node;
}

/*
 * Clear the given node list.
 */
void SGS_node_list_clear(struct SGSNodeList *list) {
  if (list->count > 1 &&
      list->count > list->inherit_count) free(list->data);
  list->count = 0;
  list->alloc = 0;
  list->inherit_count = 0;
  list->data = 0;
}

/*
 * Copy the node list src to dst (clearing dst first if needed); to save
 * memory, dst will actually merely reference the data in src unless/until
 * added to.
 *
 * This is a "safe copy", meaning the copied node entries at the beginning
 * of the list will remain "inactive" - SGS_node_list_rforeach() as well as
 * the cleanup code of the owner of the list will ignore them, avoiding
 * duplicate operations.
 *
 * Manual (read-only) access of the list will still give access to the
 * "inactive" nodes, unless deliberately beginning iteration at
 * inherit_count.
 */
void SGS_node_list_inherit(struct SGSNodeList *dst, const struct SGSNodeList *src) {
  SGS_node_list_clear(dst);
  dst->count = src->count;
  dst->inherit_count = src->count;
  dst->data = src->data;
}
