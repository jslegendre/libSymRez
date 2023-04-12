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

extern const struct mach_header_64 _mh_execute_header;
typedef struct load_command* load_command_t;
typedef struct segment_command_64* segment_command_t;
typedef struct dyld_all_image_infos* dyld_all_image_infos_t;
typedef struct nlist_64 nlist_64;
typedef void* symtab_t;
typedef void* strtab_t;

int symrez_init_mh(symrez_t symrez, mach_header_t mach_header);

static dyld_all_image_infos_t _g_all_image_infos = NULL;

struct symrez {
    mach_header_t header;
    intptr_t slide;
    symtab_t symtab;
    strtab_t strtab;
    uint32_t nsyms;
    void *exports;
    uintptr_t exports_size;
};

static int _strncmp_fast(const char *ptr0, const char *ptr1, size_t len) {
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


static intptr_t read_uleb128(void** ptr) {
    uint8_t *p = *ptr;
    uint64_t result = 0;
    int         bit = 0;
    do {
        uint64_t slice = *p & 0x7f;
        
        if (bit > 63) {
            printf("uleb128 too big for uint64, bit=%d, result=0x%0llX\n", bit, result);
        } else {
            result |= (slice << bit);
            bit += 7;
        }
    } while (*p++ & 0x80);
    
    *ptr = p;
    return (uintptr_t)result;
}

static const uint8_t* walk_export_trie(const uint8_t* start, const uint8_t* end, const char* s) {
    const uint8_t* p = start;
    while (p != NULL) {
        uintptr_t terminalSize = *p++;
        if ( terminalSize > 127 ) {
            // except for re-export-with-rename, all terminal sizes fit in one byte
            --p;
            terminalSize = read_uleb128((void**)&p);
        }
        if ((*s == '\0') && (terminalSize != 0)) {
            return p;
        }
        const uint8_t* children = p + terminalSize;
        if (children > end) {
            
            return NULL;
        }

        uint8_t childrenRemaining = *children++;
        p = children;
        uintptr_t nodeOffset = 0;
        for (; childrenRemaining > 0; --childrenRemaining) {
            const char* ss = s;
            bool wrongEdge = false;
 
            char c = *p;
            while (c != '\0') {
                if (!wrongEdge) {
                    if ( c != *ss ) {
                        wrongEdge = true;
                    }
                    ++ss;
                }
                ++p;
                c = *p;
            }
            
            if (wrongEdge) {
                ++p;
                while ((*p & 0x80) != 0) {
                    ++p;
                }
                ++p;
                if (p > end) {
                    return NULL;
                }
            } else {
                ++p;
                nodeOffset = read_uleb128((void**)&p);
                if ((nodeOffset == 0) || ( &start[nodeOffset] > end)) {
                    return NULL;
                }
                s = ss;
                break;
            }
        }
        if ( nodeOffset != 0 )
            p = &start[nodeOffset];
        else
            p = NULL;
    }
    return NULL;
}

static dyld_all_image_infos_t _get_all_image_infos(void) {
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

static segment_command_t _find_segment_64(mach_header_t mh, const char *segname) {
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

static load_command_t _find_load_command(mach_header_t mh, uint32_t cmd) {
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

static const char * _dylib_for_ordinal(mach_header_t mh, uintptr_t ordinal) {
    char *dylib = NULL;
    load_command_t lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds && ordinal > 0) {
        switch (lc->cmd) {
            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB: {
                const struct dylib_command *dylibCmd = (void*)lc;
                dylib = (char*)((void*)lc + dylibCmd->dylib.name.offset);
                --ordinal;
            }
        }
        
        lc = (load_command_t)((uint64_t)lc + (uint64_t)lc->cmdsize);
    }
    
    return dylib;
}


static intptr_t _compute_image_slide(mach_header_t mh) {
    intptr_t res = 0;
    uint64_t mh_addr = (uint64_t)(void*)mh;
    segment_command_t seg = _find_segment_64(mh, SEG_TEXT);
    res = mh_addr - (seg->vmaddr);
    return res;
}

static int _find_linkedit_commands(symrez_t symrez) {
    mach_header_t mh = symrez->header;
    intptr_t slide = symrez->slide;
    
    struct symtab_command *symtab = NULL;
    segment_command_t linkedit = _find_segment_64(mh, SEG_LINKEDIT);
    if (!linkedit) {
        return 0;
    }
    
    symtab = (symtab_t)_find_load_command(mh, LC_SYMTAB);
    if (!symtab) {
        return 0;
    }
    
    struct linkedit_data_command *exportInfo = (struct linkedit_data_command *)_find_load_command(mh, LC_DYLD_EXPORTS_TRIE);
    if (exportInfo) {
        symrez->exports = (void*)(linkedit->vmaddr - linkedit->fileoff) + exportInfo->dataoff + symrez->slide;
        symrez->exports_size = exportInfo->datasize;
    }
    
    symrez->nsyms = symtab->nsyms;
    symrez->strtab = (strtab_t)(linkedit->vmaddr - linkedit->fileoff) + symtab->stroff + slide;
    symrez->symtab = (symtab_t)(linkedit->vmaddr - linkedit->fileoff) + symtab->symoff + slide;
    
    return 1;
}

int _find_image(const char *image_name, mach_header_t *hdr) {
    int found = -1;
    *hdr = NULL;
    
    dyld_all_image_infos_t dyld_all_image_infos = _get_all_image_infos();
    const struct dyld_image_info *info_array = dyld_all_image_infos->infoArray;
    for(int i = 0; i < dyld_all_image_infos->infoArrayCount; i++) {
        const char *p = info_array[i].imageFilePath;
        if (_strncmp_fast(p, image_name, strlen(p)) == 0) {
            found = i;
            break;
        }
        
        char *img = strrchr(p, '/');
        img = (char *)&img[1];
        if(_strncmp_fast(img, image_name, strlen(img)) == 0) {
            found = i;
            break;
        }
    }
    
    if (found >= 0) {
        *hdr = (mach_header_t)(info_array[found].imageLoadAddress);
        return 1;
    }
    
//    uint32_t imagecount = _dyld_image_count();
//    for(int i = 0; i < imagecount; i++) {
//        const char *p = _dyld_get_image_name(i);
//        if (_strncmp_fast(p, image_name, strlen(p)) == 0) {
//            found = i;
//            break;
//        }
//
//        char *img = strrchr(p, '/');
//        img = (char *)&img[1];
//        if(strcmp(img, image_name) == 0) {
//            *hdr = (const struct mach_header_64 *)_dyld_get_image_header(i);
//            return 1;
//        }
//    }
//
//    if (found >= 0) {
//        *hdr = (mach_header_t)_dyld_get_image_header(found);
//        return 1;
//    }
    
    return 0;
}

mach_header_t _get_base_addr(void) {
    dyld_all_image_infos_t dyld_all_image_infos = _get_all_image_infos();
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

static void * _resolve_local_symbol(symrez_t symrez, const char *symbol) {
    strtab_t strtab = symrez->strtab;
    symtab_t symtab = symrez->symtab;
    intptr_t slide = symrez->slide;
    uintptr_t nl_addr = (uintptr_t)symtab;
    uint64_t i = 0;
    void *addr = NULL;
    size_t sym_len = strlen(symbol);
    
    for (i = 0; i < symrez->nsyms; i++, nl_addr += sizeof(struct nlist_64)) {
        struct nlist_64 *nl = (struct nlist_64 *)nl_addr;
        const char *str = (const char *)strtab + nl->n_un.n_strx;
        if (strlen(str) != sym_len) {
            continue;
        }
        
        if (_strncmp_fast(str, symbol, strlen(symbol)) == 0) {
            uint64_t n_value = nl->n_value;
            if (n_value > 0) {
                addr = (void *)(n_value + slide);
                break;
            }
        }
    }
    
    return addr;
}

static void* _resolve_reexported_symbol(symrez_t symrez, const char *symbol) {
    if (!symrez->exports_size) {
        return NULL;
    }
    
    mach_header_t mh = symrez->header;
    void *exportTrie = symrez->exports;
    void *addr = NULL;
    void *end = (void*)(exportTrie + symrez->exports_size);
    const uint8_t* node = walk_export_trie(exportTrie, end, symbol);
    if (node) {
        const uint8_t* p = node;
        const uintptr_t flags = read_uleb128((void**)&p);
        if ( flags & EXPORT_SYMBOL_FLAGS_REEXPORT ) {
            uintptr_t ordinal = read_uleb128((void**)&p);
            const char *importedName = (char*)p;
            const char *dylib = _dylib_for_ordinal(mh, ordinal);
            
            if (!dylib) return NULL;
            
            struct symrez sr = { 0 };
            mach_header_t hdr = NULL;
            if(!_find_image(dylib, &hdr)) return NULL;
            if (!symrez_init_mh(&sr, hdr)) return NULL;
            
            addr = sr_resolve_symbol(&sr, importedName);
        }
    }
    
    return addr;
}

static void* _resolve_dependent_symbol(symrez_t symrez, const char *symbol) {
    void *addr = NULL;
    mach_header_t mh = symrez->header;
    load_command_t lc = (load_command_t)((uint64_t)mh + sizeof(struct mach_header_64));
    while ((uint64_t)lc < (uint64_t)mh + (uint64_t)mh->sizeofcmds && !addr) {
        switch (lc->cmd) {
            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB: {
                const struct dylib_command *dylibCmd = (void*)lc;
                const char *dylib = (char*)((void*)lc + dylibCmd->dylib.name.offset);
                if (!dylib) {
                    goto next;
                }
                
                struct symrez sr = { 0 };
                mach_header_t hdr = NULL;
                if(!_find_image(dylib, &hdr)) {
                    goto next;
                }
                if (!symrez_init_mh(&sr, hdr)) {
                    goto next;
                }
                
                if (lc->cmd == LC_REEXPORT_DYLIB) {
                    addr = sr_resolve_symbol(&sr, symbol);
                } else {
                    addr = _resolve_local_symbol(&sr, symbol);
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
    void *addr = _resolve_local_symbol(symrez, symbol);
    
    if (!addr) {
        addr = _resolve_reexported_symbol(symrez, symbol);
    }
    
    if (!addr) {
        addr = _resolve_dependent_symbol(symrez, symbol);
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

static void sr_for_each_in_trie_do_callback(symrez_t symrez, const uint8_t *node, char *sym, symrez_function_t work) {
    uintptr_t flags = read_uleb128((void**)&node);
    switch (flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) {
        case EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE: {
            uintptr_t addr = read_uleb128((void**)&node);
            if (work(sym, (void*)addr)) return;
            break;
        }
        case EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION:
        case EXPORT_SYMBOL_FLAGS_KIND_REGULAR: {
            uintptr_t offset = read_uleb128((void**)&node);
            uintptr_t addr = (uintptr_t)(symrez->header) + offset;
            if (work(sym, (void*)addr)) return;
            break;
        }
        default:
            break;
    }
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
static void sr_for_each_in_trie(symrez_t symrez, symrez_function_t work) {
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
        if ( terminal_size > 127 ) {
            --p;
            terminal_size = read_uleb128((void**)&p);
        }

        const uint8_t* children = p + terminal_size;
        uint8_t child_count = *children++;
        if (child_count == 0) { // Handle leaf node
            sr_for_each_in_trie_do_callback(symrez, ++node, sym, work);
            continue;
        }

        p = children;
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

void * symrez_resolve_once_mh(mach_header_t header, const char *symbol) {
    mach_header_t hdr = header;
    if (!hdr) {
        hdr = _get_base_addr();
    }
    
    if (header == SR_DYLD_HDR) {
        dyld_all_image_infos_t aii = _get_all_image_infos();
        hdr = (mach_header_t)(aii->dyldImageLoadAddress);
    }
    
    intptr_t slide = _compute_image_slide(hdr);
    
    struct symrez sr = { 0 };
    sr.header = hdr;
    sr.slide = slide;
    
    if (!_find_linkedit_commands(&sr)) {
        return NULL;
    }
    
    return sr_resolve_symbol(&sr, symbol);
}

void * symrez_resolve_once(const char *image_name, const char *symbol) {
    mach_header_t hdr = NULL;
    
    if(image_name != NULL && !_find_image(image_name, &hdr)) {
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
        hdr = _get_base_addr();
    }
    
    intptr_t slide = _compute_image_slide(hdr);
    
    symrez->header = hdr;
    symrez->slide = slide;
    
    if (!_find_linkedit_commands(symrez)) {
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
    if(image_name != NULL && !_find_image(image_name, &hdr)) {
        return NULL;
    }
    
    return symrez_new_mh(hdr);
}
