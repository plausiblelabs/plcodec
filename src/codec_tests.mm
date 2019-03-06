/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "codec.hpp"
#import "PLSmallTestExt.hpp"
#import "codec_tests.hpp"

#include <PLStdCPP/ftl/functional.h>
#include <PLStdCPP/ftl/vector.h>
#include <PLStdCPP/ftl/tuple.h>
#include <PLStdCPP/ftl/function.h>
#include <PLStdCPP/ftl/either_trans.h>
#include <PLStdCPP/ftl/concepts/monad.h>

/* File format version record. */
PL_RECORD_STRUCT(
    TestRecordVersion,
    (uint8_t, compatVersion),
    (uint8_t, featureVersion)
);

/* File section record */
PL_RECORD_STRUCT(
    TestSectionRecord,
    (uint64_t, offset),
    (uint64_t, length)
);

/* File header */
PL_RECORD_STRUCT(
    TestFileHeader,
    (TestRecordVersion, version),
    (TestSectionRecord, dataSection),
    (TestSectionRecord, metaSection)
);

xsm_given("Codec") {
    
    xsm_when("using integral codecs") {

#define BYTEVEC(...) ByteVector {__VA_ARGS__}
        
#define XCTAssertIntegralRoundTripB(size, value, bytes) \
    xsm_then("int" #size " values should round-trip") { \
        XCTAssertRoundTripBytes(codecs::int##size, (int##size##_t)value, BYTEVEC bytes); \
    } \
    xsm_then("uint" #size " values should round-trip") { \
        XCTAssertRoundTripBytes(codecs::uint##size, (uint##size##_t)value, BYTEVEC bytes); \
    }

#define XCTAssertIntegralRoundTripL(size, value, bytes) \
    xsm_then("little-endian int" #size " values should round-trip") { \
        XCTAssertRoundTripBytes(codecs::int##size##L, (int##size##_t)value, BYTEVEC bytes); \
    } \
    xsm_then("little-endian uint" #size " values should round-trip") { \
        XCTAssertRoundTripBytes(codecs::uint##size##L, (uint##size##_t)value, BYTEVEC bytes); \
    }

        XCTAssertIntegralRoundTripB( 8, 7, (0x07))

        XCTAssertIntegralRoundTripB(16, 7, (0x00, 0x07))
        XCTAssertIntegralRoundTripL(16, 7, (0x07, 0x00))

        XCTAssertIntegralRoundTripB(32, 7, (0x00, 0x00, 0x00, 0x07))
        XCTAssertIntegralRoundTripL(32, 7, (0x07, 0x00, 0x00, 0x00))

        XCTAssertIntegralRoundTripB(64, 7, (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07))
        XCTAssertIntegralRoundTripL(64, 7, (0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00))
    }
    
    
    xsm_when("using a fixed-size codec") {

        xsm_then("a uint8 value should round-trip") {
            auto codec = codecs::fixedSizeBytes(4, codecs::uint32);
            XCTAssertRoundTrip(codec, (uint32_t)42);
        }
        
        xsm_then("the result should be padded with zeros when value needs less space than the given length") {
            auto codec = codecs::fixedSizeBytes(3, codecs::uint8);
            auto encoded = codec.encode(7);
            auto expected = ByteVector { 0x07, 0x00, 0x00};
            
            /* Verify the result */
            encoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTFail("Encoding failed: %s", e->message().c_str()); },
                [&](const ByteVector &bv) { XCTAssertTrue(expected == bv); }
            );
        }

        xsm_then("encoding should fail when value needs more space than the given length") {
            auto codec = codecs::fixedSizeBytes(2, codecs::uint32);
            auto encoded = codec.encode(7);
            
            /* Verify the result */
            encoded.matchE (
                            [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Encoding requires 4 bytes but codec is limited to fixed length of 2 bytes", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const ByteVector &bv) { XCTFail("Encoding should fail"); }
            );
        }
        
        xsm_then("decoding should fail when vector has less space than the given length") {
            auto codec = codecs::fixedSizeBytes(4, codecs::uint32);
            auto bv = ByteVector { 0x00, 0x07 };
            auto decoded = codec.decode(bv);
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Requested offset of 0 and length of 4 bytes exceeds buffer length of 2", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<uint32_t> &result) { XCTFail("Decoding should fail"); }
            );
        }
        
        xsm_then("decoding should return a remainder vector that had `length` bytes dropped") {
            auto codec = codecs::fixedSizeBytes(3, codecs::uint8);
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            auto decoded = codec.decode(bv);
            auto expectedRemainder = ByteVector { 0x03, 0x04 };
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) {
                    XCTFail("Decoding failed: %s", e->message().c_str());
                },
                [&](const DecodeResult<uint8_t> &result) {
                    XCTAssertEqual((uint8_t)0x07, result.value());
                    XCTAssertTrue(expectedRemainder == result.remainder());
                }
            );
        }
    }
    
    
    xsm_when("using the identity byte vector codec") {
        
        xsm_then("a byte vector should round-trip") {
            auto codec = codecs::identityBytes;
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            XCTAssertRoundTrip(codec, bv);
        }
    }
    
    
    xsm_when("using a byte vector codec") {
        
        xsm_then("a byte vector should round-trip") {
            auto codec = codecs::bytes(5);
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            XCTAssertRoundTrip(codec, bv);
        }
        
        xsm_then("decoding should fail when vector has less space than the given length") {
            auto codec = codecs::bytes(4);
            auto bv = ByteVector { 0x00, 0x07 };
            auto decoded = codec.decode(bv);
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Requested offset of 0 and length of 4 bytes exceeds buffer length of 2", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<ByteVector> &result) { XCTFail("Decoding should fail"); }
            );
        }
        
        xsm_then("decoding should return a remainder vector that had `length` bytes dropped") {
            auto codec = codecs::bytes(3);
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            auto decoded = codec.decode(bv);
            auto expectedBytes = ByteVector { 0x07, 0x01, 0x02 };
            auto expectedRemainder = ByteVector { 0x03, 0x04 };
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) {
                    XCTFail("Decoding failed: %s", e->message().c_str());
                },
                [&](const DecodeResult<ByteVector> &result) {
                    XCTAssertTrue(expectedBytes == result.value());
                    XCTAssertTrue(expectedRemainder == result.remainder());
                }
            );
        }
    }
    
    xsm_when("using the eager byte vector codec") {
        xsm_then("encoding and decoding should round-trip") {
            auto codec = codecs::eager(codecs::bytes(5));
            std::vector<uint8_t> bytes = { 0x07, 0x01, 0x02, 0x03, 0x04 };
            XCTAssertRoundTrip(codec, bytes);
        }
    }
    
    xsm_when("using a variable-length codec") {

        xsm_then("a length-delimited byte vector should round-trip") {
            auto codec = codecs::variableSizeBytes(codecs::uint32, codecs::identityBytes);
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            XCTAssertRoundTrip(codec, bv);
        }
        
        xsm_then("encoding should fail when length of encoded value is too large for the length codec") {
            auto codec = codecs::variableSizeBytes(codecs::uint8, codecs::identityBytes);
            auto bv = ByteVector::Memset(0x7, 256);
            auto encoded = codec.encode(bv);

            /* Verify the result */
            encoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Length of encoded value (256 bytes) is greater than maximum value (255) of length type", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const ByteVector &bv) { XCTFail("Encoding should fail"); }
            );
        }
    }

    
    xsm_when("using an ignore codec") {
        
        xsm_then("encoding should result in a vector containing zeros") {
            auto codec = codecs::ignore(3);
            auto encoded = codec.encode(pl::unit);
            auto expected = ByteVector::Memset(0, 3);
            
            /* Verify the result */
            encoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTFail("Encoding failed: %s", e->message().c_str()); },
                [&](const ByteVector &bv) { XCTAssertTrue(expected == bv); }
            );
        }
        
        xsm_then("decoding should succeed if the input vector is long enough") {
            auto codec = codecs::ignore(3);
            auto bv = ByteVector { 0x07, 0x01, 0x02, 0x03, 0x04 };
            auto decoded = codec.decode(bv);
            auto expectedRemainder = ByteVector { 0x03, 0x04 };
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) {
                    XCTFail("Decoding failed: %s", e->message().c_str());
                },
                [&](const DecodeResult<pl::Unit> &result) {
                    XCTAssertTrue(expectedRemainder == result.remainder());
                }
            );
        }
        
        xsm_then("decoding should fail if the input vector is smaller than ignored length") {
            auto codec = codecs::ignore(3);
            auto bv = ByteVector { 0x07 };
            auto decoded = codec.decode(bv);
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Requested offset of 3 bytes exceeds buffer length of 1", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<pl::Unit> &result) { XCTFail("Decoding should fail"); }
            );
        }
    }
    
    
    xsm_when("using a constant codec") {
        
        xsm_then("encoding should result in a vector that is identical to the input vector") {
            auto input = ByteVector { 0x01, 0x02, 0x03, 0x04 };
            auto codec = codecs::constant(input);
            auto encoded = codec.encode(pl::unit);
            
            /* Verify the result */
            encoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTFail("Encoding failed: %s", e->message().c_str()); },
                [&](const ByteVector &bv) { XCTAssertTrue(bv == input); }
            );
        }
        
        xsm_then("decoding should succeed if the input vector matches the constant vector") {
            auto input = ByteVector { 0x01, 0x02, 0x03, 0x04 };
            auto codec = codecs::constant(ByteVector { 0x01, 0x02, 0x03 });
            auto decoded = codec.decode(input);
            auto expectedRemainder = ByteVector { 0x04 };

            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) {
                    XCTFail("Decoding failed: %s", e->message().c_str());
                },
                [&](const DecodeResult<pl::Unit> &result) {
                    XCTAssertTrue(expectedRemainder == result.remainder());
                }
            );
        }
        
        xsm_then("decoding should fail if the input vector does not match the constant vector") {
            auto input = ByteVector { 0x01, 0x02, 0x03, 0x04 };
            auto codec = codecs::constant(ByteVector { 0x06, 0x06, 0x06 });
            auto decoded = codec.decode(input);
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Expected constant 060606 but got 010203", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<pl::Unit> &result) { XCTFail("Decoding should fail"); }
            );
        }
        
        xsm_then("decoding should fail if the input vector is smaller than the constant vector") {
            auto input = ByteVector { 0x01 };
            auto codec = codecs::constant(ByteVector { 0x06, 0x06, 0x06 });
            auto decoded = codec.decode(input);
            
            /* Verify the result */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"Requested offset of 0 and length of 3 bytes exceeds buffer length of 1", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<pl::Unit> &result) { XCTFail("Decoding should fail"); }
            );
        }
    }
    
    
    xsm_when("applying the | operator") {
        
        xsm_then("context should be pushed into the given codec") {
            using codecs::operator|;
            
            auto input = ByteVector { 0x01, 0x02, 0x03, 0x04 };
            auto codec =
                ("section" |
                    ("header" |
                        ("magic" | codecs::constant(ByteVector { 0x06, 0x06, 0x06 }))
                    )
                );
            auto decoded = codec.decode(input);
            
            /* Verify that the error message is prefixed with the correct context */
            decoded.matchE (
                [self](ftl::Left<ParseError> e) { XCTAssertEqualObjects(@"section/header/magic: Expected constant 060606 but got 010203", [NSString stringWithUTF8String:e->message().c_str()]); },
                [&](const DecodeResult<pl::Unit> &result) { XCTFail("Decoding should fail"); }
            );
        }
    }
    

    xsm_when("constructing a tupled codec") {
        xsm_then("the generated codec should encode tuple values") {
            TupledCodec<uint8_t, uint16_t, uint16_t> tupled = (
                codecs::uint8 &
                codecs::uint16 &
                codecs::uint16
            );
            
            XCTAssertRoundTrip(tupled.codec(), (std::make_tuple<uint8_t, uint16_t, uint16_t>(0xAB, 0xCAFE, 0xF00D)));
        }
        
        xsm_then("given an isomorphism between the tupled codec and type `T', the codec should be mappable to a Codec<T>") {
            using namespace codecs;

            /* Define our codecs */
            auto versionCodec = (
                // These strings are used for error reporting, providing additional context on parse errors.
                ("record_compat_version"     | codecs::uint8  ) &
                ("record_feature_version"    | codecs::uint8  )
            ).as<TestRecordVersion>();

            auto sectionCodec = (
                /* The base uintXX codecs assume big endian serialization; the `L' variants assume little endian serialization. */
                ("section_offset"            | codecs::uint64L  ) &
                ("section_length"            | codecs::uint64L  )
            ).as<TestSectionRecord>();

            const auto fileMagic = ByteVector::Bytes("magic", 5, false);
            auto headerCodec = (
                ("magic"                     | constant(fileMagic)    ) >>
                ("file_version"              | versionCodec           ) &
                ("data_sect"                 | sectionCodec           ) &
                ("meta_sect"                 | sectionCodec           )
            ).as<TestFileHeader>();
            
            /* Verify round-trip encoding and decoding of TestFileHeader instances. */
            auto header = TestFileHeader(TestRecordVersion(1, 2), TestSectionRecord(10, 20), TestSectionRecord(30, 40));
            XCTAssertRoundTrip(headerCodec, header);
        }
        
        xsm_then("a codec of unit type that uses the >> operator should be dropped from the resulting tuple type (take 1)") {
            auto tupled = (
                codecs::constant(ByteVector { 0x07 }) >>
                codecs::uint8
            );
         
            auto expected = ByteVector { 0x07, 0xAB };
            XCTAssertRoundTripBytes(tupled, (uint8_t)0xAB, expected);
        }
        
        xsm_then("a codec of unit type that uses the >> operator should be dropped from the resulting tuple type (take 2)") {
            auto tupled = (
                codecs::constant(ByteVector { 0x66 }) >>
                codecs::uint16 &
                codecs::uint8
            );
            
            auto expected = ByteVector { 0x66, 0xCA, 0xFE, 0x42 };
            XCTAssertRoundTripBytes(tupled.codec(), (std::make_tuple<uint16_t, uint8_t>(0xCAFE, 0x42)), expected);
        }
        
        xsm_then("a codec of unit type that uses the >> operator should be dropped from the resulting tuple type (take 3)") {
            auto tupled = (
                codecs::uint8 &
                codecs::constant(ByteVector { 0x07 }) >>
                codecs::uint8 &
                codecs::constant(ByteVector { 0x66 }) >>
                codecs::uint16 &
                codecs::uint8
            );
            
            auto expected = ByteVector { 0xFF, 0x07, 0xAB, 0x66, 0xCA, 0xFE, 0x42 };
            XCTAssertRoundTripBytes(tupled.codec(), (std::make_tuple<uint8_t, uint8_t, uint16_t, uint8_t>(0xFF, 0xAB, 0xCAFE, 0x42)), expected);
        }
    }
    
    
    xsm_when("applying flatPrepend (the >>= operator)") {
        xsm_then("the second codec should be dependent on the data from the first codec") {
            auto tupled = (
                codecs::uint8 >>= [&](const uint8_t &value) {
                    return (codecs::bytes((std::size_t)value) & codecs::uint16);
                }
            );

            auto input = std::make_tuple<uint8_t, ByteVector, uint16_t>(0x01, ByteVector { 0xAB }, 0xCAFE);
            auto expected = ByteVector { 0x01, 0xAB, 0xCA, 0xFE };
            XCTAssertRoundTripBytes(tupled, (input), expected);
        }
    }
}
