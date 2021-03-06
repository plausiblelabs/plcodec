/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#pragma once

#include <PLStdCPP/ftl/functional.h>
#include <PLStdCPP/ftl/either.h>
#include <PLStdCPP/ftl/maybe.h>

#include <PLCodec/parse_error.hpp>

#ifdef __OBJC__
#include <Foundation/Foundation.h>
#include "ns_ptr.hpp"
#endif

/* C++ / FTL extensions for XSmallTest / XCTest */

/** 
 * @internal
 * XCTest-compatible assertion implementations
 */
struct XCTestExt {
    /** Assert that @a result is empty or non-empty, and if non-empty, that it is equal to the provided expected value. */
    template<typename T> static void assertSome (id self, const ftl::maybe<T> &result, bool expectSome, const T &expectedValue) {
        /* If this doesn't fail, we have the expected value. */
        assertSome(self, result, expectSome);
        
        /* Check the value; we abuse map for our side-effecting test. */
        [&](const T &value) {
            XCTAssertTrue(value == expectedValue, @"some returned, but value != expectedValue");
            return value;
        } % result;
    }

    /** Assert that @a result is either or a left or right value.  */
    template<typename T> static void assertSome (id self, const ftl::maybe<T> &result, bool expectSome) {
        result.matchE(
            [&](const T &n) {
                if (!expectSome) {
                    XCTFail("Tests failed: expected nothing, got a value");
                }
            },
            [&](ftl::otherwise) {
                if (expectSome) {
                    XCTFail("Tests failed: expected a value, got nothing");
                }
            }
        );
    }
    
    /** Assert that @a result is either or a left or right value, and if so, that it is equal to the provided expected value. */
    template<typename E, typename T> static void assertEither (id self, const ftl::either<E, T> &result, bool expectRight, const T &expectedValue) {
        /* If this doesn't fail, we have the expected right or left value. */
        assertEither(self, result, expectRight);
        
        /* Perform our side-effecting equality assertion. */
        if (expectRight) {
            XCTAssertTrue(ftl::fromRight(result) == expectedValue, @"right returned, but value != expectedValue");
        } else {
            XCTAssertTrue(ftl::fromLeft(result) == expectedValue, @"left returned, but value != expectedValue");
        }
    }
    
    /** Assert that @a result is either or a left or right value.  */
    template<typename E, typename T> static void assertEither (id self, const ftl::either<E, T> &result, bool expectRight) {
        result.matchE(
            [&](ftl::Left<ParseError> e) {
                if (expectRight) {
                    XCTFail("Tests failed: %s", e->message().c_str());
                }
            },
#ifdef __OBJC__
            [&](ftl::Left<ns_ptr<NSError *>> e) {
                if (expectRight) {
                    XCTFail("Tests failed: %@", **e);
                }
            },
#endif
            [&](ftl::otherwise) {
                /* Right value */
                if (!expectRight) {
                    XCTFail("Expected failure, but evaluated to a non-error value");
                }
            }
        );
    }
};

#define XSMAssertRight(expr, ...) XCTestExt::assertEither(self, expr, true, ## __VA_ARGS__);
#define XSMAssertLeft(expr, ...) XCTestExt::assertEither(self, expr, false, ## __VA_ARGS__);

#define XSMAssertSome(expr, ...) XCTestExt::assertSome(self, expr, true, ## __VA_ARGS__);
#define XSMAssertNone(expr) XCTestExt::assertSome(self, expr, false);