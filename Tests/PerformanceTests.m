//
//  PerformanceTests.m
//  Tests
//
//  Created by Jeremy on 4/12/23.
//  Copyright © 2023 Jeremy Legendre. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <SymRez.h>
#import <mach/task.h>
#import <mach-o/dyld_images.h>
#import <CoreFoundation/CoreFoundation.h>

extern void * resolve_exported_symbol(symrez_t symrez, const char *symbol);
extern mach_header_t find_image(const char *image_name);
@interface PerformanceTests : XCTestCase

@end

@implementation PerformanceTests {
    struct dyld_all_image_infos *aii;
}

- (void)setUp {
    task_dyld_info_data_t dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
    
    aii = (void*)(dyld_info.all_image_info_addr);
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
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

- (void)testPerformanceResolveExported {
    symrez_t sr = symrez_new("AppKit");
    [self measureBlock:^{
        resolve_exported_symbol(sr, "__nsBeginNSPSupport");
    }];

    sr_free(sr);
}

- (void)testPerformanceFindImageByName {
    const struct dyld_image_info *info_array = aii->infoArray;
    const char *p = info_array[(aii->infoArrayCount - 1)].imageFilePath;
    p = strrchr(p, '/');
    ++p;
    [self measureBlock:^{
        mach_header_t mh = find_image(p);
        XCTAssertTrue(mh);
    }];
}


- (void)testPerformanceFindImageByPath {
    const struct dyld_image_info *info_array = aii->infoArray;
    const char *p = info_array[(aii->infoArrayCount - 1)].imageFilePath;
    [self measureBlock:^{
        mach_header_t mh = find_image(p);
        XCTAssertTrue(mh);
    }];
}

- (void)testPerformanceResolveOnce {
    [self measureBlock:^{
        void *p = symrez_resolve_once("AppKit", "__nsBeginNSPSupport");
        XCTAssertTrue(p);
    }];
}

@end
