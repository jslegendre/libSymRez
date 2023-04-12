//
//  Tests.m
//  Tests
//
//  Created by Jeremy on 12/21/21.
//  Copyright © 2021 Jeremy Legendre. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <SymRez.h>
#import <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/ldsyms.h>

extern mach_header_t _get_base_addr(void);
extern int _find_image(const char *image_name, mach_header_t *hdr);

@interface Tests : XCTestCase

@end

@implementation Tests

- (void)testGetBaseAddress {
    mach_header_t m1;
    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; i++) {
        m1 = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (m1->filetype == MH_EXECUTE) {
            break;
        }
    }
    
    mach_header_t m2 = _get_base_addr();
    XCTAssertEqual(m1, m2);
}

- (void)testResolveSymbolOnce_public_printf {
    void *_printf = (void*)printf;
    void *sr_printf = symrez_resolve_once("libsystem_c.dylib", "_printf");

    XCTAssertEqual(_printf, sr_printf);
}

- (void)testResolveSymbolOnceMh_dyld_isMainExecutable {
    void *sym = symrez_resolve_once_mh(SR_DYLD_HDR, "__ZNK5dyld39MachOFile16isMainExecutableEv");
    XCTAssertTrue(sym);
}

- (void)testCallingResolvedSymbol_CFStringCreateWithCString {
    CFStringRef (*_CFStringCreateWithCString)(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding) = NULL;
    symrez_t sr = symrez_new("CoreFoundation");
    _CFStringCreateWithCString = sr_resolve_symbol(sr, "_CFStringCreateWithCString");
    sr_free(sr);
    
    CFStringRef cfstr1 = CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    CFStringRef cfstr2 = _CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    XCTAssertEqual(cfstr1, cfstr2);
    CFRelease(cfstr1);
    CFRelease(cfstr2);
}

- (void)testCallingResolveOnceSymbol_CFStringCreateWithCString {
    CFStringRef (*_CFStringCreateWithCString)(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding) = NULL;
    _CFStringCreateWithCString = symrez_resolve_once("CoreFoundation", "_CFStringCreateWithCString");
    
    CFStringRef cfstr1 = CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    CFStringRef cfstr2 = _CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    XCTAssertEqual(cfstr1, cfstr2);
    CFRelease(cfstr1);
    CFRelease(cfstr2);
}

- (void)testResolveReExportedSymbol_CGSClearWindowTags {
    symrez_t sr = symrez_new("CoreGraphics");
    void *sym = sr_resolve_symbol(sr, "_CGSClearWindowTags");
    sr_free(sr);
    
    XCTAssertTrue(sym);
}

- (void)testResolveSymbolFromReExportedLibrary_strcmp {
    symrez_t sr = symrez_new("/usr/lib/libSystem.B.dylib");
    void *sym = sr_resolve_symbol(sr, "_strcmp");
    sr_free(sr);
    XCTAssertTrue(sym);
}

- (void)testResolveSymbol_public_printf {
    void *_printf = (void*)printf;
    symrez_t sr = symrez_new("libsystem_c.dylib");
    void *sr_printf = sr_resolve_symbol(sr, "_printf");
    sr_free(sr);
    XCTAssertEqual(_printf, sr_printf);
}

- (void)testResolveSymbol_notFound {
    symrez_t sr = symrez_new("libsystem_c.dylib");
    void *sr_sym = sr_resolve_symbol(sr, "abc123");
    sr_free(sr);
    
    XCTAssertNil((__bridge id)sr_sym);
}

- (void)testResolveSymbol_private_CFStringHash {
    void *_CFStringHash = NULL;
    symrez_t sr = symrez_new("CoreFoundation");
    _CFStringHash = sr_resolve_symbol(sr, "___CFStringHash");
    sr_free(sr);
    XCTAssertTrue(_CFStringHash);
}

- (void)testFindImage_name_path {
    mach_header_t hdr1 = NULL;
    mach_header_t hdr2 = NULL;
    _find_image("/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation", &hdr1);
    _find_image("Foundation", &hdr2);
    
    XCTAssertEqual(hdr1, hdr2);
}

@end
