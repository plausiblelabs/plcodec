/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#pragma once

#include <PLStdCPP/ftl/either.h>
#include <PLStdCPP/ftl/either_trans.h>

#include <memory.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <deque>

/**
* A parse error.
*/
class ParseError {
public:
    explicit ParseError (const std::string &msg) : ParseError(msg, std::deque<const std::string>()) {}
    explicit ParseError (std::string &&msg) : ParseError(msg, std::deque<const std::string>()) {}

    explicit ParseError (const std::string &msg, const std::deque<const std::string> &context) : _message(msg), _context(context) {}
    explicit ParseError (std::string &&msg, const std::deque<const std::string> &context) : _message(std::move(msg)), _context(context) {}

    /** Return a human-readable error message that includes context, if any. */
    std::string message () const {
        if (_context.size() > 0) {
            std::string ctx = _context[0];
            for (size_t i = 1; i < _context.size(); i++) {
                ctx += "/" + _context[i];
            }
            return ctx + ": " + _message;
        } else {
            return _message;
        }
    }
    
    /** Return the stack of context strings.  The outermost context identifier is at the front of the deque. */
    std::deque<const std::string> context () const {
        return _context;
    }
    
    /** Return a new ParseError with the given context identifier pushed into the context stack. */
    ParseError pushContext(const std::string &context) const {
        auto newContext = _context;
        newContext.push_front(context);
        return ParseError(_message, newContext);
    }

    bool operator== (const ParseError &rhs) { return message() == rhs.message(); }

private:
    const std::string _message;
    const std::deque<const std::string> _context;
};

/** Return an either<ParseError, T> containing a ParseError with the given message. */
template<typename T>
ftl::either<ParseError, T> fail (const std::string str) {
    return ftl::make_left<T>(ParseError(str));
}

/** Return an either<ParseError, T> containing the provided value. */
template<typename T>
auto yield(T&& t) -> decltype(ftl::make_right<ParseError>(std::forward<T>(t))) {
    return ftl::make_right<ParseError>(std::forward<T>(t));
}

/**
 * Apply `f` to left values.
 *
 * If `e` is a right value, it's simply passed on without any
 * modification. However, if `e` is a left value, `f` is applied and
 * its result is what's passed on.
 */
template<typename F, typename L, typename T>
ftl::either<L,T> left_map(F f, const ftl::either<L,T> &e) {
    return e.match (
        [](const T &r){ return ftl::make_right<L>(r); },
        [f](const ftl::Left<L> &l){ return ftl::make_left<T>(f(*l)); }
    );
}

/* FTL concept implementation(s) */
namespace ftl {
    template<>
    struct monoid<ParseError> {
        static ParseError id () { return ParseError(""); }

        static ParseError append (const ParseError &lhs, const ParseError &rhs) {
        if (lhs.message().empty()) {
            return ParseError(rhs.message());
        } else if (rhs.message().empty()) {
            return ParseError(lhs.message());
        } else {
            return ParseError(lhs.message() + " or " + rhs.message());
        }
    }

    static constexpr bool instance = true;
    };
}