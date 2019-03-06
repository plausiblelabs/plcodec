/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "codec.hpp"
#import "decoder.hpp"
#import "PLSmallTestExt.hpp"

struct DecoderTestProductClass {
    uint8_t _t;
    uint16_t _u;
    uint32_t _v;
    
    DecoderTestProductClass (uint8_t t, uint16_t u, uint32_t v) : _t(t), _u(u), _v(v) {}
};

template<> struct product<DecoderTestProductClass> {
    using P = std::tuple<uint8_t, uint16_t, uint32_t>;
    static P unapply (const DecoderTestProductClass &p) { return std::make_tuple(p._t, p._u, p._v); }
    static DecoderTestProductClass apply (const P &values) { return DecoderTestProductClass(std::get<0>(values), std::get<1>(values), std::get<2>(values)); }
    
    static constexpr bool instance = true;
};

xsm_given("Decoder") {
    
    xsm_when("operating on a tuple<T, U, V> decoder") {
        /* Set up a tupled codec */
        auto tupledCodec = (
            codecs::uint8 &
            codecs::uint16 &
            codecs::uint32
        ).codec();
        
        /* Encode a set of values */
        auto values = std::make_tuple<uint8_t, uint16_t, uint32_t>(0xFF, 0xFFFF, 0xFFFFFFFF);
        auto encoded = ftl::fromRight(tupledCodec.encode(values));
        
        /* Extract just the decoder */
        auto tupledDecoder = tupledCodec.decoder();
        
        xsm_then("the decoder should be mappable over product-implementing types") {
            auto decoder = tupledDecoder.as<DecoderTestProductClass>();
            DecoderTestProductClass result = ftl::fromRight(decoder.decode(encoded)).value();
            
            XCTAssertEqual(result._t, 0xFF);
            XCTAssertEqual(result._u, 0xFFFF);
            XCTAssertEqual(result._v, 0xFFFFFFFF);
        }
    }
    

}