//
//  SymRez.h
//  SymRez
//
//  Created by Jeremy Legendre on 4/14/20.
//  Copyright © 2020 Jeremy Legendre. All rights reserved.
//

#ifndef SymRez_h
#define SymRez_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

typedef const struct mach_header_64* mach_header_t;

#define SR_DYLD_HDR ((mach_header_t)(void *) -1)

// return true to stop loop
typedef bool (*symrez_function_t)(char *symbol, void *ptr, void *context);

typedef struct symrez* symrez_t;
typedef struct sr_iterator* sr_iterator_t;

/*! @function symrez_new
    @abstract Create new symrez object. Caller must free.
    @param image_name Name or full path of the library to symbolicate. Pass NULL for current executable  */
symrez_t symrez_new(const char *image_name);

/*! @function symrez_new
    @abstract Create new symrez object. Caller must free.
    @param header  Pointer to the mach_header_64 to symbolicate. Pass NULL for current executable */
symrez_t symrez_new_mh(mach_header_t header);

/*! @function sr_resolve_symbol
    @abstract Find symbol address
    @param symrez symrez object created by symrez_new
    @param symbol Mangled symbol name
    @return Pointer to symbol location or NULL if not found */
void * sr_resolve_symbol(symrez_t symrez, const char *symbol);

/*! @function sr_for_each
    @abstract Loop through all symbols with a callback
    @param symrez symrez object created by symrez_new
    @param context user context for callback
    @param callback callback for processing each iteration. Return true to stop loop.
    @discussion String passed to 'callback' may be ephemeral . */
void sr_for_each(symrez_t symrez, void *context, symrez_function_t callback);

/*! @function sr_get_iterator
    @abstract Get iterator from symrez object
    @param symrez symrez object created by symrez_new
    @return iterator reference */
sr_iterator_t sr_get_iterator(symrez_t symrez);

/*! @function sr_set_slide
    @abstract Set custom slide value
    @param symrez symrez object created by symrez_new
    @param slide new slide value
    @discussion Useful for static analysis tools. i.e. disassemblers. */
void sr_set_slide(symrez_t symrez, intptr_t slide);

/*! @function sr_get_slide
    @abstract Get mach-o image slide
    @param symrez symrez object created by symrez_new
    @return slide value  */
intptr_t sr_get_slide(symrez_t symrez);

/*! @function sr_free
    @abstract Release all resources allocated for this symrez object */
void sr_free(symrez_t);

/*! @function symrez_resolve_once
    @abstract Lookup a single symbol. Does not allocate memory but not recommended for multiple lookups
    @param image_name Name or full path of the library to symbolicate. Pass NULL for current executable
    @return Pointer to symbol location or NULL if not found */
void * symrez_resolve_once(const char *image_name, const char *symbol);

/*! @function symrez_resolve_once_mh
    @abstract Lookup a single symbol. Does not allocate memory but not recommended for multiple lookups
    @param header  Pointer to the mach_header_64 to symbolicate. Pass NULL for current executable
    @return Pointer to symbol location or NULL if not found */
void * symrez_resolve_once_mh(mach_header_t header, const char *symbol);

/*! @function sr_iterator_get_next
    @abstract Get next symbol
    @param iterator
    @return volatile string reference to symbol name
    @discussion Use strdup or sr_iterator_copy_symbol if you need to cache or save the symbol name. */
char * sr_iterator_get_next(sr_iterator_t iterator);

/*! @function sr_iterator_get_addr
    @abstract Get current symbol address
    @param iterator
    @return Pointer to symbol location. */
void * sr_iterator_get_ptr(sr_iterator_t iter);

/*! @function sr_iterator_get_symbol
    @abstract Get current symbol name
    @param iterator
    @return volatile string reference to symbol name
    @discussion Use strdup or sr_iterator_copy_symbol if you need to cache or save the symbol name. */
char * sr_iterator_get_symbol(sr_iterator_t iter);

/*! @function sr_iterator_copy_symbol
    @abstract Copy-out current symbol name
    @param iterator
    @param dest Copy destination or NULL to only get strlen
    @return strlen of symbol */
size_t sr_iterator_copy_symbol(sr_iterator_t iter, char *dest);

#ifdef __cplusplus
}
#endif
#endif /* SymRez_h */
