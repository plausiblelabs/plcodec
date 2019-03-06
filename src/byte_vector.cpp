/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#include "byte_vector.hpp"

/** An empty byte vector. */
ByteVector ByteVector::Empty(bytevector::make_sum<ByteVector::VType>(bytevector::Empty {}));

// Documented on declaration
std::string ByteVector::description () const {
    std::stringstream stream;
    bytevector::describe(_storage, stream, 0);
    return stream.str();
}

/** ByteVector equality operator */
bool operator ==(const ByteVector &lhs, const ByteVector &rhs) {
    const size_t l = lhs.length();
    if (l != rhs.length())
        return false;
    
    /* TODO: If equality winds up being a bottleneck, we can perform much more efficient comparisons. */
    for (size_t i = 0; i < l; i++) {
        if (lhs[i] != rhs[i])
            return false;
    }
    
    return true;
}
