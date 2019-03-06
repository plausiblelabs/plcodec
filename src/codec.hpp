/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * This API is based on the design of Michael Pilquist and Paul Chiusano's
 * Scala scodec library: https://github.com/scodec/scodec/
 */

#pragma once

#include <functional>
#include <type_traits>
#include <limits>
#include <array>

#include <PLStdCPP/ftl/functional.h>
#include <PLStdCPP/ftl/type_functions.h>
#include <PLStdCPP/ftl/concepts/functor.h>
#include <PLStdCPP/unit_type.hpp>
#include <PLStdCPP/hlist.hpp>
#include <PLStdCPP/product_type.hpp>
#include <PLStdCPP/record_type.hpp>

#include <CoreFoundation/CoreFoundation.h>

#include "parse_error.hpp"
#include "decoder.hpp"
#include "encoder.hpp"

using namespace pl;

template<typename ... Ts> class TupledCodec;
template<typename T> class Codec;
template<typename L, typename ... Rs> constexpr TupledCodec<L, Rs...> operator&(const Codec<L> &lhs, const TupledCodec<Rs...> &rhs);

/**
 * @interface Codec
 *
 * Implements encoding and decoding of values of type `T`.
 *
 * @tparam T The value type coded by this instance.
 */
template<typename T>
class Codec {
public:
    /**
     * Construct a new instance with the given codec functions.
     *
     * @param encoding Encoding function.
     * @param decoding Decoding function.
     */
    Codec (const typename Encoder<T>::Fn &encoding, const typename Decoder<T>::Fn &decoding) : _encoder(encoding), _decoder(decoding) {}
    
    /**
     * Construct a new instance with the given coders.
     *
     * @param encoder Encoding implementation.
     * @param decoder Decoding implementation.
     */
    Codec (const Encoder<T> &encoder, const Decoder<T> &decoder) : _encoder(encoder), _decoder(decoder) {}
    
    /** Move constructor */
    Codec (Codec &&other) = default;
    
    /** Copy constructor */
    Codec (const Codec &other) = default;

    /**
     * Attempt to encode a value of type `T`.
     *
     * @param value Value to be encoded.
     */
    inline typename Encoder<T>::Result encode (const T &value) const { return _encoder.encode(value); }

    /**
     * Attempt to decode a value of type `T` from the given @a bytes.
     *
     * @param bytes Input data to be decoded.
     */
    inline typename Decoder<T>::Result decode (const ByteVector &bytes) const { return _decoder.decode(bytes); }
    
    /**
     * Return a borrowed reference to the backing encoder.
     */
    const Encoder<T> &encoder () const { return _encoder; };

    /**
     * Return a borrowed reference to the backing decoder.
     */
    const Decoder<T> &decoder () const { return _decoder; };
    
    /**
     * Return a new Codec that encodes/decodes a value of type `C' by
     * applying an isomorphism between `C' and `T'
     *
     * @tparam C The class type over which the returned Codec will operate.
     */
    template <class C> constexpr Codec<C> as () const {
        return Codec<C> (
            _encoder.template as<C>(),
            _decoder.template as<C>()
        );
    };
    
    /**
     * Given an isomorphism `T` <=> `U` described by the provided functions @a fnT and @a fnU,
     * transform this codec into a new Codec<U>.
     *
     * @tparam The type coded by the returned codec.
     * @param fnUT A function mapping `U` to `T`.
     * @param fnTU A function mapping `T` to `U`.
     */
    template <typename FnUT, typename FnTU, typename U = typename std::decay<decltype(std::declval<FnTU>()(std::declval<T>()))>::type> auto xmap (const FnUT &fnUT, const FnTU &fnTU) const -> Codec<U> {
        static_assert(std::is_same<T, decltype(fnUT(std::declval<U>()))>::value, "fnUT(U) and fnTU(T) do not operate on compatible values");
        
        const auto &encoder = _encoder;
        const auto &decoder = _decoder;
        return Codec<U> (
            [encoder, fnUT](const U &value) {
              return encoder.encode(fnUT(value));
            },
            [decoder, fnTU](const ByteVector &bv) {
                return [fnTU](const DecodeResult<T> &result) {
                    return DecodeResult<U>(fnTU(result.value()), result.remainder());
                } % decoder.decode(bv);
            }
        );
    }
    
private:
    /**
     * @internal
     *
     * Type inference support for Codec<T>::flatPrepend.
     *
     * This is necessary to guide C++'s type inferencer into calculating flatPrepend()'s interdependent
     * types in the correct order.
     *
     * Without this, clang fails to infer the element types in TupledCodec<T, Rs...>.
     *
     * @tparam Fn A function of the form `T -> TupledCodec<Rs...>`.
     */
    template <typename Fn> struct PrependTypeExtractor {
        /** The std::tuple<Rs...> type */
        using RightTupleType = decltype(
            ftl::fromRight(
                std::declval<Fn>()(std::declval<T>()).codec().decode(ByteVector::Empty)
            ).value()
        );

        /** The std::tuple<T, Rs...> type */
        using CombinedTupleType = decltype(std::tuple_cat(std::make_tuple<T>(std::declval<T>()), std::declval<RightTupleType>()));
    };

public:
    /**
     * Given a function `T -> TupledCodec<Rs...>', return a new Codec<std::tuple<T, Rs...>> that first
     * performs encoding/decoding of `T', using the resulting value to produce codecs for the remaining
     * `Rs...' types.
     *
     * This allows later parts of a codec to be dependent on earlier values.
     *
     * @param fn A function that uses the result of this codec to dynamically construct additional codec(s).
     */
    template <typename Fn> Codec<typename PrependTypeExtractor<Fn>::CombinedTupleType> flatPrepend (const Fn &fn) const {
        using Types = PrependTypeExtractor<Fn>;

        // TODO: If we can avoid this variable declaration, we'll be able to turn this back into a const expression.
        auto lhs = std::make_shared<Codec<T>>(_encoder, _decoder);
        return Codec<typename Types::CombinedTupleType>(
            [lhs, fn](const typename Types::CombinedTupleType &value) {
                const T &head = hlist::head(value);
                const auto rhs = fn(head);
                const auto tupled = (*lhs & rhs);
                return tupled.codec().encode(value);
            },
            [lhs, fn](const ByteVector &bv) {
                return lhs->decode(bv) >>= [fn](const DecodeResult<T> &lresult) {
                    const auto &rhs = fn(lresult.value());
                    
                    return [lresult, fn](const DecodeResult<typename Types::RightTupleType> &rresult) {
                        typename std::tuple<T> lvalue = std::make_tuple<T>(lresult.value());
                        typename Types::RightTupleType rvalue = rresult.value();
                        return DecodeResult<typename Types::CombinedTupleType>(std::tuple_cat(lvalue, rvalue), rresult.remainder());
                    } % rhs.codec().decode(lresult.remainder());
                };
            }
        );
    }


    /**
     * Given a function `T -> TupledCodec<Rs...>', return a new Codec<std::tuple<T, Rs...>> that first
     * performs encoding/decoding of `T', using the resulting value to produce codecs for the remaining
     * `Rs...' types.
     *
     * This operator is equivalent to flatPrepend().
     *
     * @param fn A function that uses the result of this codec to dynamically construct additional codec(s).
     */
    template <typename Fn> constexpr auto operator>>=(const Fn &fn) const -> decltype(flatPrepend(fn)) {
        return flatPrepend(fn);
    }

private:
    /** Backing encoder. */
    const Encoder<T> _encoder;
    
    /** Backing decoder. */
    const Decoder<T> _decoder;
};

/* Private TupledCodec implementation */
namespace tupled_codecs_impl {
    /**
     * Polymorphic function; maps Codec<V> or Encoder<V> values to
     * a unary encode() function.
     */
    static constexpr struct _encoder_fn {
        template <typename T> typename Encoder<T>::Fn constexpr operator() (const Codec<T> &codec) const { return codec.encoder().fn(); }
        template <typename T> typename Encoder<T>::Fn constexpr operator() (const Encoder<T> &encoder) const { return encoder.fn(); }
    } encoder_fn {};
    
    /**
     * Polymorphic function; maps Codec<V> or Decoder<V> values to
     * a unary decode() function.
     */
    static constexpr struct _decoder_fn {
        template <typename T> typename Decoder<T>::Fn constexpr operator() (const Codec<T> &codec) const { return codec.decoder().fn(); }
        template <typename T> typename Decoder<T>::Fn constexpr operator() (const Decoder<T> &decoder) const { return decoder.fn(); }
    } decoder_fn {};
    
    /**
     * Recursive tuple decoding.
     *
     * @tparam Ts Value types being decoded.
     */
    template <typename ... Ts> struct TupledDecoder {
        template <typename T> using DecodeFn = typename Decoder<T>::Fn;
        
        /** Calculate the remaining std::tuple<> values when starting from the given index */
        template <std::size_t ... Indices> using Remaining = decltype(hlist::select<Indices...>(std::declval<std::tuple<Ts...>>()));
        
        template <typename T> using Result = ftl::either<ParseError, DecodeResult<T>>;

        /*
         * Terminal state when performing recursive tuple decoding.
         */
        template <std::size_t ... Indices>
        static inline auto decode (const ByteVector &remainder, const std::tuple<DecodeFn<Ts>...> &decoders, const hlist::index_sequence<Indices...> &) ->
        Result<std::tuple<>>
        {
            return yield(DecodeResult<std::tuple<>>(std::tuple<>(), remainder));
        };

        /**
         * Non-terminal compile *AND* runtime-recursive tuple decoding.
         */
        template <std::size_t Idx, std::size_t ... Indices>
        static inline auto decode (const ByteVector &remainder, const std::tuple<DecodeFn<Ts>...> &decoders, const hlist::index_sequence<Idx, Indices...> &) ->
        Result<Remaining<Idx, Indices...>>
        {            
            /* Attempt to decode the next element */
            typedef std::tuple_element<Idx, std::tuple<Ts...>> V1;
            auto v1 = std::get<Idx>(decoders)(remainder);
            
            /* Flatmap and recurse */
            return v1 >>= [&](const ftl::Value_type<decltype(v1)> &result) {
                auto recursed = TupledDecoder::decode(result.remainder(), decoders, hlist::make_index_range<Idx+1, sizeof...(Indices)>());
                
                return [&result](const DecodeResult<Remaining<Indices...>> &next) {
                    auto ret = std::tuple_cat(std::make_tuple(result.value()), next.value());

                    return DecodeResult<decltype(ret)>(ret, next.remainder());
                } % recursed;
            };
        };
    };
}

/**
 * A codec over the product of types known at compile-time.
 *
 * @tparam Ts The value types coded by this instance.
 */
template <typename ... Ts> class TupledCodec {
private:
    /** Backing codecs */
    const std::tuple<Codec<Ts>...> _codecs;
    
    /* Internal implementation of codecs() */
    template <std::size_t ... Indices> Codec<std::tuple<Ts...>> codec (const hlist::index_sequence<Indices...> &) const {
        using namespace tupled_codecs_impl;
        
        /* Fetch the encoders */
        const auto encoders = hlist::map(_codecs, encoder_fn);
        
        /* Fetch the decoders */
        const auto decoders = hlist::map(_codecs, decoder_fn);
        
        /* Create our encoder lambda */
        typename Encoder<std::tuple<Ts...>>::Fn encoder = [encoders] (const std::tuple<Ts...> &values) {
            return hlist::sequence(hlist::apply(encoders, values));
        };
        
        /* Create our (recursive) decoder lambda. */
        typename Decoder<std::tuple<Ts...>>::Fn decoder = [decoders] (const ByteVector &bytes) -> typename Decoder<std::tuple<Ts...>>::Result {
            return TupledDecoder<Ts...>::template decode(bytes, decoders, hlist::make_index_range<0, sizeof...(Ts)>());
        };
        
        /* Return our new Codec */
        return Codec<std::tuple<Ts...>> (encoder, decoder);
    }

public:
    /**
     * Construct a new instance with the given tuple of codecs.
     *
     * @param codecs A tuple of codecs to be applied sequentially when performing encoding/decoding using
     * this TupledCodec.
     */
    TupledCodec (const std::tuple<Codec<Ts>...> &codecs) : _codecs(codecs) {}
    
    /**
     * Construct a new instance with the given codecs.
     *
     * @param codecs A tuple of codecs to be applied sequentially when performing encoding/decoding using
     * this TupledCodec.
     */
    TupledCodec (const Codec<Ts> &...codecs) : TupledCodec(std::make_tuple(codecs...)) {}
    
    /** Move constructor. */
    TupledCodec (TupledCodec &&other) = default;
    
    /** Copy constructor */
    TupledCodec (const TupledCodec &other) = default;

    /** Return a borrowed reference to the codecs comprising this TupledCodec. */
    inline const std::tuple<Codec<Ts>...> &codecs () const { return _codecs; }

    /**
     * Return a new TupledCodec containing this left operand's codecs, followed by the right operand's codecs.
     *
     * @param rhs The right operand.
     */
    template<typename ... Rs> TupledCodec<Ts ..., Rs ...> constexpr operator&(const TupledCodec<Rs...> &rhs) const {
        return TupledCodec(std::tuple_cat(_codecs, rhs.codecs()));
    }
    
    /**
     * Return a new TupledCodec with the provided Codec<R> appended to this TupledCodec.
     *
     * @param rhs The right operand.
     */
    template<typename R> TupledCodec<Ts ..., R> constexpr operator&(const Codec<R> &rhs) const {
        return TupledCodec<Ts..., R>(std::tuple_cat(_codecs, std::make_tuple(rhs)));
    }
    
    /**
     * Return the Codec<std::tuple<Ts...>> representation of this TupledCodec.
     */
    Codec<std::tuple<Ts...>> codec () const {
        return codec(hlist::make_index_sequence<sizeof...(Ts)>());
    }
    
    /**
     * Return a new Codec that encodes/decodes a value of type `C' by
     * applying an isomorphism between `C' and `T'
     *
     * @tparam C The class type over which the returned Codec will operate.
     */
    template <class C> constexpr Codec<C> as () const {
        return codec().template as<C>();
    };
};

/**
 * Lift a Codec<L>, Codec<R> pair to a TupledCodec<L, R>.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 *
 * @tparam L The first codec's type.
 * @tparam R The second codec's type.
 */
template <typename L, typename R> constexpr TupledCodec<L, R> operator&(const Codec<L> &lhs, const Codec<R> &rhs) {
    return TupledCodec<L, R>(lhs, rhs);
}

/**
 * Lift a Codec<L>, TupledCodec<Rs...> pair to a TupledCodec<L, Rs...>.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 *
 * @tparam L The first codec's type.
 * @tparam Rs The second codec's type.
 */
template <typename L, typename ... Rs> constexpr TupledCodec<L, Rs...> operator&(const Codec<L> &lhs, const TupledCodec<Rs...> &rhs) {
    return TupledCodec<L, Rs...>(std::tuple_cat(std::make_tuple(lhs), rhs.codecs()));
}

/**
 * Given a `Codec<Unit>` and a `TupledCodec<Rs...>`, return a new `TupledCodec<Rs...>` that first applies the `Codec<Unit>`, discarding
 * the @a lhs' Unit value when decoding.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 *
 * @tparam L The first codec's type.
 * @tparam Rs The second codec's type.
 */
template <typename ... Rs> constexpr TupledCodec<Rs...> operator>>(const Codec<pl::Unit> &lhs, const TupledCodec<Rs...> &rhs) {
    return lhs.flatPrepend([rhs](const pl::Unit &) {
        return rhs;
    });
}

/**
 * Given a `Codec<Unit>` and a `Codec<T>`, return a new `Codec<T>` that first applies the `Codec<Unit>`, discarding
 * the @a lhs' Unit value when decoding.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 *
 * @tparam L The first codec's type.
 * @tparam T The second codec's type.
 */
template <typename T> constexpr Codec<T> operator>>(const Codec<pl::Unit> &lhs, const Codec<T> &rhs) {
    return (lhs & rhs).codec().xmap(
        [](const T &value) { return std::make_tuple(unit, value); },
        [](const std::tuple<Unit, T> &value) { return std::get<1>(value); }
    );
}

namespace codecs {

    /** Byte order support. */
    struct ByteOrder {

#define DEF_NOSWAP(size) \
static inline uint##size##_t swap (uint##size##_t v) { return v; } \
static inline  int##size##_t swap ( int##size##_t v) { return v; }

#define DEF_SWAP(size) \
static inline uint##size##_t swap (uint##size##_t v) { return CFSwapInt##size(v); } \
static inline  int##size##_t swap ( int##size##_t v) { return CFSwapInt##size(v); }

        /** Native (unswapped) order. */
        struct Native {
            DEF_NOSWAP( 8)
            DEF_NOSWAP(16)
            DEF_NOSWAP(32)
            DEF_NOSWAP(64)
        };
        
        /** Swapped order. */
        struct Swapped {
            DEF_NOSWAP( 8)
            DEF_SWAP  (16)
            DEF_SWAP  (32)
            DEF_SWAP  (64)
        };

#undef DEF_NOSWAP
#undef DEF_SWAP

#if defined(__BIG_ENDIAN__)
        typedef Native Big;
        typedef Swapped Little;
#else
        typedef Swapped Big;
        typedef Native Little;
#endif
        
        template <typename N> using IsType = typename std::integral_constant<
            bool,
            std::is_same<N, Native>::value ||
            std::is_same<N, Swapped>::value
        >;
    };
    
    /**
     * Encode/decode codec over an integral type.
     *
     * @tparam T Integral codec type.
     * @tparam B Byte order.
     */
    template <typename T, class B> struct integral {
    public:
        static_assert(std::is_integral<T>::value, "`T' is not an integral type");
        static_assert(ByteOrder::IsType<B>::value, "`B' is not a valid ByteOrder instance");
        
        /** Encoding implementation */
        static typename Encoder<T>::Result encode (const T &value) {
            return yield(ByteVector::Value(B::swap(value)));
        };
        
        /** Decoding implementation */
        static typename Decoder<T>::Result decode (const ByteVector &bv) {
            return [](const std::tuple<T, ByteVector> &next) {
                return DecodeResult<T>(
                    B::swap(std::get<0>(next)),
                    std::get<1>(next)
                );
            } % bv.read<T>();
        }
        
        /** Codec for this integral type */
        static const Codec<T> codec;
    };
    
    /** Codec instance vended by an integral<T> */
    template<typename T, typename B> const Codec<T> integral<T, B>::codec(integral<T, B>::encode, integral<T, B>::decode);
    
    /** Signed 8-bit integer codec. */
    static const Codec<int8_t> int8 = integral<int8_t, ByteOrder::Native>::codec;
    
    /** Unsigned 8-bit integer codec. */
    static const Codec<uint8_t> uint8 = integral<uint8_t, ByteOrder::Native>::codec;

    /** Big-endian, signed 16-bit integer codec. */
    static const Codec<int16_t> int16 = integral<int16_t, ByteOrder::Big>::codec;
    
    /** Big-endian, unsigned 16-bit integer codec. */
    static const Codec<uint16_t> uint16 = integral<uint16_t, ByteOrder::Big>::codec;
    
    /** Big-endian, signed 32-bit integer codec. */
    static const Codec<int32_t> int32 = integral<int32_t, ByteOrder::Big>::codec;
    
    /** Big-endian, unsigned 32-bit integer codec. */
    static const Codec<uint32_t> uint32 = integral<uint32_t, ByteOrder::Big>::codec;
    
    /** Big-endian, signed 64-bit integer codec. */
    static const Codec<int64_t> int64 = integral<int64_t, ByteOrder::Big>::codec;
    
    /** Big-endian, unsigned 64-bit integer codec. */
    static const Codec<uint64_t> uint64 = integral<uint64_t, ByteOrder::Big>::codec;

    /** Little-endian, signed 16-bit integer codec. */
    static const Codec<int16_t> int16L = integral<int16_t, ByteOrder::Little>::codec;
    
    /** Little-endian, unsigned 16-bit integer codec. */
    static const Codec<uint16_t> uint16L = integral<uint16_t, ByteOrder::Little>::codec;
    
    /** Little-endian, signed 32-bit integer codec. */
    static const Codec<int32_t> int32L = integral<int32_t, ByteOrder::Little>::codec;
    
    /** Little-endian, unsigned 32-bit integer codec. */
    static const Codec<uint32_t> uint32L = integral<uint32_t, ByteOrder::Little>::codec;
    
    /** Little-endian, signed 64-bit integer codec. */
    static const Codec<int64_t> int64L = integral<int64_t, ByteOrder::Little>::codec;
    
    /** Little-endian, unsigned 64-bit integer codec. */
    static const Codec<uint64_t> uint64L = integral<uint64_t, ByteOrder::Little>::codec;

    
    /**
     * Identity byte vector codec.
     *
     * Encodes by returning the given byte vector.
     *
     * Decodes by taking all remaining bytes from the given byte vector.
     *
     * @param length The number of bytes that are available to the underlying codec.
     * @param codec The underlying codec.
     * @tparam T Underlying codec type.
     */
    static const Codec<ByteVector> identityBytes = Codec<ByteVector>(
        [](const ByteVector &value) {
            return yield(value);
        },
         
        [](const ByteVector &bv) {
            return yield(DecodeResult<ByteVector>(bv, ByteVector::Empty));
        }
    );

    /**
     * Codec that limits the number of bytes that are available to the given codec.
     *
     * When encoding, if the given codec encodes fewer than `length` bytes, the byte vector
     * is right padded with low bytes.  If the given codec instead encodes more than `length`
     * bytes, an error is returned.
     *
     * When decoding, the given codec is only given `length` bytes.  If the given codec does
     * not consume all `length` bytes, any remaining bytes are discarded.
     *
     * @param length The number of bytes that are available to the underlying codec.
     * @param codec The underlying codec.
     * @tparam T Underlying codec type.
     */
    template <typename T> inline Codec<T> fixedSizeBytes (const std::size_t &length, const Codec<T> &codec) {
        return Codec<T>(
            [codec, length](const T &value) {
                return codec.encode(value) >>= [length](const ByteVector &encoded) {
                    if (encoded.length() > length) {
                        return fail<ByteVector>("Encoding requires " + std::to_string(encoded.length()) + " bytes but codec is limited to fixed length of " + std::to_string(length) + " bytes");
                    } else {
                        return encoded.padRight(length);
                    }
                };
            },
        
            [codec, length](const ByteVector &bv) {
                /* Give `length` bytes to the decoder; if successful, return the result along with the remainder of `bv` after dropping `length` bytes from it */
                return [bv, length](const DecodeResult<T> &result) {
                    return DecodeResult<T>(result.value(), ftl::fromRight(bv.drop(length)));
                } % (bv.take(length) >>= [codec](const ByteVector &taken) {
                    return codec.decode(taken);
                });
            }
        );
    }
    
    /**
     * Byte vector codec.
     *
     * Encodes by returning the given byte vector if its length is `length` bytes, otherwise returns an error.
     *
     * Decodes by taking `length` bytes from the given byte vector.
     *
     * @param length The number of bytes to encode/decode.
     */
    inline Codec<ByteVector> bytes (const std::size_t &length) {
        return fixedSizeBytes(length, codecs::identityBytes);
    }
    
    /**
     * Given a Codec<ByteVector>, return a new codec that encodes/decodes fully realized byte array values.
     *
     * @param byteCodec A codec that encodes/decodes a potentially lazy-evaluated ByteVector value.
     *
     * @return A new Codec that performs a fully realized read of the backing ByteVector when decoding, and efficiently
     * converts std::vector<uint8_t> values to a ByteVector on encoding.
     */
    inline Codec<std::vector<uint8_t>> eager (const Codec<ByteVector> &byteCodec) {
        using T = std::vector<uint8_t>;
        
        return Codec<T> (
            [byteCodec](const std::vector<uint8_t> &bytes) -> typename Encoder<T>::Result {
                return byteCodec.encode(ByteVector::fromVector(bytes));
            },
            [byteCodec](const ByteVector &input) -> typename Decoder<T>::Result {
                return byteCodec.decode(input) >>= [](const DecodeResult<ByteVector> &result) -> typename Decoder<T>::Result {
                    return [result](const std::vector<uint8_t> &vec) {
                        return DecodeResult<T>(vec, result.remainder());
                    } % result.value().toVector();
                };
            }
        );
    }
    
    /**
     * Codec for length-delimited values.
     *
     * @param lengthCodec The codec the encodes/decodes the length (in bytes) of the value.
     * @param valueCodec The codec for the value.
     * @tparam LT Type of the length codec.
     * @tparam VT Type of the value codec.
     */
    template <typename LT, typename VT> inline Codec<VT> variableSizeBytes (const Codec<LT> &lengthCodec, const Codec<VT> &valueCodec) {
        static_assert(std::is_integral<LT>::value, "Not an integral type");
        static_assert(!std::is_signed<LT>::value, "Not an unsigned type");

        return Codec<VT>(
            [=](const VT &value) {
                /* Encode the value, then prepend the length of the encoded value */
                return valueCodec.encode(value) >>= [=](const ByteVector &encodedValue) {
                    /* Fail if length is too long to be encoded */
                    auto maxLength = (std::size_t)std::numeric_limits<LT>::max();
                    if (encodedValue.length() > maxLength) {
                        return fail<ByteVector>("Length of encoded value (" + std::to_string(encodedValue.length()) + " bytes) is greater than maximum value (" + std::to_string(maxLength) + ") of length type");
                    } else {
                        return lengthCodec.encode((LT)encodedValue.length()) >>= [=](const ByteVector &encodedLength) {
                            return yield(encodedLength.append(encodedValue));
                        };
                    }
                };
            },
                        
            [=](const ByteVector &bv) {
                /* Decode the length, then decode the value */
                return lengthCodec.decode(bv) >>= [=](const DecodeResult<LT> &length) {
                    auto fixedCodec = fixedSizeBytes((std::size_t)length.value(), valueCodec);
                    return fixedCodec.decode(length.remainder());
                };
            }
        );
    }
    
    /**
     * Codec that encodes `length` low bytes and decodes by discarding `length` bytes.
     *
     * @param length The number of bytes to ignore.
     */
    inline Codec<Unit> ignore (const std::size_t &length) {
        return Codec<Unit>(
            [length](const Unit &value) {
                return yield(ByteVector::Memset(0, length));
            },
            
            [length](const ByteVector &bv) {
                return bv.drop(length) >>= [](const ByteVector &remainder) {
                    return yield(DecodeResult<Unit>(unit, remainder));
                };
            }
        );
    }
    
    /**
     * Codec that always encodes the given byte vector, and decodes by returning a unit result if the actual bytes match
     * the given byte vector or an error otherwise.
     *
     * @param bytes The constant bytes.
     */
    inline Codec<Unit> constant (const ByteVector &bytes) {
        return Codec<Unit>(
            [bytes](const Unit &value) {
                return yield(bytes);
            },
                          
            [bytes](const ByteVector &bv) {
                return bv.take(bytes.length()) >>= [bytes, bv](const ByteVector &taken) {
                    if (taken == bytes) {
                        return yield(DecodeResult<Unit>(unit, ftl::fromRight(bv.drop(bytes.length()))));
                    } else {
                        return fail<DecodeResult<Unit>>("Expected constant " + bytes.toString() + " but got " + taken.toString());
                    }
                };
            }
        );
    }
    
    /**
     * Operator that creates a new codec that wraps the given codec while supplying additional context (e.g. in error messages).
     */
    template<typename T> Codec<T> operator |(const std::string &context, const Codec<T> &codec) {
        return Codec<T>(
            [context, codec](const T &value) {
                return left_map([context](const ParseError &e) { return e.pushContext(context); }, codec.encode(value));
            },
                        
            [context, codec](const ByteVector &bv) {
                return left_map([context](const ParseError &e) { return e.pushContext(context); }, codec.decode(bv));
            }
        );
    }
}

/* FTL concept implementation(s) */
namespace ftl {
    /**
     * Codec functor implementation.
     */
    template<typename T> struct functor<Codec<T>> {
        template<typename Fn, typename U = typename std::result_of<Fn(T)>::type>
        static constexpr Codec<U> map(Fn &&fn, const Codec<T> &codec) {
            return Codec<U>(
                fn % codec.encoder,
                fn % codec.decoder
            );
        }
        
        static constexpr bool instance = true;
    };
}
