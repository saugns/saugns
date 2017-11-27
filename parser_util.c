/* sgensys: Script parser utility functions.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
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

/*
 * General-purpose functions
 */

static void *memdup(const void *src, size_t size) {
  void *ret = malloc(size);
  if (!ret) return 0;
  memcpy(ret, src, size);
  return ret;
}

#ifdef strdup /* deal with libc issues when already provided */
# undef strdup
#endif
#define strdup _strdup
static char *_strdup(const char *src) {
  size_t len = strlen(src);
  char *ret;
  if (!len) return 0;
  ret = memdup(src, len + 1);
  if (!ret) return 0;
  ret[len] = '\0';
  return ret;
}

#define IS_WHITESPACE(c) \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

static bool testc(char c, FILE *f) {
  char gc = getc(f);
  ungetc(gc, f);
  return (gc == c);
}

static bool testgetc(char c, FILE *f) {
  char gc;
  if ((gc = getc(f)) == c) return true;
  ungetc(gc, f);
  return false;
}

static int32_t getinum(FILE *f) {
  char c;
  int32_t num = -1;
  c = getc(f);
  if (c >= '0' && c <= '9') {
    num = c - '0';
    for (;;) {
      c = getc(f);
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  ungetc(c, f);
  return num;
}

static int32_t strfind(FILE *f, const char *const*str) {
  int32_t search, ret;
  uint32_t i, len, pos, matchpos;
  char c, undo[256];
  uint32_t strc;
  const char **s;
  for (len = 0, strc = 0; str[strc]; ++strc)
    if ((i = strlen(str[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = str[i];
  search = ret = -1;
  pos = matchpos = 0;
  while ((c = getc(f)) != EOF) {
    undo[pos] = c;
    for (i = 0; i < strc; ++i) {
      if (!s[i]) continue;
      else if (!s[i][pos]) {
        s[i] = 0;
        if (search == (int32_t)i) {
          ret = i;
          matchpos = pos-1;
        }
      } else if (c != s[i][pos]) {
        s[i] = 0;
        search = -1;
      } else
        search = i;
    }
    if (pos == len) break;
    ++pos;
  }
  free(s);
  for (i = pos; i > matchpos; --i) ungetc(undo[i], f);
  return ret;
}

static void eatws(FILE *f) {
  char c;
  while ((c = getc(f)) == ' ' || c == '\t') ;
  ungetc(c, f);
}
