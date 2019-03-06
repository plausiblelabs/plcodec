/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"
#import "PLSmallTestExt.hpp"
#import "plist_record_type.hpp"
#import "codec_tests.hpp"

#include <string>

namespace pl {
    
    PL_PLIST_STRUCT(TestStruct,
        (std::string, name),
        (int, count)
    );
    
    PL_PLIST_STRUCT(TestInt, (int, value));
    PL_PLIST_STRUCT(TestNSUInteger, (NSInteger, value));
    PL_PLIST_STRUCT(TestNSInteger, (NSInteger, value));
    PL_PLIST_STRUCT(TestBoolean, (bool, value));
    PL_PLIST_STRUCT(TestFloat, (float, value));
    PL_PLIST_STRUCT(TestDouble, (double, value));
    PL_PLIST_STRUCT(TestNSString, (std::string, value));
    PL_PLIST_STRUCT(TestNSArray, (std::vector<NSUInteger>, value));
    PL_PLIST_STRUCT(TestMaybeValue, (ftl::maybe<int>, value));
    PL_PLIST_STRUCT(TestNSPtrValue, (ftl::maybe<ns_ptr<NSString *>>, value));
    PL_PLIST_STRUCT(TestSharedPtrValue, (std::shared_ptr<std::string>, value));

    PL_PLIST_STRUCT(TestInner,
        (std::string, one),
        (std::string, two)
    );
    
    PL_PLIST_STRUCT(TestOuter,
        (std::string, identifier),
        (std::vector<TestInner>, contents)
    );
    
    static NSString *ToNSString(const std::string &str) {
        return [NSString stringWithUTF8String: str.c_str()];
    }
    
    
    /**
     * Perform round-trip encoding and decoding of the given value, asserting on success
     * and equality of the result.
     *
     * @param recordType The plist record type.
     * @param value The plist value to be decoded/encoded and used for equality comparison.
     */
#define AssertRoundTrip(recordType, ...) AssertRoundTrip_impl<recordType>(self, __VA_ARGS__)
    
    /* Backing implementation of AssertRoundTrip */
    template <typename R> void AssertRoundTrip_impl (id self, NSDictionary *value, PropertyListExtraKeysOptions extraKeysOptions = PropertyListNoExtraKeys) {
        auto decodedEither = R::from_plist(value);
        XSMAssertRight(decodedEither);

        /* Perform round-trip encoding */
        auto result = [self, &value, &extraKeysOptions](const R &decoded) { return make_ns_ptr(decoded.to_plist(extraKeysOptions)); } % decodedEither;
        
        /* Verify the result */
        result.matchE (
           [self](ftl::Left<ParseError> e) { XCTFail("Round-trip encoding failed: %s", e->message().c_str()); },
           [&](const ns_ptr<NSDictionary *> decoded) { XCTAssertEqualObjects(value, *decoded); }
        );
    }

    
#define AssertEqualStrings(str1, str2, ...) XCTAssertEqualObjects(ToNSString(str1), ToNSString(str2), __VA_ARGS__)
    
    xsm_given("plist_record_type") {
        static_assert(is_plist_record<TestStruct>::value, "PL_PLIST_STRUCT-defined types should be detected as structually type compatible by is_plist_record()");
        
        xsm_when("converting property list types") {
            xsm_then("NSNumber should be mappable to int")          { AssertRoundTrip(TestInt,          @{ @"value" : @(42) }           );}
            xsm_then("NSNumber should be mappable to NSInteger")    { AssertRoundTrip(TestNSInteger,    @{ @"value" : @(42) }           );}
            xsm_then("NSNumber should be mappable to NSUInteger")   { AssertRoundTrip(TestNSUInteger,   @{ @"value" : @(42U) }          );}
            xsm_then("NSNumber should be mappable to bool")         { AssertRoundTrip(TestBoolean,      @{ @"value" : @(false) }   );}
            xsm_then("NSNumber should be mappable to float")        { AssertRoundTrip(TestFloat,        @{ @"value" : @(1.0f) }   );}
            xsm_then("NSNumber should be mappable to double")       { AssertRoundTrip(TestDouble,       @{ @"value" : @(1.0) }   );}
            xsm_then("NSString should be mappable to std::string")  { AssertRoundTrip(TestNSString,     @{ @"value" : @"string value" } );}
            xsm_then("NSArray should be mappable to std::vector")   { AssertRoundTrip(TestNSArray,      @{ @"value" : @[ @(42U) ] }     );}
        }
        
        xsm_when("handling optional values with ftl::maybe") {
            xsm_then("round-trip parsing should succeed with a missing value") {
                AssertRoundTrip(TestMaybeValue, @{});
                AssertRoundTrip(TestMaybeValue, @{});
            }
            
            xsm_then("round-trip parsing should succeed with a non-missing value") {
                AssertRoundTrip(TestMaybeValue, @{ @"value" : @(42) });
            }
        }
        
        xsm_when("handling std::shared_ptr-wrapped values") {
            xsm_then("round-trip parsing should succeed") {
                AssertRoundTrip(TestSharedPtrValue, @{ @"value" : @"string value" });
            }
        }
        
        xsm_when("handling ns_ptr-wrapped values") {
            xsm_then("round-trip parsing should succeed") {
                AssertRoundTrip(TestNSPtrValue, @{ @"value" : @"string value" });
            }
        }
        
        xsm_when("parsing valid plists") {
            xsm_then("parsing should round-trip") {
                AssertRoundTrip(TestStruct, (@{ @"name": @"abcd", @"count": @42 }));
            }
        }
        
        xsm_when("parsing plists containing valid keys with invalid types") {
            id plist1 = @{ @"name": @42, @"count": @42 };
            id plist2 = @{ @"name": @"abcd", @"count": @"forty-two" };
            
            auto x1 = TestStruct::from_plist(plist1);
            auto x2 = TestStruct::from_plist(plist2);
            
            xsm_then("number instead of string should fail") {
                XSMAssertLeft(x1);
            }
            xsm_then("string instead of number should fail") {
                XSMAssertLeft(x2);
            }
        }
        
        xsm_when("working with good nested plists") {
            id innerPlists = @[ @{ @"one": @"abc",
                                   @"two": @"def" },
                                @{ @"one": @"ghi",
                                   @"two": @"jkl" }];
            id plist = @{ @"identifier": @"123",
                          @"contents": innerPlists };
            TestOuter outer = ftl::fromRight(TestOuter::from_plist(plist));
            
            xsm_then("extraction should succeed") {
                AssertEqualStrings(outer.identifier(), "123");
                XCTAssertEqual((int)outer.contents().size(), 2);
                AssertEqualStrings(outer.contents()[0].one(), "abc");
                AssertEqualStrings(outer.contents()[0].two(), "def");
                AssertEqualStrings(outer.contents()[1].one(), "ghi");
                AssertEqualStrings(outer.contents()[1].two(), "jkl");
            }
            
            xsm_then("roundtrip should preserve the plist") {
                id plist2 = outer.to_plist();
                XCTAssertEqualObjects(plist, plist2);
            }
        }
        
        xsm_when("working with bad nested plists") {
            id innerPlists = @[ @{ @"collectionUUID": @"abc",
                                   @"itemUUID": @"def" },
                                @{ @"collectionUUID": @"ghi",
                                   @"itemUUID": @42 }];
            id plist = @{ @"collectionUUID": @"123",
                          @"contents": innerPlists };
            auto result = TestOuter::from_plist(plist);
            
            xsm_then("bad nested plists should fail") {
                XSMAssertLeft(result);
            }
        }
        
        xsm_when("using the plist codec") {
            auto example = TestStruct("a name", 42);
            
            xsm_then("round-trip encoding should succeed") {
                auto codec = plist_record_codec<TestStruct>(NSPropertyListXMLFormat_v1_0);
                XCTAssertRoundTrip(codec, example);
            }
            
            xsm_then("encoding should use the specified XML property list format") {
                auto codec = plist_record_codec<TestStruct>(NSPropertyListXMLFormat_v1_0);
                ByteVector encoded = ftl::fromRight(codec.encode(example));

                NSPropertyListFormat format;
                NSDictionary *decoded = [NSPropertyListSerialization propertyListWithData: ftl::fromRight(encoded.toNSData()).get() options: 0 format: &format error: nullptr];
                XCTAssertNotNil(decoded);
                XCTAssertEqual(format, NSPropertyListXMLFormat_v1_0);
            }
            
            xsm_then("encoding should use the specified binary property list format") {
                auto codec = plist_record_codec<TestStruct>(NSPropertyListBinaryFormat_v1_0);
                ByteVector encoded = ftl::fromRight(codec.encode(example));
                
                NSPropertyListFormat format;
                NSDictionary *decoded = [NSPropertyListSerialization propertyListWithData: ftl::fromRight(encoded.toNSData()).get() options: 0 format: &format error: nullptr];
                XCTAssertNotNil(decoded);
                XCTAssertEqual(format, NSPropertyListBinaryFormat_v1_0);
            }
            
            xsm_then("Invalid plist data should fail") {
                auto codec = plist_record_codec<TestStruct>(NSPropertyListXMLFormat_v1_0);
                
                auto bad_bytes = ByteVector::String("blah blah !#$Z*)G@%*G#($hoR;keuqcf87l34'5#$<%\x12\x34\x01", false);
                XSMAssertLeft(codec.decode(bad_bytes));
            }
            
            xsm_then("Decoding a non-dictionary property list should fail") {
                auto codec = plist_record_codec<TestStruct>(NSPropertyListXMLFormat_v1_0);

                NSArray *plist = @[ @"one", @"two" ];
                auto data = ByteVector::fromNSData([NSPropertyListSerialization dataWithPropertyList: plist format: NSPropertyListXMLFormat_v1_0 options: 0 error: nullptr]);
                XSMAssertLeft(codec.decode(data));
            }
        }
        
        xsm_when("parsing plists that contain extra keys") {
            xsm_then("the extra keys should be preserved when requested") {
                AssertRoundTrip(TestInt, (@{ @"value" : @(42), @"extra1": @(1), @"extra2": @"two" }), PropertyListIncludeExtraKeys);
                
                id innerPlists = @[ @{ @"one": @"abc",
                                       @"two": @"def",
                                       @"three": @"ghi" },
                                    @{ @"one": @"jkl",
                                       @"two": @"mno",
                                       @"three": @"pqr" }];
                id plist = @{ @"identifier": @"123",
                              @"contents": innerPlists,
                              @"extra": @"hello" };
                AssertRoundTrip(TestOuter, plist, PropertyListIncludeExtraKeys);
            }
            
            xsm_then("the extra keys should not be preserved when not requested") {
                id innerPlists = @[ @{ @"one": @"abc",
                                       @"two": @"def",
                                       @"three": @"ghi" },
                                    @{ @"one": @"jkl",
                                       @"two": @"mno",
                                       @"three": @"pqr" }];
                id plist = @{ @"identifier": @"123",
                              @"contents": innerPlists,
                              @"extra": @"hello" };
                
                id innerGoalPlists = @[ @{ @"one": @"abc",
                                           @"two": @"def" },
                                        @{ @"one": @"jkl",
                                           @"two": @"mno" }];
                id goalPlist = @{ @"identifier": @"123",
                                  @"contents": innerGoalPlists };
                
                
                TestOuter outer = ftl::fromRight(TestOuter::from_plist(plist));
                id plist2 = outer.to_plist();
                XCTAssertEqualObjects(goalPlist, plist2);
            }
        }
    }
    
}
