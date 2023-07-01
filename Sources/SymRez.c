//
//  SymRez.c
//  SymRez
//
//  Created by Jeremy Legendre on 4/14/20.
//  Copyright © 2020 Jeremy Legendre. All rights reserved.
//

#include "SymRez.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/getsect.h>
#include <mach-o/nlist.h>
#include <mach-o/getsect.h>
#include <mach/mach_vm.h>
#include <sys/syslimits.h>

#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif

#ifndef EXPORT_SYMBOL_FLAGS_WEAK_REEXPORT
#define EXPORT_SYMBOL_FLAGS_WEAK_REEXPORT 0xC
#endif

#ifndef SR_STATIC
  #ifdef DEBUG
    #define SR_STATIC
  #else
    #define SR_STATIC static
  #endif
#endif

#ifndef SR_INLINE
#define SR_INLINE static inline __attribute__((always_inline))
#endif


extern const struct mach_header_64 _mh_execute_header;
typedef struct load_command* load_command_t;
typedef struct segment_command_64* segment_command_t;
typedef struct dyld_all_image_infos* dyld_all_image_infos_t;
typedef struct nlist_64 nlist_64;
typedef void* symtab_t;
typedef void* strtab_t;

int symrez_init_mh(symrez_t symrez, mach_header_t mach_header);

SR_STATIC dyld_all_image_infos_t _g_all_image_infos = NULL;

struct symrez {
    mach_header_t header;
    intptr_t slide;
    symtab_t symtab;
    strtab_t strtab;
    uint32_t nsyms;
    void *exports;
    uintptr_t exports_size;
};

SR_STATIC int _strncmp_fast(const char *ptr0, const char *ptr1, size_t len) {
    size_t fast = len/sizeof(size_t) + 1;
    size_t offset = 0;
    int current_block = 0;
    
    if( len < sizeof(size_t)) { fast = 0; }
    
    
    size_t *lptr0 = (size_t*)ptr0;
    size_t *lptr1 = (size_t*)ptr1;
    
    while(current_block < fast){
        if(lptr0[current_block] ^ lptr1[current_block]){
            offset = current_block * sizeof(size_t);
            break;
        }
        
        ++current_block;
    }
    
    while(offset < len){
        if( (ptr0[offset] ^ ptr1[offset]) || ptr0[offset] == 0 || ptr1[offset] == 0) {
            return (int)((unsigned char)ptr0[offset] - (unsigned char)ptr1[offset]);
        }
        
        ++offset;
    }
    
    
    return 0;
}

SR_INLINE intptr_t read_uleb128(void** ptr) {
    uint8_t *p = *ptr;
    uint64_t result = 0;
    int bit = 0;
    
    do {
        uint64_t slice = *p & 0x7f;
        result |= (slice << bit);
        bit += 7;
    } while (*p++ & 0x80);
    
    *ptr = p;
    return (uintptr_t)result;
}

SR_STATIC const uint8_t* walk_export_trie(const uint8_t* start, const uint8_t* end, const char* symbol) {
    const uint8_t* p = start;
    while (p != NULL) {
        uintptr_t terminal_size = *p++;
        
        if ( terminal_size > 127 ) {
            --p;
            terminal_size = read_uleb128((void**)&p);
        }
        
        if ((*symbol == '\0') && (terminal_size != 0)) {
            return p;
        }
        
        const uint8_t* children = p + terminal_size;
        if (children > end) {
            return NULL;
        }

        uint8_t childrenRemaining = *children++;
        p = children;
        uintptr_t node_offset = 0;
        
        for (; childrenRemaining > 0; --childrenRemaining) {
            const char* ss = symbol;
            bool wrong_edge = false;
            const uint8_t *child = p;
            
            size_t child_len = strlen((char*)p);
            char c = *child;
            while (c != '\0') {
                if ((c ^ *ss++) > 0) {
                    wrong_edge = true;
                    break;
                }
                ++child;
                c = *child;
            }
            
            p += child_len + 1;
            node_offset = read_uleb128((void**)&p);
            if (wrong_edge) {
                node_offset = 0;
            } else {
                p = &start[node_offset];
                symbol = ss;
                break;
            }
        }
        
        if (!node_offset || p > end) break;
    }
    return NULL;
}

SR_STATIC dyld_all_image_infos_t get_all_image_infos(void) {
    if (!_g_all_image_infos) {
        task_dyld_info_data_t dyld_info;
        mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
        kern_return_t kr = task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
        if (kr != KERN_SUCCESS) {
            return NULL;
        }
        
        _g_all_image_infos = (void*)(dyld_info.all_image_info_addr);
    }
    
    return _g_all_image_infos;
}

SR_STATIC segment_command_t find_lc_segment(mach_header_t mh, const char *segname) {
    load_command_t lc;
    segment_command_t seg, foundseg = NULL;
    
    lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds) {
        if (lc->cmd == LC_SEGMENT_64) {
            seg = (segment_command_t)lc;
            if (strcmp(seg->segname, segname) == 0) {
                foundseg = seg;
                break;
            }
        }
        
        lc = (load_command_t)((uint64_t)lc + (uint64_t)lc->cmdsize);
    }
    
    return foundseg;
}

SR_STATIC load_command_t find_load_command(mach_header_t mh, uint32_t cmd) {
    load_command_t lc, foundlc = NULL;
    
    lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds) {
        if (lc->cmd == cmd) {
            foundlc = (load_command_t)lc;
            break;
        }
        
        lc = (load_command_t)((uint64_t)lc + (uint64_t)lc->cmdsize);
    }
    
    return foundlc;
}

SR_STATIC const char * dylib_name_for_ordinal(mach_header_t mh, uintptr_t ordinal) {
    const char *dylib = NULL;
    load_command_t lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds && ordinal > 0) {
        switch (lc->cmd) {
            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB: {
                const struct dylib_command *dylibCmd = (void*)lc;
                dylib = (const char*)((void*)lc + dylibCmd->dylib.name.offset);
                --ordinal;
            }
        }
        
        lc = (load_command_t)((uint64_t)lc + (uint64_t)lc->cmdsize);
    }
    
    return dylib;
}

SR_STATIC intptr_t compute_image_slide(mach_header_t mh) {
    intptr_t res = 0;
    uint64_t mh_addr = (uint64_t)(void*)mh;
    segment_command_t seg = find_lc_segment(mh, SEG_TEXT);
    res = mh_addr - (seg->vmaddr);
    return res;
}

SR_STATIC int find_linkedit_commands(symrez_t symrez) {
    mach_header_t mh = symrez->header;
    intptr_t slide = symrez->slide;
    
    struct symtab_command *symtab = NULL;
    segment_command_t linkedit = find_lc_segment(mh, SEG_LINKEDIT);
    if (!linkedit) {
        return 0;
    }
    
    symtab = (symtab_t)find_load_command(mh, LC_SYMTAB);
    if (!symtab) {
        return 0;
    }
    
    symrez->nsyms = symtab->nsyms;
    symrez->strtab = (strtab_t)(linkedit->vmaddr - linkedit->fileoff) + symtab->stroff + slide;
    symrez->symtab = (symtab_t)(linkedit->vmaddr - linkedit->fileoff) + symtab->symoff + slide;
    
    struct linkedit_data_command *exportInfo = (struct linkedit_data_command *)find_load_command(mh, LC_DYLD_EXPORTS_TRIE);
    if (exportInfo) {
        symrez->exports = (void*)(linkedit->vmaddr - linkedit->fileoff) + exportInfo->dataoff + symrez->slide;
        symrez->exports_size = exportInfo->datasize;
        return 1;
    }
    
    struct dyld_info_command *dyld_info = (void*)find_load_command(mh, LC_DYLD_INFO_ONLY);
    if (!dyld_info) {
        dyld_info = (void*)find_load_command(mh, LC_DYLD_INFO);
    }
    
    if (dyld_info) {
        symrez->exports = (void*)(linkedit->vmaddr - linkedit->fileoff) + dyld_info->export_off + symrez->slide;
        symrez->exports_size = dyld_info->export_size;
    }
    
    return 1;
}

SR_INLINE int find_image_by_name(const char *image_name, mach_header_t *hdr) {
    *hdr = NULL;
    uint32_t block = *(uint32_t*)image_name;
    
    dyld_all_image_infos_t dyld_all_image_infos = get_all_image_infos();
    const struct dyld_image_info *info_array = dyld_all_image_infos->infoArray;
    for(int i = 0; i < dyld_all_image_infos->infoArrayCount; i++) {
        const char *p = info_array[i].imageFilePath;
        const char *img = strrchr(p, '/');
        ++img;
        
        if (block ^ *(uint32_t*)img) continue;
        if(strcmp(img, image_name) == 0) {
            *hdr = (mach_header_t)(info_array[i].imageLoadAddress);
            return i;
        }
    }
    
    return -1;
}


// If the image is found, place result in *hdr and return index of image.
// return -1 otherwise.
SR_STATIC
int find_image(const char *image_name, mach_header_t *hdr) {
    *hdr = NULL;
    if (*image_name ^ '/') {
        return find_image_by_name(image_name, hdr);
    }

    size_t name_len = strlen(image_name);
    dyld_all_image_infos_t dyld_all_image_infos = get_all_image_infos();
    const struct dyld_image_info *info_array = dyld_all_image_infos->infoArray;
    for(int i = 0; i < dyld_all_image_infos->infoArrayCount; i++) {
        const char *p = info_array[i].imageFilePath;
        if (_strncmp_fast(p, image_name, name_len) == 0) {
            *hdr = (mach_header_t)(info_array[i].imageLoadAddress);
            return i;
        }
    }

    return -1;
}

mach_header_t get_base_addr(void) {
    dyld_all_image_infos_t dyld_all_image_infos = get_all_image_infos();
    if (dyld_all_image_infos) {
        return (mach_header_t)(dyld_all_image_infos->infoArray[0].imageLoadAddress);
    }
    
    // Fallback
    kern_return_t kr = KERN_FAILURE;
    vm_region_basic_info_data_t info = { 0 };
    mach_vm_size_t size = 0;
    mach_port_t object_name = MACH_PORT_NULL;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_vm_address_t address = 0x100000000;
    
    while (kr != KERN_SUCCESS) {
        address += size;
        kr = mach_vm_region(current_task(), &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t) &info, &count, &object_name);
    }
    
    return (mach_header_t)address;
}

SR_INLINE
bool sr_for_each_trie_callback(symrez_t symrez, const uint8_t *node, char *sym, symrez_function_t work) {
    uintptr_t addr = 0;
    uintptr_t flags = read_uleb128((void**)&node);
    switch (flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) {
        case EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE: {
            addr = read_uleb128((void**)&node);
            break;
        }
        case EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION:
        case EXPORT_SYMBOL_FLAGS_KIND_REGULAR: {
            uintptr_t offset = read_uleb128((void**)&node);
            addr = (uintptr_t)(symrez->header) + offset;
            break;
        }
        default:
            break;
    }
    
    return work(sym, (void*)addr);
}

/*
Example export tree of this library:
             s
            / \
           /   \
          /   ymrez_
         /     / \
        /     /   \
       /    new  init
      r_
     / \
    f  resolve_symbol
   / \
 ree or_each

Consider each node in the export tree a symbol prefix and it's children to be suffixes.
The leaves of the tree represent complete symbols.
 
Traverse the tree in iterative preorder, accumulating complete symbols by appending the
suffix of the current node to the prefix of the parent node.
 */
SR_STATIC void sr_for_each_in_trie(symrez_t symrez, symrez_function_t work) {
    void *export_trie = symrez->exports;
    const uint8_t *start = export_trie;

    struct stack_node {
        const uint8_t* node;
        char sym[PATH_MAX];
        size_t sym_len;
    };

    struct stack_node node_stack[PATH_MAX];
    size_t stack_top = 0;
    node_stack[stack_top++].node = start;
    node_stack[stack_top].sym_len = 0;
    while (stack_top) {
        const uint8_t *node = node_stack[--stack_top].node;
        char *sym = node_stack[stack_top].sym;
        size_t sym_len = node_stack[stack_top].sym_len;
        
        const uint8_t *p = node;
        uintptr_t terminal_size = *p++;
        if (terminal_size > 127) {
            --p;
            terminal_size = read_uleb128((void**)&p);
        }

        const uint8_t* children = p + terminal_size;
        uint8_t child_count = *children;
        if (child_count == 0) { // Handle leaf node
            if (sr_for_each_trie_callback(symrez, ++node, sym, work)) {
                return;
            }
            continue;
        }

        ++p;
        size_t parent_len = sym_len;
        
        // First child needs to be first to get popped so
        // increment stack_top by child_count and iterate
        // over children first to last. Decrement stack_top
        // each time to place siblings from top to bottom,
        // maintaining preorder.
        // The current node is popped and will be replaced
        // by the last child.
        stack_top += child_count;
        for (int i = child_count; i > 0; --i) {
            char *child_sym = node_stack[--stack_top].sym;
            
            // At child_count, stack will be at original
            // (parent) stack_node, which already contains
            // `sym`. Reuse it, saving a memcpy.
            if (i != 1) {
                memcpy(child_sym, sym, parent_len);
            }
            
            size_t child_len = strlen((char*)p);
            memcpy(&child_sym[parent_len], (char*)p, child_len);
            
            size_t new_len = parent_len + child_len;
            node_stack[stack_top].sym_len = new_len;
            child_sym[new_len] = '\0';
            
            p += child_len + 1;
            
            uintptr_t nodeOffset = read_uleb128((void**)&p);
            if (nodeOffset != 0) {
                node_stack[stack_top].node = &start[nodeOffset];
            }
        }
        // Restore stack_top
        stack_top += child_count;
    }
}

void sr_for_each(symrez_t symrez, symrez_function_t work) {
    strtab_t strtab = symrez->strtab;
    symtab_t symtab = symrez->symtab;
    intptr_t slide = symrez->slide;
    uintptr_t nl_addr = (uintptr_t)symtab;
    uint64_t i = 0;
    void *addr = NULL;
    
    for (i = 0; i < symrez->nsyms; i++, nl_addr += sizeof(struct nlist_64)) {
        struct nlist_64 *nl = (struct nlist_64 *)nl_addr;
        if ((nl->n_type & N_STAB) || nl->n_sect == 0) continue; // External symbol
        char *str = (char *)strtab + nl->n_un.n_strx;
        addr = (void *)(nl->n_value + slide);
        if (work(str, addr)) {
            break;
        }
    }
    
    if (symrez->exports_size)
        sr_for_each_in_trie(symrez, work);
}

SR_STATIC void * resolve_local_symbol(symrez_t symrez, const char *symbol) {
    strtab_t strtab = symrez->strtab;
    symtab_t symtab = symrez->symtab;
    intptr_t slide = symrez->slide;
    uintptr_t nl_addr = (uintptr_t)symtab;
    uint64_t i = 0;
    void *addr = NULL;
    size_t sym_len = strlen(symbol) + 1;
    uint32_t sym_block = *(uint32_t*)symbol;
    
    for (i = 0; i < symrez->nsyms; i++, nl_addr += sizeof(struct nlist_64)) {
        struct nlist_64 *nl = (struct nlist_64 *)nl_addr;
        if (nl->n_un.n_strx == 0) continue;
        
        const char *str = (const char *)strtab + nl->n_un.n_strx;
        if (*(uint32_t*)str ^ sym_block) continue;
        
        if (strncmp(str, symbol, sym_len) == 0) {
            uint64_t n_value = nl->n_value;
            if (n_value > 0) {
                addr = (void *)(n_value + slide);
                break;
            }
        }
    }
    
    return addr;
}

SR_STATIC void * resolve_exported_symbol(symrez_t symrez, const char *symbol) {
    if (!symrez->exports_size) {
        return NULL;
    }

    mach_header_t mh = symrez->header;
    void *exportTrie = symrez->exports;
    void *addr = NULL;
    void *end = (void*)((uintptr_t)exportTrie + symrez->exports_size);
    const uint8_t* node = walk_export_trie(exportTrie, end, symbol);
    if (node) {
        const uint8_t* p = node;
        uintptr_t flags = read_uleb128((void**)&p);
        if ((flags & EXPORT_SYMBOL_FLAGS_REEXPORT) ||
            (flags & EXPORT_SYMBOL_FLAGS_WEAK_REEXPORT)) {
            uintptr_t ordinal = read_uleb128((void**)&p);
            const char* importedName = (char*)p;
            if (!importedName || strlen(importedName) == 0) {
                importedName = symbol;
            }
            const char *dylib = dylib_name_for_ordinal(mh, ordinal);
            
            if (!dylib) return NULL;
            
            struct symrez sr;
            mach_header_t hdr = NULL;
            if(!find_image(dylib, &hdr)) return NULL;
            if (!symrez_init_mh(&sr, hdr)) return NULL;
            
            return sr_resolve_symbol(&sr, importedName);
        }
        
        switch (flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) {
            case EXPORT_SYMBOL_FLAGS_KIND_REGULAR: {
                if (flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
                    typedef uintptr_t (*ResolverProc)(void);
                    ResolverProc resolver = (ResolverProc)(read_uleb128((void**)&p) + (uintptr_t)mh);
                    #if __has_feature(ptrauth_calls)
                    resolver = (ResolverProc)__builtin_ptrauth_sign_unauthenticated(resolver, ptrauth_key_asia, 0);
                    #endif
                    
                    uintptr_t result = (*resolver)();
                    #if __has_feature(ptrauth_calls)
                    result = (uintptr_t)__builtin_ptrauth_strip((void*)result, ptrauth_key_asia);
                    #endif
                    return (void*)result;
                }
                
                uintptr_t offset = read_uleb128((void**)&p);
                addr = (void*)(offset + (uintptr_t)mh);
                break;
            }
            case EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE:
                addr = (void*)read_uleb128((void**)&p);
                break;
            default:
                break;
        }
    }
    
    return addr;
}

SR_STATIC void* resolve_dependent_symbol(symrez_t symrez, const char *symbol) {
    void *addr = NULL;
    mach_header_t mh = symrez->header;
    load_command_t lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds && !addr) {
        switch (lc->cmd) {
//            case LC_LOAD_DYLIB:
//            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB: {
                const struct dylib_command *dylibCmd = (void*)lc;
                const char *dylib = (char*)((void*)lc + dylibCmd->dylib.name.offset);
                if (!dylib) {
                    goto next;
                }
                
                struct symrez sr = { 0 };
                mach_header_t hdr = NULL;
                if(!find_image(dylib, &hdr)) {
                    goto next;
                }
                
                if (!symrez_init_mh(&sr, hdr)) {
                    goto next;
                }
                
                if (lc->cmd == LC_REEXPORT_DYLIB) {
                    addr = sr_resolve_symbol(&sr, symbol);
                } else {
                    addr = resolve_local_symbol(&sr, symbol);
                }
                
                if (addr) {
                    return addr;
                }
            }
        }
    next:
        lc = (load_command_t)((uint64_t)lc + (uint64_t)lc->cmdsize);
    }
    
    return NULL;
}

void * sr_resolve_symbol(symrez_t symrez, const char *symbol) {
    void *addr = resolve_local_symbol(symrez, symbol);
    
    if (!addr) {
        addr = resolve_exported_symbol(symrez, symbol);
    }
    
    if (!addr) {
        addr = resolve_dependent_symbol(symrez, symbol);
    }
    
#if __has_feature(ptrauth_calls)
    if (addr)
        addr = ptrauth_sign_unauthenticated(addr, ptrauth_key_function_pointer, 0);
#endif
    
    return addr;
}

void sr_free(symrez_t symrez) {
    free(symrez);
}

void * symrez_resolve_once_mh(mach_header_t header, const char *symbol) {
    mach_header_t hdr = header;
    if (!hdr) {
        hdr = get_base_addr();
    }
    
    if (header == SR_DYLD_HDR) {
        dyld_all_image_infos_t aii = get_all_image_infos();
        hdr = (mach_header_t)(aii->dyldImageLoadAddress);
    }
    
    intptr_t slide = compute_image_slide(hdr);
    
    struct symrez sr = { 0 };
    sr.header = hdr;
    sr.slide = slide;
    
    if (!find_linkedit_commands(&sr)) {
        return NULL;
    }
    
    return sr_resolve_symbol(&sr, symbol);
}

void * symrez_resolve_once(const char *image_name, const char *symbol) {
    mach_header_t hdr = NULL;
    
    if(image_name != NULL && !find_image(image_name, &hdr)) {
        return NULL;
    }
    
    return symrez_resolve_once_mh(hdr, symbol);
}

int symrez_init_mh(symrez_t symrez, mach_header_t mach_header) {
    symrez->header = NULL;
    symrez->slide = 0;
    symrez->nsyms = 0;
    symrez->symtab = NULL;
    symrez->strtab = NULL;
    symrez->exports = NULL;
    symrez->exports_size = 0;
    
    mach_header_t hdr = mach_header;
    if (hdr == NULL) {
        hdr = get_base_addr();
    }
    
    intptr_t slide = compute_image_slide(hdr);
    
    symrez->header = hdr;
    symrez->slide = slide;
    
    if (!find_linkedit_commands(symrez)) {
        return 0;
    }
    
    return 1;
}

symrez_t symrez_new_mh(mach_header_t mach_header) {
    symrez_t symrez = NULL;
    if ((symrez = malloc(sizeof(*symrez))) == NULL) {
        return NULL;
    }
    
    if (!symrez_init_mh(symrez, mach_header)) {
        free(symrez);
        symrez = NULL;
        return NULL;
    }
    
    return symrez;
}

symrez_t symrez_new(const char *image_name) {
    
    mach_header_t hdr = NULL;
    if(image_name != NULL && (find_image(image_name, &hdr) < 0)) {
        return NULL;
    }
    
    return symrez_new_mh(hdr);
}
