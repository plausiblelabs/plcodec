/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * This API is based on the design of Michael Pilquist and Paul Chiusano's
 * Scala scodec library: https://github.com/scodec/scodec/
 */

#pragma once

#include "byte_vector.hpp"

#include <type_traits>
#include <PLStdCPP/ftl/concepts/functor.h>

/* Forward declarations */
template <typename T> struct DecodeResult;

namespace ftl {
    template<typename T> struct functor<DecodeResult<T>>;
}

/**
 * A result type, consisting of a decoded value and any unconsumed data, returned by Decoder operations.
 *
 * @tparam T The decoded value.
 */
template <typename T>
struct DecodeResult {
public:
    /**
     * Construct a new instance.
     *
     * @param value The decoded value.
     * @param remainder Any unconsumed data.
     */
    DecodeResult (const T &value, const ByteVector &remainder) : _value(value), _remainder(remainder) {}

    /** Return the decoded value. */
    T value () const { return _value; }
    
    /** Return a borrowed reference to the unconsumed data. */
    const ByteVector& remainder () const { return _remainder; }
    
    /**
     * Map over this result's remainder.
     */
    template<typename Fn> const DecodeResult<T> mapRemainder (Fn fn) const {
        return DecodeResult(_value, fn(_remainder));
    }

    friend struct ftl::functor<DecodeResult<T>>;

private:
    const T _value;
    const ByteVector _remainder;
};


/* FTL concept implementation(s) */
namespace ftl {
    /** 
     * DecodeResult functor implementation.
     */
    template<typename T> struct functor<DecodeResult<T>> {
        // from functor
        template<typename Fn, typename U = typename std::result_of<Fn(T)>::type>
        static DecodeResult<U> map(Fn &&fn, const DecodeResult<T> &f) {
            return DecodeResult<U>(fn(f._value), f._remainder);
        }
        
        template<typename Fn, typename U = typename std::result_of<Fn(T)>::type>
        static DecodeResult<U> map(Fn&& fn, DecodeResult<T>&& f) {
            return DecodeResult<U>(fn(std::move(f._value)), std::move(f._remainder));
        }
    
        static constexpr bool instance = true;
    };
}