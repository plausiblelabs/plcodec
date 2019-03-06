/*
 * Author: Landon Fuller <landonf@plausible.coop>
 *
 * Copyright (c) 2013-2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#import "XSmallTest.h"

#import "ns_ptr.hpp"

using namespace pl;

xsm_given("ns_ptr") {
    xsm_when("wrapping ObjC objects") {
        /* Create the target object */
        NSString *string = [[NSString alloc] initWithFormat: @"%@", @"Testing, 1, 2, 3"];

        CFIndex intitialRefCount = CFGetRetainCount((__bridge CFTypeRef) string);
        const auto ptr = ns_ptr<NSString*>(string);
        
        xsm_then("the initial refcount must be incremented") {
            XCTAssertEqual(ptr.referenceCount(), intitialRefCount+1, @"Incorrect refcount");
        }
        
        xsm_then("derefencing the ns_ptr should return the original value") {
            XCTAssertEqual(*ptr, string, @"Wrong value returned");
            XCTAssertEqual(ptr.get(), string, @"Wrong value returned");
        }
        
        xsm_then("copying the ns_ptr should increment the reference count") {
            CFIndex refCount = ptr.referenceCount();
            auto ptr2 = ptr;
            XCTAssertEqual(ptr2.referenceCount(), refCount+1, @"Incorrect refcount");
        }
        
        xsm_then("assignment overwriting the same backing object should not bump the reference count") {
            auto ptr2 = ptr;
            CFIndex refCount = ptr.referenceCount();

            ptr2 = ptr;
            XCTAssertEqual(ptr2.referenceCount(), refCount, @"Incorrect refcount");
        }
        
        xsm_then("assignment overwriting a different backing object should bump the reference count of the new backing object") {
            auto ptr2 = ns_ptr<NSString*>(@"Other string");
            
            CFIndex refCount = ptr.referenceCount();
            ptr2 = ptr;
            XCTAssertTrue(ptr.referenceCount() >= refCount+1, @"Incorrect refcount");
        }
        
        xsm_then("equality comparison should work as per the Objective-C isEqual: invariants") {
            XCTAssertEqual(ptr, make_ns_ptr<NSString*>(@"Testing, 1, 2, 3"));
            XCTAssertNotEqual(ptr, make_ns_ptr<NSString*>(@"Other String"));
            XCTAssertNotEqual(ptr, make_ns_ptr<NSString*>(nil));
        }
        
        xsm_then("equality comparison on two empty ns_ptrs should return true") {
            XCTAssertEqual(make_ns_ptr<id>(nil), make_ns_ptr<id>(nil));
        }
        
        xsm_then("std::hash should return the backing object's -hash value.") {
            std::hash<ns_ptr<NSString*>> h;
            
            XCTAssertEqual(h(ptr), h(make_ns_ptr<NSString*>(@"Testing, 1, 2, 3")));
            XCTAssertNotEqual(h(ptr), h(make_ns_ptr<NSString*>(@"Other String")));
        }
    }
    
    xsm_when("performing implicit conversion of Objective-C types to the common id base type") {
        /* Create the target object */
        NSString *string = [[NSString alloc] initWithFormat: @"%@", @"Testing, 1, 2, 3"];
        XCTAssertEqual(CFGetRetainCount((__bridge CFTypeRef) string), (CFIndex)1, @"Incorrect refcount");
        
        /* Adopt our existing ownership of the value. */
        auto stringRef = ns_ptr<NSString*>(string);
        CFIndex refCount = stringRef.referenceCount();
        
        xsm_then("the implicit conversion should increment the refcount") {
            /* Let ns_ptr perform an implicit conversion from a ns_ptr<CFStringRef> to an ns_ptr<CFTypeRef> */
            const ns_ptr<id> typeRef = stringRef;
            XCTAssertEqual(typeRef.referenceCount(), refCount+1, @"Incorrect refcount");
        }
    }
}

