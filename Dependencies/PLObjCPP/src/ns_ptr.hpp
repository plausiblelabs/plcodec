/*
 * Author: Landon Fuller <landonf@plausible.coop>
 *
 * Copyright (c) 2013-2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include <type_traits>
#include <functional>

extern "C" id objc_retainAutoreleaseReturnValue (id obj);

namespace pl {

/**
 * A minimal wrapper around CoreFoundation reference counted objects; manages automatic
 * retaining and releasing of references to the backing CoreFoundation instance.
 */
template<class T> class ns_ptr {
    static_assert(std::is_convertible<id, T>::value, "ns_ptr<T> only supports Objective-C compatible types.");

private:
    using IDType = typename std::remove_pointer<id>::type;
    
    /**
     * The backing CF (or Objective-C) value. Values are always cast to CFTypeRef for internal storage; this
     * avoids ARC's poor C++ support.
     */
    CFTypeRef _target;

    /**
     * Set the internally retained CF value to @target, releasing
     * the current value.
     *
     * @param target The value to be stored, or NULL.
     */
    void put (const CFTypeRef target) {
        /* Already referencing this value? */
        if (_target == target)
            return;

        /* Set and bump the retain count */
        if (target != NULL)
            CFRetain(target);
        
        if (_target != NULL)
            CFRelease(_target);

        _target = target;
    }

public:
    /**
     * Construct an empty instance.
     */
    inline ns_ptr () {
        _target = NULL;
    }
    
    /**
     * Create a new reference to the given @a target.
     *
     * @param target An Objective-C object.
     */
    ns_ptr (T target) {
        _target = NULL;
        put((__bridge CFTypeRef) target);
    }

    // Copy constructor
    inline  ns_ptr (const ns_ptr<T> &kptr) : ns_ptr((__bridge T) kptr._target) {}

    // Copy assignment operator
    ns_ptr& operator = (const ns_ptr &kptr) {
        put(kptr._target);
        return *this;
    };

    // Destructor
    ~ns_ptr() {
        if (_target != NULL)
            CFRelease(_target);
    };
    
    /**
     * Return the underlying retain count. Note that this is
     * totally unreliable and, in general, should not be used.
     */
    CFIndex referenceCount (void) const {
        if (_target == NULL) return 0;
        return CFGetRetainCount(_target);
    }
    
    /** Allow implicit conversions to the equivalent untyped ns_ptr<id>. */
    operator ns_ptr<id> () const {
        return ns_ptr<id>((__bridge id) _target);
    };
    
    /** Return a reference to the Objective-C instance. */
    inline T get (void) const {
        return objc_retainAutoreleaseReturnValue((__bridge T) _target);
    }
    
    /**
     * Return a borrowed reference to the backing CFTypeRef.
     */
    inline CFTypeRef target (void) const { return _target; }
    
    
    /** Return a reference to the Objective-C instance. */
    inline T operator *() const { return get(); }

    /** Return a borrowed reference to the Objective-C instance. */
    inline T operator ->() const { return (__bridge T) _target; };
};


/**
 * Perform Objective-C equality comparison. If both ns_ptr values are empty, they
 * will be considered equal.
 *
 * @param rhs The rhs value to perform comparison against.
 */
template<typename T> bool operator== (const ns_ptr<T> &lhs, const ns_ptr<T> &rhs) {
    if (lhs.target() == rhs.target())
        return true;
    
    return [(__bridge T) lhs.target() isEqual: (__bridge T) rhs.target()];
}

/**
 * Perform Objective-C (in-)equality comparison.
 *
 * @param rhs The rhs value to perform comparison against.
 */
template<typename T> bool operator!= (const ns_ptr<T> &lhs, const ns_ptr<T> &rhs) {
    return !(lhs == rhs);
}

/**
 * Create and return a new wrapping ns_ptr.
 *
 * @param target The Objective-C value to be wrapped.
 */
template<typename T> static inline ns_ptr<T> make_ns_ptr (T target) {
    return ns_ptr<T>(target);
}

} /* namespace pl */

namespace std {
    /**
     * Support std::hash over ns_ptr's enclosed Objective-C object.
     */
    template<typename T> struct hash<pl::ns_ptr<T>> {
        size_t operator()(const pl::ns_ptr<T> &ptr) const {
            return (size_t) [*ptr hash];
        }
    };

}