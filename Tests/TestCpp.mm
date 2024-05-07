//
//  TestCpp.m
//  Tests
//
//  Created by Jeremy on 4/12/23.
//  Copyright © 2023 Jeremy Legendre. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <SymRez/SymRez.h>
#include <iostream>

@interface TestCpp : XCTestCase

@end

@implementation TestCpp

- (void)setUp {
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)testSymRezConstruct {
    SymRez sr("CoreFoundation");
    for (const auto& i : sr) {
        std::cout << i.name() << "\n";
    }

    auto p = sr.resolveSymbol<void>("_CFStringGetCStringPtr");
    XCTAssertTrue(p);
}

@end
