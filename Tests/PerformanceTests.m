//
//  PerformanceTests.m
//  Tests
//
//  Created by Jeremy on 4/12/23.
//  Copyright © 2023 Jeremy Legendre. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <SymRez.h>
extern void * resolve_exported_symbol(symrez_t symrez, const char *symbol);

@interface PerformanceTests : XCTestCase

@end

@implementation PerformanceTests

- (void)setUp {
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)testPerformanceResolveExported {
    symrez_t sr = symrez_new("AppKit");
    [self measureBlock:^{
        resolve_exported_symbol(sr, "__nsBeginNSPSupport");
    }];

    sr_free(sr);
}

- (void)testPerformanceResolveSymbol {
    symrez_t sr = symrez_new("AppKit");
    // When resolving symbols, SymRez will first check the symbol
    // table (SYMTAB) then the export tree. The reason for this is
    // to prioritize lookups of symbols not available from dlsym or
    // by linking. __nsBeginNSPSupport is (at the time of writing),
    // the last symbol in AppKit's export tree, making it symbol
    // number 139802.
    // tl;dr: Check how long it takes to resolve the 139802nd symbol
    // in AppKit.

    [self measureBlock:^{
        sr_resolve_symbol(sr, "__nsBeginNSPSupport");
    }];

    sr_free(sr);
}



@end
