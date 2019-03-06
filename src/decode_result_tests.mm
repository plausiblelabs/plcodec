/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "decode_result.hpp"
#import "PLSmallTestExt.hpp"

#include <PLStdCPP/ftl/functional.h>

xsm_given("DecodeResult") {
    DecodeResult<char> result('c', ByteVector::Empty);
    
    xsm_when("mapping over the value") {
        auto mapped = ftl::fmap([](const char c) -> char {
            return 'b';
        }, result);
        
        xsm_then("the result should contain the new value") {
            XCTAssertTrue('b' == mapped.value());
        }
        
        xsm_then("the result should contain the same vector") {
            XCTAssertTrue(mapped.remainder() == result.remainder());
        }
    }
    
    xsm_when("mapping over the remainer") {
        auto remainder = ByteVector::Bytes("1234", 4, false);
        auto mapped = result.mapRemainder([remainder](const ByteVector &bv) {
            return remainder;
        });
        XCTAssertTrue(mapped.remainder() == remainder);
    }
}
