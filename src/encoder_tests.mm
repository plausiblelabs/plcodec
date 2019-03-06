/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "codec.hpp"
#import "encoder.hpp"
#import "decoder.hpp"
#import "PLSmallTestExt.hpp"


struct EncoderTestProductClass {
    uint8_t _t;
    uint16_t _u;
    uint32_t _v;
    
    EncoderTestProductClass (uint8_t t, uint16_t u, uint32_t v) : _t(t), _u(u), _v(v) {}
};

template<> struct product<EncoderTestProductClass> {
    using P = std::tuple<uint8_t, uint16_t, uint32_t>;
    static P unapply (const EncoderTestProductClass &p) { return std::make_tuple(p._t, p._u, p._v); }
    static EncoderTestProductClass apply (const P &values) { return EncoderTestProductClass(std::get<0>(values), std::get<1>(values), std::get<2>(values)); }
    
    static constexpr bool instance = true;
};

xsm_given("Encoder") {
    xsm_when("operating on a tuple<T, U, V> encoder") {
        /* Set up a tupled codec */
        auto tupledCodec = (
            codecs::uint8 &
            codecs::uint16 &
            codecs::uint32
        ).codec();
        
        /* Encode a set of values */
        EncoderTestProductClass value { 0xFF, 0xFFFF, 0xFFFFFFFF };
        
        /* Extract just the encoder */
        auto tupledEncoder = tupledCodec.encoder();
        
        xsm_then("the encoder should be mappable over product-implementing types") {
            auto encoder = tupledEncoder.as<EncoderTestProductClass>();
            
            auto result = ftl::fromRight(encoder.encode(value));

            auto directEncoded = ftl::fromRight(tupledEncoder.encode(product<EncoderTestProductClass>::unapply(value)));
            XCTAssertTrue(result == directEncoded);
        }
    }
}