/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * This API is based on the design of Michael Pilquist and Paul Chiusano's
 * Scala scodec library: https://github.com/scodec/scodec/
 */

#pragma once

#include "byte_vector.hpp"
#include "decode_result.hpp"

#include <PLStdCPP/product_type.hpp>
#include <PLStdCPP/record_type.hpp>

#include <functional>
#include <tuple>

/**
 * Implements decoding of values of type `T` from a ByteVector.
 *
 * @tparam T The value type decoded by this instance.
 */
template<typename T>
class Decoder {
public:
    /** Result type. */
    using Result = ftl::either<ParseError, DecodeResult<T>>;
    
    /** Implementation function type. */
    typedef std::function<Result(const ByteVector &)> Fn;

    /**
     * Construct a new decoder with the given decode function.
     *
     * @param decoder Decode function to be used by this decoder.
     */
    Decoder (const Fn &decoder) : _decoder(decoder) {}
    
    /** Move constructor */
    Decoder (Decoder &&other) = default;
    
    /** Copy constructor */
    Decoder (const Decoder &other) = default;

    /**
     * Attempt to decode a value of type `T` from the given @a bytes.
     *
     * @param bytes Input data to be decoded.
     */
    Result decode (const ByteVector &bytes) const {
        return _decoder(bytes);
    }

    /**
     * Return a new Decoder<C> instance that will map decoded values to an instance
     * of type `C`.
     *
     * @tparam C The class to which a decoded tuple result will be applied.
     */
    template <class C, typename R = ftl::Requires<pl::Product<C>{}>> constexpr Decoder<C> as () const {
        static_assert(std::is_same<T, typename pl::product<C>::P>::value, "`C` cannot be constructed with `T'");

        return ftl::functor<Decoder<T>>::map(
        [](const T &values) {
            return pl::product<C>::apply(values);
        }, *this);
    };
    
    /**
     * Return a new Decoder<C> instance that will map decoded values to a record instance
     * of type `C`.
     *
     * @tparam C The class to which a decoded tuple result will be applied.
     */
    template <class C> constexpr typename std::enable_if<pl::is_record<C>::value, Decoder<C>>::type as () const {
        return ftl::functor<Decoder<T>>::map(
            [](const T &values) {
            return C::apply(values);
        }, *this);
    };
    
    /**
     * Return the decoding implementation function.
     */
    const Fn fn () const { return _decoder; }

private:
    /** Decode implementation function */
    const Fn _decoder;
};


/* FTL concept implementation(s) */
namespace ftl {
    // TODO - Should Decoder<T> really be a monad transformer over DecoderResult<T>?
    
    /**
     * Decoder functor implementation.
     */
    template<typename T> struct functor<Decoder<T>> {
        template<typename Fn, typename U = typename std::result_of<Fn(T)>::type>
        static constexpr Decoder<U> map(Fn &&fn, const Decoder<T> &decoder) {
            return Decoder<U>(
                [fn, decoder](const ByteVector &bv) {
                    return [&fn](const DecodeResult<T> &result) {
                        return DecodeResult<U>(fn(result.value()), result.remainder());
                    } % decoder.decode(bv);
                }
            );
        }
        
        static constexpr bool instance = true;
    };
}