/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "parse_error.hpp"
#import "PLSmallTestExt.hpp"

#include <string>

#include <PLStdCPP/ftl/functional.h>

xsm_given("ParseError") {
    ParseError error("message");
    using M = ftl::monoid<ParseError>;
    
    xsm_when("pushing context") {
        xsm_then("the error message should include context in the correct order") {
            std::string msg = "This is a slam poem that I wrote and I am speaking the slam poem to you right now with my mouth.";
            std::string expected = "outer/inner: " + msg;
            auto e = ParseError(msg).pushContext("inner").pushContext("outer");
            XCTAssertEqualObjects([NSString stringWithUTF8String: expected.c_str()], [NSString stringWithUTF8String: e.message().c_str()]);
        }
    }
    
    xsm_when("applying the monoid laws") {
        xsm_then("identity element (id + a = a + id = a)") {
            XCTAssertTrue(M::append(M::id(), error) == M::append(error, M::id()));
        }
        
        xsm_then("associativity (a + b) + c = a + (b + c)") {
            ParseError a("a");
            ParseError b("b");
            ParseError c("c");
            
            XCTAssertTrue(
                M::append(M::append(a, b), c) == M::append(a, M::append(b, c))
            );
        }
    }
}