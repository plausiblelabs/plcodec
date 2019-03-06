/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#include "byte_vector.hpp"

#import <objc/runtime.h>

/**
 * Determine whether dispatch_data_t is an NSData. If this function returns YES, then it is
 * safe to take a dispatch_data_t and cast it to NSData* and use the result as an NSData.
 * Otherwise this will not work and you must not convert objects in this way. This function
 * will return YES on 10.9+ and iOS 7+.
 */
static BOOL PLDispatchDataIsNSData(void) {
    // There are three cases to consider:
    // 1. dispatch_data_t is not an Objective-C object.
    // 2. dispatch_data_t is an Objective-C object, but not an NSData subclass.
    // 3. dispatch_data_t is an NSData subclass.
    //
    // On 10.7, case #1 is true. On 10.8, case #2 is true. On 10.9 and beyond, case #3 is true.
    // It is part of the API contract as of 10.9 so we can trust that all future versions will
    // be case #3.
    //
    // We assume that in the case of #1, dispatch_data_t is still a valid pointer, and furthermore
    // that object_getClass can be called on it and return *something* that doesn't look like an
    // Objective-C class. These assumptions hold on 10.7, and case #1 doesn't apply beyond 10.7,
    // so these are safe assumptions to rely on.
    //
    // This code performs two checks. First, it checks to see if dispatch_data_t is an Objective-C
    // object at all. It does this by calling object_getClass on it, and then scanning for that class
    // in the runtime's class list. If no such class exists, it's not an Objective-C object. If it
    // is an Objective-C object, the second check can be performed, which is to see if it's an NSData
    // or not. If it's an Objective-C object and an NSData, the function returns YES.
    
    static BOOL answer;
    static dispatch_once_t pred;
    dispatch_once(&pred, ^{
        dispatch_data_t dummyData = dispatch_data_create("a", 1, nil, nil);
        void *maybeClass = object_getClass((id)dummyData);
        
        BOOL isObjC = NO;
        
        void **classList = (void **)objc_copyClassList(NULL);
        for (void **cursor = classList; *cursor != NULL; cursor++) {
            if (maybeClass == *cursor) {
                isObjC = YES;
                break;
            }
        }
        free(classList);
        
        answer = (isObjC && [(id)dummyData isKindOfClass: [NSData class]]);
    });
    return answer;
}

ftl::either<ParseError, pl::ns_ptr<NSData *>> ByteVector::toNSData () const {
    if (PLDispatchDataIsNSData()) {
        dispatch_data_t data = NULL;
        
        auto result = iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
            dispatch_data_t localData = dispatch_data_create(ptr, len, dispatch_get_global_queue(0, 0), ^{ destructor(); });
            if (data == NULL) {
                data = localData;
            } else {
                dispatch_data_t concatenated = dispatch_data_create_concat(data, localData);
                dispatch_release(data);
                dispatch_release(localData);
                data = concatenated;
            }
        }, /*provideLongTermBytes=*/true);
        
        if (result.is<ftl::Left<ParseError>>()) {
            if (data != NULL) {
                dispatch_release(data);
            }
            return ftl::make_left<pl::ns_ptr<NSData *>>(ftl::fromLeft(result));
        }
        
        pl::ns_ptr<NSData *>returnValue;
        if (data != NULL) {
            returnValue = pl::make_ns_ptr((NSData *)data);
            dispatch_release(data);
        } else {
            returnValue = pl::make_ns_ptr([NSData data]);
        }
        return yield(returnValue);
    } else {
        NSMutableData *data = [NSMutableData dataWithCapacity: length()];
        
        auto result = iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
            [data appendBytes: ptr length: len];
        }, /*provideLongTermBytes=*/false);
        
        if (result.is<ftl::Left<ParseError>>()) {
            return ftl::make_left<pl::ns_ptr<NSData *>>(ftl::fromLeft(result));
        }
        
        return yield(pl::make_ns_ptr((NSData *)data));
    }
}

const ByteVector ByteVector::fromNSData(NSData *data) {
    NSData *copy = [data copy];
    
    auto bytes = (uint8_t *)[copy bytes];
    auto length = [copy length];
    auto destructor = [=](){ [copy release]; };
    
    return ByteVector(VType(ftl::constructor<bytevector::Buffered>(), bytevector::Buffered(bytes, length, destructor)));
}


