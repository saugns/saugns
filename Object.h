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

#pragma once
#include "common.h"
#ifndef MGS_API /* not yet used */
# define MGS_API
#endif
#ifndef MGS_USERAPI /* not yet used */
# define MGS_USERAPI
#endif
#ifndef MGS_TYPE
# define MGS_TYPE void
#endif
struct mgsMemPool;
#include <stdlib.h>
#include <stdarg.h>

/** \file
   The API for the object model and system. It provides support for the
   following:
   - Class declaration and definition
   - Single inheritance (by direct inclusion of the members of the supertype
     into the subtype)
   - Virtual functions
   - Explicit RTTI checks

   In addition to such "full-fledged" types, support is also provided for
   declaring simple structs with single inheritance (by direct inclusion of
   members). Whether this or a "real" class makes sense to use depends on
   the specific requirements of each case.

   Classes can be instantiated using either static or dynamic memory
   allocations. mgs_finalize() is used to destroy the instance without
   deallocating its memory, while mgs_delete() also deallocates its memory.

   Instance creation functions for a given class are declared and defined
   using a convenience macro for brevity, and use a utility function to deal
   with memory allocation and initialization of the instance.

   There is no need to "register" a class before allocating an instance. The
   meta type will become fully initialized the first time an instance
   is allocated.

   A note on the API naming convention:
   - Declarations and definitions meant to mimic new keywords are named
     with an "MGS"-prefix (no underscore follows), the rest of the name
     then in lowercase. E.g., MGSclassdef.
   - Functions and macros that behave like functions are named with an
     "mgs_"-prefix and the rest of the name in lowercase, unless methods
     of a class.
   - Macros that don't behave like functions are generally named with an
     "mgs_"-prefix and the rest of the name in uppercase, unless methods
     of a class.
   - For methods of a class, the name of the class, followed by an
     underscore, prefixes the name. If the method is a macro which doesn't
     behave like a function, then the rest of the name is in all-uppercase
     letters; otherwise, the rest of the name is in all-lowercase letters.
 */

/*
 * OOC inheritance-enabled structs.
 */

/** Declare a type *without* a meta type, i.e. a plain struct.
  * (There is no support for passing such data to mgs_delete(),
  * mgs_finalize(), or trying to use RTTI or virtual functions.)
  *
  * \p Name is the name of the type to declare. The existence of a
  * macro, having the same name except with an appended underscore, is
  * expected. That macro should list all the members of the type, the
  * way they are listed within a struct declaration; it will be
  * referenced as a means of declaring the type.
  *
  * A member list macro can reference one other MGSstructdef()
  * type's member list macro at the beginning, giving rise to single
  * inheritance and allowing type-cast pseudo-compatibility. (Padding
  * at the end of a base type struct may overlap with members added
  * in a derived type struct. To avoid this, inherited members can be
  * wrapped in an anonymous struct. This works for non-class types.)
  */
#define MGSstructdef(Name) \
typedef struct Name { Name##_ } Name

/*
 * OOC full-fledged classes.
 */

/** Declare a type with an associated meta type, i.e. a class.
  * (Such data can be passed to mgs_delete(), mgs_finalize(), and the
  * RTTI and virtual table system used.) Each such struct begins
  * with a \a meta pointer to its meta type.
  *
  * \p Name is the name of the type to declare. The existence of a
  * macro, having the same name except with an appended underscore, is
  * expected. That macro should list all the members of the type, the
  * way they are listed within a struct declaration; it will be
  * referenced as a means of declaring the type.
  *
  * A member list macro can reference one other MGSclassdef()/MGSclasstype()
  * type's member list macro at the beginning, giving rise to single
  * inheritance and allowing type-cast pseudo-compatibility.
  *
  * \ref MGSclassdef() combines this and \ref MGSmetatype() into a single
  * keyword-like macro.
  */
#define MGSclasstype(Name) \
typedef struct Name { const struct Name##_Meta *meta; Name##_ } Name

#ifndef MGS__DTOR_F_DEFINED
# define MGS__DTOR_F_DEFINED
/** Class destructor function pointer type. */
typedef void (*mgsDtor_f)(void *o);
#endif

/** Meta type vtable initializer function pointer type.
    The meta type instance is expected as the \p o argument. */
typedef void (*mgsVtinit_f)(void *o);

/** Declare a meta type for a type declared with MGSclassdef();
  * the name of this type seldom needs to be explicitly referenced,
  * but is the same as that of the class with _Meta appended.
  *
  * This version \a does \a not forward-declare the corresponding global
  * instance made by \ref MGSmetainst() for symbol export.
  *
  * \see MGSmetatype()
  *
  * _MGSclassdef() combines this and MGSclasstype() into a single step.
  */
#define _MGSmetatype(Class) \
typedef struct Class##_Virt { mgsDtor_f dtor; Class##__ } Class##_Virt; \
typedef struct Class##_Meta { \
	const struct mgsObject_Meta *super; \
	size_t size; \
	unsigned short vnum; \
	unsigned char done; \
	const char *name; \
	mgsVtinit_f vtinit; /* virtual table init function, passed meta */ \
	Class##_Virt virt; \
} Class##_Meta

/** Declare a meta type for a type declared with MGSclassdef().
  * the name of this type seldom needs to be explicitly referenced,
  * but is the same as that of the class with _Meta appended.
  *
  * This version \a will \a also forward-declare the corresponding global
  * instance made by MGSmetainst() for symbol export, as is done for
  * public APIs. To avoid that, use \ref _MGSmetatype() instead.
  *
  * The declaration uses the corresponding macro listing virtual
  * methods - named the same as the class except for having \a two
  * appended underscores - which should contain a sequence of
  * function pointer declarations.
  *
  * That macro can reference one other such macro at the beginning of
  * its contents for (single) inheritance - and this must be done when
  * inheriting from another class. The resulting list of function
  * pointers form the contents of the virtual table data structure for the
  * class, named the same as the class except with _Virt appended.
  *
  * MGSclassdef() combines this and MGSclasstype() into a single step.
  */
#define MGSmetatype(Class) \
_MGSmetatype(Class); \
MGS_USERAPI extern Class##_Meta Class##__meta

/** Get the global meta type instance of the \p Class named.
  *
  * This requires it to have been either forward-declared with
  * MGSmetatype() (as is done for public APIs) when the class was
  * defined - or to have been otherwise defined earlier in the same
  * module if part of a non-public API.
  *
  * Supplying the keyword \a mgsNone as the class will produce a NULL
  * pointer.
  */
#define mgs_metaof(Class) (&(Class##__meta))

/** This combines MGSclasstype() and _MGSmetatype() to declare a class
  * and its meta type at once.
  * \see MGSclasstype()
  * \see _MGSmetatype()
  */
#define _MGSclassdef(Class) \
MGSclasstype(Class); \
_MGSmetatype(Class)

/** This combines MGSclasstype() and MGSmetatype() to declare a class
  * and its meta type at once - and forward-declare the symbol of
  * the MGSmetainst() definition of the global meta type for
  * export, as is done for public APIs.
  * \see MGSclasstype()
  * \see MGSmetatype()
  */
#define MGSclassdef(Class) \
MGSclasstype(Class); \
MGSmetatype(Class)

/** Use to declare a set of allocation and constructor functions for a
  * class if they do not take variable arguments. This version is used
  * to forward-declare a static set (not part of any visible API).
  *
  * \see MGSctordec()
  */
#define _MGSctordec(Class, FunctionName, NameSuffix, Parlist) \
static Class* FunctionName##_new##NameSuffix Parlist; \
static Class* FunctionName##_mpnew##NameSuffix \
MGS_SUBST_HEAD(struct mgsMemPool *mp, Parlist); \
static inline unsigned char FunctionName##_ctor##NameSuffix Parlist

/** Use to declare a set of allocation and constructor functions for a
  * class if they do not take variable arguments. They will include a
  * FunctionName_new() and a FunctionName_ctor() for \p FunctionName
  * (if \p NameSuffix is empty), and both functions take exactly the
  * the same arguments, the parameters given by \p Parlist.
  *
  * A FunctionName_mpnew() variation will also be included, which
  * replaces the first parameter with one for a mempool instance.
  *
  * Any number of these function sets may be declared and defined.
  *
  * \ref MGSctordef() is used to define an allocation/construction
  * function set, whether or not it was forward-declared using this.
  *
  * See \ref MGSctordef() for further details.
  */
#define MGSctordec(Class, FunctionName, NameSuffix, Parlist) \
MGS_USERAPI Class* FunctionName##_new##NameSuffix Parlist; \
MGS_USERAPI Class* FunctionName##_mpnew##NameSuffix \
MGS_SUBST_HEAD(struct mgsMemPool *mp, Parlist); \
MGS_USERAPI unsigned char FunctionName##_ctor##NameSuffix Parlist

/** Use to define a set of allocation and constructor functions for a
  * class if they do not take variable arguments. This version is used
  * to forward-declare a static set (not part of any visible API).
  *
  * \see MGSctordef()
  */
#define _MGSctordef(Class, FunctionName, NameSuffix, Parlist, Arglist) \
static inline unsigned char FunctionName##_ctor##NameSuffix Parlist; \
static mgsMaybeUnused Class * \
FunctionName##_new##NameSuffix Parlist \
{ \
	void *MGSctordef__mem = (MGS_ARG1 Arglist); \
	if (((MGS_ARG1 Arglist) = \
	     mgs_raw_new(MGSctordef__mem, mgs_metaof(Class))) != NULL && \
	    !FunctionName##_ctor##NameSuffix Arglist) { \
		(MGSctordef__mem ? mgs_finalize : mgs_delete)( \
				(MGS_ARG1 Arglist)); \
		return 0; \
	} \
	return (MGS_ARG1 Arglist); \
} \
static mgsMaybeUnused Class * \
FunctionName##_mpnew##NameSuffix \
MGS_SUBST_HEAD(struct mgsMemPool *MGSctordef__mp, Parlist) \
{ \
	void *MGSctordef__mem; \
	if ((MGSctordef__mem = \
	     mgs_raw_mpnew(MGSctordef__mp, mgs_metaof(Class))) != NULL && \
	    !FunctionName##_ctor##NameSuffix \
	    MGS_SUBST_HEAD(MGSctordef__mem, Arglist)) { \
		return 0; \
	} \
	return MGSctordef__mem; \
} \
static inline mgsMaybeUnused unsigned char \
FunctionName##_ctor##NameSuffix Parlist

/** Use to define a set of allocation and constructor functions for a
  * class if they do not take variable arguments. They will include a
  * FunctionName_new() and a FunctionName_ctor() for \p FunctionName
  * (if \p NameSuffix is empty), and both functions take exactly the
  * same arguments, the parameters given by \p Parlist. \p Arglist is
  * used to pass arguments from one of them to the other, and should
  * be \p Parlist without types. The object pointer, whatever it is
  * named, must come first in \p Arglist.
  *
  * The FunctionName_new() function will first allocate zero'd memory
  * if its first, memory pointer argument is zero, otherwise zero and
  * (re)use the memory. Unless an error occurs, the meta type is set,
  * the corresponding FunctionName_ctor() function is called, and the
  * instance is thereafter returned. On error, either mgs_delete() or
  * mgs_finalize() is used depending on if memory had been allocated.
  *
  * The FunctionName_ctor() function is a constructor which takes a
  * valid memory block - zero'd and with the correct meta type
  * set. Its body is to be defined immediately after the macro
  * invocation, beginning with an opening curly brace; it should return
  * non-zero if construction successful, zero if construction failed.
  *
  * A FunctionName_mpnew() variation will also be included, which
  * replaces the first parameter with one for a mempool instance.
  *
  * Note that a constructor for a derived class must explicitly call a
  * constructor for the superclass at the beginning of the function.
  *
  * A caveat: Since subclass constructors call superclass constructors,
  * and the meta type remains that of the instantiated class,
  * this means a constructor might be called with the meta type
  * of a derived class. This affects the definition of virtual
  * functions - so a constructor should not use the virtual function
  * table when it depends on a specific version of the function it calls.
  * Instead, it should then make a direct call to the function as
  * defined for that class.
  *
  * Any number of these function sets may be declared and defined.
  *
  * \see MGSctordec() for simply declaring them as in a header.
  */
#define MGSctordef(Class, FunctionName, NameSuffix, Parlist, Arglist) \
unsigned char FunctionName##_ctor##NameSuffix Parlist; \
Class* FunctionName##_new##NameSuffix Parlist \
{ \
	void *MGSctordef__mem = (MGS_ARG1 Arglist); \
	if (((MGS_ARG1 Arglist) = \
	     mgs_raw_new(MGSctordef__mem, mgs_metaof(Class))) != NULL && \
	    !FunctionName##_ctor##NameSuffix Arglist) { \
		(MGSctordef__mem ? mgs_finalize : mgs_delete)( \
				(MGS_ARG1 Arglist)); \
		return 0; \
	} \
	return (MGS_ARG1 Arglist); \
} \
Class* FunctionName##_mpnew##NameSuffix \
MGS_SUBST_HEAD(struct mgsMemPool *MGSctordef__mp, Parlist) \
{ \
	void *MGSctordef__mem; \
	if ((MGSctordef__mem = \
	     mgs_raw_mpnew(MGSctordef__mp, mgs_metaof(Class))) != NULL && \
	    !FunctionName##_ctor##NameSuffix \
	    MGS_SUBST_HEAD(MGSctordef__mem, Arglist)) { \
		return 0; \
	} \
	return MGSctordef__mem; \
} \
unsigned char FunctionName##_ctor##NameSuffix Parlist

/** Define the global instance of the meta type for the class.
  * This version makes the symbol static (not part of a public API).
  *
  * \see MGSmetainst()
  */
#define _MGSmetainst static MGSmetainst

/** Define the global instance of the meta type for the class.
  *
  * \p Superclass should be \a mgsNone for base classes, otherwise the name
  * of the superclass.
  *
  * \p dtor should be the destructor function for the class if it
  * (re)defines one, otherwise NULL. A destructor should call the
  * destructor for the superclass, if any, at the end. If no
  * destructor has been set and none ends up inherited, unlike for
  * other virtual functions, a safe blank no-op function will be set
  * so that calling the superclass destructor using its dtor field is
  * always valid.
  *
  * \p vtinit should be a function setting any other pointers in the virt
  * structure for virtual functions (re)defined by the class. If no
  * virtual functions are (re)defined by the class, it can be NULL. If
  * provided, it will be called upon creation of the first instance of the
  * class, and given the meta type as the argument. It needn't (and
  * shouldn't) change any other pointers: definitions inherited from the
  * superclass are automatically copied, and "pure virtual" (i.e. as-yet
  * undefined) functions are automatically defined to prompt a fatal error
  * (using \ref mgs_fatal()) if called.
  */
#define MGSmetainst(Class, Superclass, dtor, vtinit) \
struct Class##_Meta Class##__meta = { \
	(mgsObject_Meta*)mgs_metaof(Superclass), \
	sizeof(Class), \
	(sizeof(Class##_Virt) / sizeof(void (*)())), \
	0, \
	#Class, \
	(mgsVtinit_f)vtinit, \
	{(mgsDtor_f)dtor}, \
}

/** The member content list for the dummy type mgsObject - it is empty,
  * and does not need to be referenced anywhere.
  */
#define mgsObject_

/** The virtual method list for the dummy type mgsObject - it is empty,
  * and does not need to be referenced anywhere.
  */
#define mgsObject__

/** Dummy class containing only the meta type pointer; a
  * mgsObject pointer and/or cast may be used to access the basic
  * (common) type information of any object of a class declared with
  * MGSclassdef().
  *
  * mgsObject is just a dummy definition - not a valid class name!
  */
MGSclasstype(mgsObject);

/** Dummy meta type type containing only the common type
  * information; a mgsObject_Meta pointer and/or cast may be used to
  * access the information common to any type, ie. all except any
  * virtual methods present.
  *
  * mgsObject is just a dummy definition - not a valid class name!
  */
_MGSmetatype(mgsObject);

#ifndef MGS_DOXYGEN
/* This is a dummy meta type allowing the keyword \a mgsNone
 * to be specified as the supertype for base classes in MGSmetainst().
 */
# define mgsNone__meta (*(mgsObject_Meta*)(0))
#endif

/** Assuming \p mem points to a valid object, retrieves the class
  * description through typecasting, allowing access to the
  * information common to all classes.
  */
#define mgs_meta(mem) \
	((mgsObject_Meta*)((mgsObject*)mem)->meta)

/** Assuming \p mem points to a valid object or to an object under
  * construction, changes the meta type to \p _meta.
  */
#define mgs_set_meta(mem, _meta) \
	((void)(((mgsObject*)(mem))->meta = (_meta)))

/** Assuming \p mem points to a valid object or to an object under
  * construction, changes the meta type to that of the
  * \p Class named.
  *
  * Supplying the keyword \a mgsNone as the class will set it to a NULL
  * pointer.
  */
#define mgs_set_metaof(mem, Class) \
	((void)(((mgsObject*)mem)->meta = (mgsObject_Meta*)mgs_metaof(Class)))

/** Call a virtual method named \p func belonging to the
  * class instance \p o and pass \p o as the first argument,
  * any other arguments following.
  *
  * This macro is meant to simplify calls to dynamically selected
  * versions of virtual functions, but is not needed to make such
  * calls. It can only be used for functions which have this form
  * and include the object pointer as the first parameter.
  */
#define mgs_virt(func, ...) \
	(MGS_ARG1(__VA_ARGS__))->meta->virt.func(__VA_ARGS__)

/** Allocation method used in instance creation functions,
  * typically inside the "new"-wrapper around the constructor
  * function, as generated by MGSctordef(). (A *_new()
  * function for a type with an empty constructor can alternatively
  * be defined as a simple wrapper macro around this.)
  *
  * If \a mem is zero, returns a new, zero'd allocation of
  * \a meta->size; if non-zero, zeroes \p mem and returns
  * it.
  *
  * If not done, the final run-time initialization of the type
  * description will be performed.
  *
  * The \a meta pointer of the new object is set to \p
  * meta.
  */
MGS_API void* mgs_raw_new(void *mem, void *meta);

/** Memory pool-using version of the allocation method
  * used in instance creation functions, typically
  * inside the "mpnew"-wrapper around the constructor
  * function, as generated by MGSctordef(). (A *_mpnew()
  * function for a type with an empty constructor can alternatively
  * be defined as a simple wrapper macro around this.)
  *
  * Uses \p mp to allocate an instance. If \p meta has a
  * destructor, it will be registered with the mempool.
  *
  * Otherwise behaves like \ref mgs_raw_new().
  */
MGS_API void* mgs_raw_mpnew(struct mgsMemPool *mp, void *meta);

/** Destroys object and frees memory, first calling the destructor for
  * the class if any.
  *
  * Should not be used for an object allocated using a mempool.
  */
MGS_API void mgs_delete(void *o);

/** Destroys object without freeing memory, calling the destructor for
  * the class if any, and zeroes the type pointer so that the object
  * is left explicitly invalid. The allocation can be reused after this;
  * if it's dynamic, it may later need to be freed with free().
  *
  * Should not be used for an object allocated using a mempool.
  */
MGS_API void mgs_finalize(void *o);

/** An underlying function used by the more convenient class type-checking
  * macros:
  * - mgs_subclass()
  * - mgs_superclass()
  * - mgs_of_class()
  * - mgs_of_subclass()
  *
  * It checks if \p submeta is a subclass of \p meta.
  * Returns 1 if subclass, 0 if same class, -1 if neither.
  */
MGS_API int mgs_rtticheck(const void *submeta, const void *meta);

/** Checks if the named \p Subclass is a subclass of the named \p Class.
    Returns 1 if subclass, 0 if same class, -1 if neither. */
#define mgs_subclass(Subclass, Class) \
	mgs_rtticheck(mgs_metaof(Subclass), mgs_metaof(Class))

/** Checks if the named \p Superclass is a superclass of the named \p Class.
    Returns 1 if superclass, 0 if same class, -1 if neither. */
#define mgs_superclass(Superclass, Class) \
	mgs_rtticheck(mgs_metaof(Class), mgs_metaof(Superclass))

/** Checks if \p o is an instance of \p Class or of a class derived
    from it. */
#define mgs_of_class(o, Class) \
	(mgs_rtticheck((o)->meta, mgs_metaof(Class)) >= 0)

/** Checks if \p o is of a type derived from \p Class. */
#define mgs_of_subclass(o, Class) \
	(mgs_rtticheck((o)->meta, mgs_metaof(Class)) > 0)
