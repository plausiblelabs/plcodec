/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * This API is based on the design of Michael Pilquist and Paul Chiusano's
 * Scala scodec library: https://github.com/scodec/scodec/
 */

#pragma once

#include <functional>

#include <PLStdCPP/product_type.hpp>

#include "byte_vector.hpp"
#include "parse_error.hpp"

/**
 * @interface encoder
 *
 * Implements encoding a value of type `T` to a ByteVector.
 *
 * @tparam T The value type encoded by this instance.
 */
template<typename T>
class Encoder {
public:
    using Result = ftl::either<ParseError, ByteVector>;
    
    /** Implementation function type. */
    typedef std::function<Result(const T &)> Fn;

    /**
     * Construct a new encoder with the given encode function.
     *
     * @param encoder Encode function to be used by this encoder.
     */
    Encoder (const Fn &encoder) : _encoder(encoder) {}
    
    /** Move constructor */
    Encoder (Encoder &&other) = default;
    
    /** Copy constructor */
    Encoder (const Encoder &other) = default;
    
    /**
     * Attempt to encode a value of type `T`.
     *
     * @param value Value to be encoded.
     */
    Result encode (const T &value) const {
        return _encoder(value);
    }
    
    /**
     * Return the encoding implementation function.
     */
    const Fn fn () const { return _encoder; }
    
    /**
     * If the encoded type `T` matches the product representation of the given
     * type `C`, return a new `Encoder<C>' that will extract the `T` value from
     * instances of `C'.
     *
     * @tparam C The class type over which the returned Encoder will operate.
     */
    template <class C, typename R = ftl::Requires<pl::Product<C>{}>> Encoder<C> as () const {
        static_assert(std::is_same<T, typename pl::product<C>::P>::value, "`C` cannot be constructed with `T'");
        
        auto encode = this->fn();
        return Encoder<C> (
            [=](const C &value) {
                return encode(pl::product<C>::unapply(value));
            }
        );
    };
    
    /**
     * If the encoded type `T` matches the product representation of the given
     * type `C`, return a new `Encoder<C>' that will extract the `T` value from
     * instances of `C'.
     *
     * @tparam C The class type over which the returned Encoder will operate.
     */
    template <class C> typename std::enable_if<pl::is_record<C>::value, Encoder<C>>::type as () const {
        auto encode = this->fn();
        return Encoder<C> (
            [=](const C &value) {
                return encode(value.unapply());
            }
        );
    };
    
private:
    /** Encode implementation function */
    const Fn _encoder;
};

/* FTL concept implementation(s) */
namespace ftl {
    /**
     * Encoder functor implementation.
     */
    template<typename T> struct functor<Encoder<T>> {
        template<typename Fn, typename U = typename std::result_of<Fn(T)>::type>
        static constexpr Encoder<U> map(Fn &&fn, const Encoder<T> &encoder) {
            return Encoder<U>(
                [fn, encoder](const T &value) { return encoder.encode(fn(value)); }
            );
        }
        
        static constexpr bool instance = true;
    };
}