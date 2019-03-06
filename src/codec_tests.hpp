/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#pragma once

#include "codec.hpp"

/**
 * Perform round-trip encoding and decoding of the given value, asserting on success
 * and equality of the result.
 *
 * @param codec The codec to be used for encoding/decoding.
 * @param value The value to be encoded and used for equality comparison.
 */
#define XCTAssertRoundTrip(codec, value) XCTAssertRoundTripBytes_Impl(self, codec, value, ftl::nothing<ByteVector>())

/**
 * Perform round-trip encoding and decoding of the given value, asserting on success
 * and equality of the result.
 *
 * @param codec The codec to be used for encoding/decoding.
 * @param value The value to be encoded and used for equality comparison.
 * @param bytes The raw byte vector to compare to the result of the decode operation.
 */
#define XCTAssertRoundTripBytes(codec, value, bytes) XCTAssertRoundTripBytes_Impl(self, codec, value, ftl::just(bytes))

/* Backing implementation of XCTAssertRoundTrip[Bytes] */
template <typename T> void XCTAssertRoundTripBytes_Impl (id self, const Codec<T> &codec, const T &value, const ftl::maybe<ByteVector> &rawBytes) {
    /* Perform round-trip encoding */
    auto result = [](const DecodeResult<T> &result) {
        /* Return only the decoded value, dropping the remainder */
        return result.value();
    } % (codec.encode(value) >>= [self, &codec, &rawBytes](const ByteVector &bv) {
        return rawBytes.match (
            [&](const ByteVector rawbv) {
                if (!(rawbv == bv)) {
                    return fail<DecodeResult<T>>("Encoded bytes do not match expected bytes");
                } else {
                    return codec.decode(bv);
                }
            },
            [&](ftl::Nothing) {
                return codec.decode(bv);
            }
        );
    });
    
    /* Verify the result */
    result.matchE (
        [self](ftl::Left<ParseError> e) { XCTFail("Round-trip encoding failed: %s", e->message().c_str()); },
        [&](const T &decoded) { XCTAssertTrue(value == decoded); }
    );
}
