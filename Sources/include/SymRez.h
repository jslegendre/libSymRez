//
//  SymRez.h
//  SymRez
//
//  Created by Jeremy Legendre on 4/14/20.
//  Copyright © 2020 Jeremy Legendre. All rights reserved.
//

#ifndef SymRez_h
#define SymRez_h

#include <stdio.h>
#include <stdbool.h>
#include <os/base.h>

OS_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

typedef const struct mach_header_64* mach_header_t;
typedef struct symrez* symrez_t;
typedef struct sr_iterator* sr_iterator_t;
typedef struct sr_iter_result * _Nullable sr_iter_result_t;
typedef void * _Nullable sr_ptr_t;
typedef char* sr_symbol_t;

/*! 
 * @define SR_EXEC_HDR
 *
 * @abstract Can be used with symrez_new_mh() to get the main executable
 */
#define SR_EXEC_HDR ((mach_header_t)(void *) -1)

/*!
 * @define SR_DYLD_HDR
 *
 * @abstract Can be used with symrez_new_mh() to get dyld header
 */
#define SR_DYLD_HDR ((mach_header_t)(void *) -2)

// return true to stop loop
typedef bool (*symrez_function_t)(sr_symbol_t symbol, sr_ptr_t ptr, void * _Nullable context);

/*! 
 * @function symrez_new
 *
 * @abstract Create new symrez object. Caller must free.
 *
 * @param image_name Name or full path of the library to symbolicate. Pass NULL for current executable
 */
symrez_t _Nullable OS_MALLOC OS_WARN_RESULT
symrez_new(char *image_name);

/*! 
 * @function symrez_new
 *
 * @abstract Create new symrez object. Caller must free.
 *
 * @param header  Pointer to the mach_header_64 to symbolicate. Pass NULL for current executable
 */
symrez_t _Nullable OS_MALLOC OS_WARN_RESULT
symrez_new_mh(mach_header_t header);

/*! 
 * @function sr_resolve_symbol
 *
 * @abstract Find symbol address
 *
 * @param symrez symrez object created by symrez_new
 *
 * @param symbol Mangled symbol name
 *
 * @return Pointer to symbol location or NULL if not found
 * */
sr_ptr_t sr_resolve_symbol(symrez_t symrez, char *symbol);

/*! 
 * @function sr_for_each
 *
 * @abstract Loop through all symbols with a callback
 *
 * @param symrez symrez object created by symrez_new
 *
 * @param context user context for callback
 *
 * @param callback callback for processing each iteration. Return true to stop loop.
 *
 * @discussion More performant and efficient than 'sr_iterator', but less convenient. String passed to 'callback' should be considered ephemeral. 
 * */
void sr_for_each(symrez_t symrez, void * _Nullable context, symrez_function_t callback);

/*! 
 * @function sr_get_iterator
 *
 * @abstract Get iterator from symrez object
 *
 * @param symrez symrez object created by symrez_new
 *
 * @return iterator reference
 *
 * @discussion First call to 'sr_get_iterator' will allocate more  memory. Consider using 'sr_for_each' for more performance.
 * */
sr_iterator_t sr_get_iterator(symrez_t symrez);

/*! 
 * @function sr_set_slide
 *
 * @abstract Set custom slide value
 * 
 * @param symrez symrez object created by symrez_new
 *
 * @param slide new slide value
 *
 * @discussion Useful for static analysis tools. i.e. disassemblers.
 */
void sr_set_slide(symrez_t symrez, intptr_t slide);

/*! 
 * @function sr_get_slide
 *
 * @abstract Get mach-o image slide
 *
 * @param symrez symrez object created by symrez_new
 *
 * @return slide value
 */
intptr_t sr_get_slide(symrez_t symrez);

/*! 
 * @function sr_free
 *
 * @abstract Release all resources allocated for this symrez object 
 * */
void sr_free(symrez_t);

/*! 
 * @function symrez_resolve_once
 *
 * @abstract Lookup a single symbol. Does not allocate memory but not recommended for multiple lookups
 *
 * @param image_name Name or full path of the library to symbolicate. Pass NULL for current executable
 *
 * @return Pointer to symbol location or NULL if not found 
 */
sr_ptr_t symrez_resolve_once(char *image_name, char *symbol);

/*! 
 * @function symrez_resolve_once_mh
 *
 * @abstract Lookup a single symbol. Does not allocate memory but not recommended for multiple lookups
 *
 * @param header  Pointer to the mach_header_64 to symbolicate. Pass NULL for current executable
 *
 * @return Pointer to symbol location or NULL if not found 
 * */
sr_ptr_t symrez_resolve_once_mh(mach_header_t header, char *symbol);

/*! 
 * @function sr_iter_get_next
 *
 * @abstract Get next symbol
 *
 * @param iterator iterator
 *
 * @return Opaque sr_iter_result_t reference or NULL if done
 *
 * @discussion Use the sr_iter_* functions below to get the symbol
 * */
sr_iter_result_t sr_iter_get_next(sr_iterator_t iterator);

/*! 
 * @function sr_iter_get_ptr
 *
 * @abstract Get current symbol address
 *
 * @param iterator iterator
 *
 * @return Pointer to symbol location or NULL if not found.
 * */
sr_ptr_t sr_iter_get_ptr(sr_iterator_t iterator);

/*! 
 * @function sr_iter_get_symbol
 *
 * @abstract Get current symbol name
 *
 * @param iterator iterator
 *
 * @return volatile string reference to symbol name
 *
 * @discussion Use strdup or sr_iter_copy_symbol() if you need to cache or save the symbol name.
 * */
sr_symbol_t sr_iter_get_symbol(sr_iterator_t iterator);

/*! 
 * @function sr_iter_copy_symbol
 *
 * @abstract Copy-out current symbol name
 *
 * @param iterator iterator
 *
 * @param dest Copy destination or NULL to only get strlen
 *
 * @return strlen of symbol
 * */
size_t sr_iter_copy_symbol(sr_iterator_t iterator, char *dest);

__END_DECLS
OS_ASSUME_NONNULL_END
#endif /* SymRez_h */
