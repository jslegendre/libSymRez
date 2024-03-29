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

extern mach_header_t get_base_addr(void);
extern mach_header_t find_image(const char *image_name);

@interface Tests : XCTestCase

@end

@implementation Tests

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
    _CFStringCreateWithCString = (typeof(_CFStringCreateWithCString))sr_resolve_symbol(sr, "_CFStringCreateWithCString");
    sr_free(sr);
    
    CFStringRef cfstr1 = CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    CFStringRef cfstr2 = _CFStringCreateWithCString(kCFAllocatorDefault, "test str", kCFStringEncodingUTF8);
    XCTAssertEqual(cfstr1, cfstr2);
    CFRelease(cfstr1);
    CFRelease(cfstr2);
}

- (void)testCallingResolveOnceSymbol_CFStringCreateWithCString {
    CFStringRef (*_CFStringCreateWithCString)(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding) = NULL;
    _CFStringCreateWithCString = (typeof(_CFStringCreateWithCString))symrez_resolve_once("CoreFoundation", "_CFStringCreateWithCString");
    
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
    void *sym2 = dlsym(RTLD_DEFAULT, "SLSClearWindowTags");
    XCTAssertEqual(sym, sym2);
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

- (void)testResolveSymbol_private_objcDirect {
    void *ojbc_direct_sym = symrez_resolve_once("Foundation", "-[NSXPCConnection _initWithPeerConnection:name:options:]");
    XCTAssertTrue(ojbc_direct_sym);
}

- (void)testResolveSymbol_unexported1 {
    void *_xpc_endpoint_create = symrez_resolve_once("libxpc.dylib", "__xpc_endpoint_create");
    XCTAssertTrue(_xpc_endpoint_create);
}

- (void)testFindImage_name_path {
    mach_header_t hdr1 = find_image("/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation");
    mach_header_t hdr2 = find_image("Foundation");
    XCTAssertTrue(hdr1 != NULL);
    XCTAssertTrue(hdr2 != NULL);
    XCTAssertEqual(hdr1, hdr2);
}

- (void)testFindImage_not_loaded {
    symrez_t sr = symrez_new("AAAAAAAAAAAA");
    XCTAssertEqual(sr, NULL);
}

@end
