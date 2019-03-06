/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#pragma once

#import <Foundation/Foundation.h>

#include <PLStdCPP/ftl/maybe.h>
#include <PLObjCPP/ns_ptr.hpp>

#include "parse_error.hpp"
#include "codec.hpp"

#include <stdint.h>
#include <string>
#include <vector>

namespace pl {
    
    /** Options to determine whether to include unrecognized extra keys that were in the original property list. */
    enum PropertyListExtraKeysOptions {
        /** When creating a new property list, include unknown extra keys that were in the original property list. */
        PropertyListIncludeExtraKeys,
        
        /** When creating a new property list, don't include any unknown extra keys from the original property list. */
        PropertyListNoExtraKeys
    };
    
    /**
     * Check if a type conforms to the `plist_record` structural type requirements:
     *
     * - A static from_plist() unary method must accept a NSDictionary* and return an instance of `ftl::either<ParseError, R>`.
     * - The non-static to_plist() method must return an instance of NSDictionary* that may be
     *   passed to from_plist().
     */
    template<class R, typename=void> struct is_plist_record : std::false_type {}; \
    template<class R> struct is_plist_record<R, typename std::enable_if<
        std::is_assignable<
            ftl::either<ParseError, R>,
            decltype(R::from_plist(std::declval<R>().to_plist(PropertyListIncludeExtraKeys)))
        >::value &&
        std::is_assignable<
            ftl::either<ParseError, R>,
            decltype(R::from_plist(std::declval<NSDictionary*>()))
        >::value
    >::type> : std::true_type {};

    #define _PL_PLIST_EXTRACT_FIELD_TEMPL_n(type, name, ...) \
        auto name = pl::PropertyListType<type>::from_plist(@#name, plist[@#name]); \
        if (name.template is<ftl::Left<ParseError>>()) { \
            return ftl::make_left<_plist_record_type>(ftl::fromLeft(name)); \
        } \
        [mutablePlist removeObjectForKey: @#name];
    #define _PL_PLIST_EXTRACT_FIELD_TEMPL_1(type, name, ...) _PL_PLIST_EXTRACT_FIELD_TEMPL_n(type, name, ...)

    #define _PL_PLIST_PARSED_VALUE_TEMPL_n(type, name, ...) ftl::fromRight(name),
    #define _PL_PLIST_PARSED_VALUE_TEMPL_1(type, name, ...) ftl::fromRight(name)

    #define _PL_PLIST_INSERT_FIELD_TEMPL_n(type, name, ...) { \
        id _plist_value = pl::PropertyListType<_PL_RECORD_UNPAREN(type)>::to_plist(@#name, _ ## name, extraKeysOptions); \
        if (_plist_value != nil) \
            plist[@#name] = _plist_value; \
    };
    #define _PL_PLIST_INSERT_FIELD_TEMPL_1(type, name, ...) _PL_PLIST_INSERT_FIELD_TEMPL_n(type, name, __VA_ARGS__)
    
    /**
     * Specialize this struct to provide conversions from a C++ type `T` to and from a corresponding property list type.
     *
     * Two methods must be defined by specializations:
     *
     * @code
     * static ftl::either<ParseError, T> from_plist (NSString *name, id plist);
     * static id to_plist (NSString *name, const T &value, PropertyListExtraKeysOptions extraKeysOptions);
     * @endcode
     */
    template <typename T> struct PropertyListType {
        /* Default `from_plist` implementation for types conforming to the plist_record structural type. */
        static typename std::enable_if<is_plist_record<T>::value, ftl::either<ParseError, T>>::type from_plist (NSString *name, id plist) { return T::from_plist(plist); }
        
        /* Default `to_plist` implementation for types conforming to the plist_record structural type. */
        static typename std::enable_if<is_plist_record<T>::value, id>::type to_plist (NSString *name, const T &value, PropertyListExtraKeysOptions extraKeysOptions) { return value.to_plist(extraKeysOptions); }
    };
    
    /**
     * @ingroup record
     *
     * Define an appropriate constructor, accessors, `product` support, equality operators, and private member
     * variables required to perform automatic parsing/serialization of a property list type.
     *
     * This provides a superset of the functionality of PL_RECORD_STRUCT, with the addition of from_plist() and to_plist()
     * methods that support serialization to/from property list types, and a codec () method providing a record-encoding
     * codec instance.
     *
     * Each field is encoded/decoded as a dictionary key with the field name as the key name, and the dictionary value
     * type based on the field type.
     *
     * @sa PL_PLIST_STRUCT
     */
    #define PL_PLIST_FIELDS(name, ...) \
        public: \
        static ftl::either<ParseError, name> from_plist(NSDictionary *plist) { \
            using _plist_record_type = name; \
            NSMutableDictionary *mutablePlist = [plist mutableCopy]; \
            _PL_RECORD_ITERATE_TEMPLATE(_PL_PLIST_EXTRACT_FIELD_TEMPL, __VA_ARGS__); \
            auto result = name::apply(std::make_tuple(_PL_RECORD_ITERATE_TEMPLATE(_PL_PLIST_PARSED_VALUE_TEMPL, __VA_ARGS__))); \
            result.pl_plist_extra_keys = mutablePlist; \
            return ftl::make_right<ParseError>(result); \
        } \
        \
        NSDictionary *to_plist(PropertyListExtraKeysOptions extraKeysOptions = PropertyListNoExtraKeys) const { \
            NSMutableDictionary *plist = [NSMutableDictionary dictionary]; \
            if (extraKeysOptions == PropertyListIncludeExtraKeys) { \
                [plist addEntriesFromDictionary: pl_plist_extra_keys.get()]; \
            }\
            _PL_RECORD_ITERATE_TEMPLATE(_PL_PLIST_INSERT_FIELD_TEMPL, __VA_ARGS__) \
            return plist; \
        } \
        \
        PL_RECORD_FIELDS(name, __VA_ARGS__); \
        private: \
            ns_ptr<NSDictionary *> pl_plist_extra_keys;
    
    /**
     * Define a property list record type. 
     *
     * This provides a superset of the functionality of PL_RECORD_STRUCT, with the addition of from_plist() and to_plist()
     * methods that support serialization to/from property list types. Each field is encoded/decoded as a dictionary key
     * with the field name as the key name, and the dictionary value type based on the field type.
     *
     * Each property list type conversion must be specified as a specialization of the PropertyListType type.
     * The PLIST_CONVERT_IMPL macros handle the boilerplate of doing that. If you get errors about not finding an
     * override of those functions, define new ones for your field type below.
     */
    #define PL_PLIST_STRUCT(name, ...) struct name { \
        PL_PLIST_FIELDS(name, __VA_ARGS__); \
    };

    /**
     * These two functions convert a type to an ftl::either<ParseError, ...>, where the type
     * is allowed to already be the appropriate either type. This allows code that always
     * succeeds to return a raw type, and code that can fail to return an either type.
     */
    template<typename T> static inline ftl::either<ParseError, T> ToEither(const T &value) {
        return ftl::make_right<ParseError>(value);
    }

    template<typename T> static inline ftl::either<ParseError, T> ToEither(ftl::either<ParseError, T> value) {
        return value;
    }

    /**
     * Create a pair of plist conversion functions between a C++ type (cpptype) and an ObjC plist
     * type (nstype). The expression ns2cppcode takes the ObjC type and returns the C++ type
     * (and can optionally be an ftl::either<ParseError, cpptype> if it can fail), and the
     * expression cpp2nscode takes the C++ type and returns the ObjC type. The tmpl parameter is
     * an optional template modifier for templated conversion functions.
     */
    #define PLIST_CONVERT_IMPL(cpptype, nstype, ns2cppcode, cpp2nscode) \
        template<> struct PropertyListType<cpptype> { \
            static inline ftl::either<ParseError, cpptype> from_plist (NSString *name, id value) { \
                if ([value isKindOfClass: [nstype class]]) { \
                    return ToEither<cpptype>(ns2cppcode); \
                } else {\
                    return ftl::make_left<cpptype>(ParseError([[NSString stringWithFormat: @"For %@: expected type %@, got %@", name, [nstype class], [value classForCoder]] UTF8String])); \
                } \
            } \
            static inline id to_plist(NSString *name, const cpptype &value, PropertyListExtraKeysOptions extraKeysOptions) { return cpp2nscode; } \
            static inline id to_plist(NSString *name, cpptype &&value, PropertyListExtraKeysOptions extraKeysOptions) { return cpp2nscode; } \
        }

    /* Define type conversions for simple C/C++ types. */
    PLIST_CONVERT_IMPL(std::string,     NSString,       std::string([value UTF8String]),    [NSString stringWithUTF8String: value.c_str()]);
    PLIST_CONVERT_IMPL(char,            NSNumber,       [value charValue],                  @(value));
    PLIST_CONVERT_IMPL(short,           NSNumber,       [value shortValue],                 @(value));
    PLIST_CONVERT_IMPL(int,             NSNumber,       [value intValue],                   @(value));
    PLIST_CONVERT_IMPL(long int,        NSNumber,       [value longValue],                  @(value));
    PLIST_CONVERT_IMPL(long long int,   NSNumber,       [value longLongValue],              @(value));
    
    PLIST_CONVERT_IMPL(unsigned char,   NSNumber,       [value unsignedCharValue],          @(value));
    PLIST_CONVERT_IMPL(unsigned short,  NSNumber,       [value unsignedShortValue],         @(value));
    PLIST_CONVERT_IMPL(unsigned int,    NSNumber,       [value unsignedIntValue],           @(value));
    PLIST_CONVERT_IMPL(unsigned long,   NSNumber,       [value unsignedLongValue],          @(value));
    PLIST_CONVERT_IMPL(unsigned long long, NSNumber,    [value unsignedLongLongValue],      @(value));
    
    PLIST_CONVERT_IMPL(bool,            NSNumber,       [value boolValue],                  @(value));
    PLIST_CONVERT_IMPL(float,           NSNumber,       [value floatValue],                 @(value));
    PLIST_CONVERT_IMPL(double,          NSNumber,       [value doubleValue],                @(value));
    
    /* Define direct type conversions for Objective-C property list types */
    PLIST_CONVERT_IMPL(NSString*,       NSString,       value,                              value);
    PLIST_CONVERT_IMPL(NSData*,         NSData,         value,                              value);
    PLIST_CONVERT_IMPL(NSDate*,         NSDate,         value,                              value);
    PLIST_CONVERT_IMPL(NSDictionary*,   NSDictionary,   value,                              value);
    PLIST_CONVERT_IMPL(NSArray*,        NSArray,        value,                              value);
    PLIST_CONVERT_IMPL(NSNumber*,       NSNumber,       value,                              value);
    
    /* Define direct type conversions for CoreFoundation property list types */
    PLIST_CONVERT_IMPL(CFStringRef,     NSString,       ((__bridge CFStringRef) value),     ((__bridge NSString *) value));
    PLIST_CONVERT_IMPL(CFDataRef,       NSData,         ((__bridge CFDataRef) value),       ((__bridge NSData *) value));
    PLIST_CONVERT_IMPL(CFDateRef,       NSDate,         ((__bridge CFDateRef) value),       ((__bridge NSDate *) value));
    PLIST_CONVERT_IMPL(CFDictionaryRef, NSDictionary,   ((__bridge CFDictionaryRef) value), ((__bridge NSDictionary *) value));
    PLIST_CONVERT_IMPL(CFArrayRef,      NSArray,        ((__bridge CFArrayRef) value),      ((__bridge NSArray *) value));
    PLIST_CONVERT_IMPL(CFNumberRef,     NSNumber,       ((__bridge CFNumberRef) value),     ((__bridge NSNumber *) value));
    
    /**
     * Property list type conversions for std::shared_ptr-wrapped values.
     */
    template<typename T> struct PropertyListType<std::shared_ptr<T>> {
        // from PropertyListType
        static ftl::either<ParseError, std::shared_ptr<T>> from_plist (NSString *name, id value) {
            // TODO - is there a sane way to avoid copying around T and instead construct the shared_ptr in place?
            return [](const T &value) { return std::make_shared<T>(value); } % PropertyListType<T>::from_plist(name, value);
        }
        
        // from PropertyListType
        static id to_plist (NSString *name, const std::shared_ptr<T> &value, PropertyListExtraKeysOptions extraKeysOptions) {
            return PropertyListType<T>::to_plist(name, *value, extraKeysOptions);
        }
    };

    /**
     * Property list type conversions for ns_ptr-wrapped values.
     */
    template<typename T> struct PropertyListType<ns_ptr<T>> {
        // from PropertyListType
        static ftl::either<ParseError, ns_ptr<T>> from_plist (NSString *name, id value) {
            return make_ns_ptr<T> % PropertyListType<T>::from_plist(name, value);
        }

        // from PropertyListType
        static id to_plist (NSString *name, const ns_ptr<T> &value, PropertyListExtraKeysOptions extraKeysOptions) {
            return PropertyListType<T>::to_plist(name, value.get(), extraKeysOptions);
        }
    };
    
    /**
     * Property list type conversions for optional values via ftl::maybe.
     */
    template<typename T> struct PropertyListType<ftl::maybe<T>> {
        // from PropertyListType
        static ftl::either<ParseError, ftl::maybe<T>> from_plist (NSString *name, id value) {
            if (value == nil) {
                return ftl::make_right<ParseError>(ftl::nothing<T>());
            } else {
                return ftl::just % PropertyListType<T>::from_plist(name, value);
            }
        }
        
        // from PropertyListType
        static id to_plist (NSString *name, const ftl::maybe<T> &value, PropertyListExtraKeysOptions extraKeysOptions) {
            return value.match(
                [&name, &extraKeysOptions](const T &v) { return PropertyListType<T>::to_plist(name, v, extraKeysOptions); },
                [](const ftl::Nothing &) { return nil; }
            );
        }
    };

    /**
     * Property list type conversions for std::vector.
     */
    template<typename T> struct PropertyListType<std::vector<T>> {
        // from PropertyListType
        static ftl::either<ParseError, std::vector<T>> from_plist (NSString *name, NSArray *array) {
            std::vector<T> out;
            for (id value in array) {
                auto result = PropertyListType<T>::from_plist(name, value);
                
                if (result.template is<ftl::Left<ParseError>>()) {
                    return ftl::make_left<std::vector<T>>(ftl::fromLeft(result));
                }
                out.push_back(ftl::fromRight(result));
            }
            return ftl::make_right<ParseError>(out);
        }
        
        
        // from PropertyListType
        static NSArray *to_plist (NSString *name, const std::vector<T> &array, PropertyListExtraKeysOptions extraKeysOptions) {
            NSMutableArray *out = [NSMutableArray arrayWithCapacity: array.size()];
            for (auto &obj : array) {
                [out addObject: PropertyListType<T>::to_plist(name, obj, extraKeysOptions)];
            }
            return out;
        }
    };
    
    /**
     * Codec over a property list record type.
     *
     * @tparam T Property list record type.
     */
    template <typename T> Codec<T> plist_record_codec (const NSPropertyListFormat format, PropertyListExtraKeysOptions extraKeysOptions = PropertyListNoExtraKeys) {
        static_assert(is_plist_record<T>::value, "`T' is not a property list record type");
        
        return Codec<T>(
            [format, extraKeysOptions](const T &record) {
                NSError *error = nil;
                
                auto dict = record.to_plist(extraKeysOptions);
                NSData *data = [NSPropertyListSerialization dataWithPropertyList: dict format: format options: 0 error: &error];
                
                if (data == nil) {
                    return fail<ByteVector>([[error description] UTF8String]);
                }
                return yield(ByteVector::fromNSData(data));
            },
            [format](const ByteVector &bv) {
                return bv.toNSData() >>= [format](const pl::ns_ptr<NSData *> data) {
                    NSError *error = nil;
                    id plist = [NSPropertyListSerialization propertyListWithData: *data options: 0 format: nullptr error: &error];
                    
                    if (plist == nil) {
                        return fail<DecodeResult<T>>([[error description] UTF8String]);
                    } else if (![plist isKindOfClass: [NSDictionary class]]) {
                        return fail<DecodeResult<T>>(std::string("Expected NSDictionary when decoding plist, but got ") + [[plist classForCoder] description].UTF8String);
                    }
                    
                    auto nsplist = ns_ptr<NSDictionary *>(plist);
                    return [](const T &result) {
                        return DecodeResult<T>(result, ByteVector::Empty);
                    } % T::from_plist(plist);
                };
            }
        );
    };
    
} /* namespace pl */
