/* Object module -- mgensys version. Originally from the FLPTK library
 * (FLTK-2 fork), later spun off into the SCOOP library, then reworked.
 *
 * Copyright (c) 2010-2011, 2013, 2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "Object.h"
#include "mempool.h"
#include <string.h>

static void blank_destructor(void *meta)
{
	(void)meta; /* do nothing */
}

static void pure_virtual(void)
{
	mgs_fatal(NULL, "pure virtual method called!");
}

/* recursively fills in blank parts of meta type instance chain */
static void init_meta(mgsObject_Meta *o)
{
	void (**virt)() = (void (**)()) &o->virt,
			 (**super_virtab)() = 0;
	unsigned int i = 0, max;
	if (o->super) {
		if (!o->super->done)
			init_meta((mgsObject_Meta*)o->super);
		super_virtab = (void (**)()) &o->super->virt;
		for (max = o->super->vnum; i < max; ++i)
			if (!virt[i]) virt[i] = super_virtab[i];
	}
	if (o->vtinit)
		o->vtinit(o);
	if (i == 0)
		if (!virt[i]) virt[i] = blank_destructor;
	for (max = o->vnum; ++i < max; )
		if (!virt[i]) virt[i] = pure_virtual;
	o->done = 1;
}

void* mgs_raw_new(void *mem, void *_meta)
{
	mgsObject_Meta *meta = _meta;
	if (!mem) {
		if (!(mem = calloc(1, meta->size)))
			return 0;
	} else {
		memset(mem, 0, meta->size);
	}
	if (!meta->done) init_meta(meta);
	mgs_set_meta(mem, meta);
	return mem;
}

void* mgs_raw_mpnew(struct mgsMemPool *mp, void *_meta)
{
	mgsObject_Meta *meta = _meta;
	void *mem;
	if (!(mem = mgs_mpalloc(mp, meta->size)))
		return 0;
	if (!meta->done) init_meta(meta);
	if (meta->virt.dtor != blank_destructor &&
	    !mgs_mpregdtor(mp, meta->virt.dtor, mem))
		return 0;
	mgs_set_meta(mem, meta);
	return mem;
}

void mgs_delete(void *o)
{
	const mgsObject_Meta *meta = mgs_meta(o);
	if (meta->virt.dtor != blank_destructor)
		meta->virt.dtor(o);
	free(o);
}

void mgs_finalize(void *o)
{
	const mgsObject_Meta *meta = mgs_meta(o);
	if (meta->virt.dtor != blank_destructor)
		meta->virt.dtor(o);
	mgs_set_metaof(o, mgsNone);
}

/* core of type comparison */
int mgs_rtticheck(const void *submeta, const void *meta)
{
	const mgsObject_Meta *subclass = submeta, *class = meta;
	if (subclass == class)
		return 0;
	do {
		subclass = subclass->super;
		if (subclass == class)
			return 1;
	} while (subclass);
	return -1;
}
