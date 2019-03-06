/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * This API is based on the design of Michael Pilquist and Paul Chiusano's
 * Scala scodec-bits library: https://github.com/scodec/scodec-bits/
 */

#pragma once

#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#include <PLStdCPP/ftl/sum_type.h>
#include <PLStdCPP/ftl/either.h>
#include <PLStdCPP/ftl/lazy.h>
#include <PLStdCPP/ftl/functional.h>
#include <PLStdCPP/ftl/function.h>
#include <PLStdCPP/ftl/maybe.h>
#include <PLStdCPP/ftl/concepts/monoid.h>
#include <PLStdCPP/unit_type.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "parse_error.hpp"

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <PLObjCPP/ns_ptr.hpp>
#endif

/**
 * @internal
 *
 * ByteVector internal storage implementation.
 */
namespace bytevector {
    struct Append;
    struct Buffered;
    struct DirectValue;
    struct File;
    struct Empty;
    struct View;
    
    /** A sum type over all supported storage object types. */
    typedef ftl::sum_type<
        Append,
        Buffered,
        DirectValue,
        File,
        Empty,
        View
    > VTypeAll;
    
    template <typename VT> static inline std::size_t length (const VT &v);

    /** Fetch a value from a given DirectValue instance by type. This is specialized below for our supported types. */
    template<typename N> constexpr N getValue (const DirectValue &);

    /**
     * Wrap an existing value in a sum type instance.
     *
     * @tparam VT Sum type.
     * @tparam R Value type.
     * @param r Value to wrap.
     */
    template<typename VT, typename R, typename R0 = ftl::plain_type<R>> constexpr VT make_sum (R&& r) noexcept (std::is_nothrow_constructible<R0,R>::value) {
        return VT { ftl::constructor<R0>{}, std::forward<R>(r) };
    }
    
    /** An empty buffer */
    struct Empty { };
    
    /** An in-memory buffer */
    struct Buffered {
        /** The minimum amount of memory to allocate when doing internal buffer concatenation. */
        static const size_t kMinimumUnderlyingStorageSize = 64;
        
        /** How much space to allocate for a new internal buffer when resizing it, relative to the current length. */
        static const unsigned kReallocationFactor = 2;
        
        /** When appending a vector of this size or smaller, always do an internal append even if it requires reallocation. Larger vectors only do an internal append if the extra space is already available. NOTE: must be equal to or greater than twice DirectValue::MaxBytes */
        static const size_t kMaximumAlwaysInternalAppendSize = 1024;
        
        /** Underlying storage potentially shared between multiple Buffered instances, supporting in-place appends. */
        struct UnderlyingStorage {
            /** The number of bytes in the buffer that are actually being used. */
            std::atomic<size_t> usedLength;
            
            /** The total length of the allocation. */
            const size_t allocatedLength;
            
            /** The buffer pointer. */
            uint8_t *bytes;
            
            /** A destructor function to call when the underlying storage is destroyed, to deallocate bytes (if needed). */
            std::function<void(void)> destructorFunction;
            
            /** Construct a new UnderlyingStorage from the given values. */
            UnderlyingStorage(size_t usedLength, size_t allocatedLength, uint8_t *bytes, const std::function<void(void)> &destructorFunction) : usedLength(usedLength), allocatedLength(allocatedLength), bytes(bytes), destructorFunction(destructorFunction) {}
            
            /**
             * Move constructor, required because std::atomic doesn't have a default copy constructor and because
             * bytes can't be shared. Also nice because it makes make_shared more efficient (perhaps).
             */
            UnderlyingStorage (UnderlyingStorage &&other) noexcept : usedLength(other.usedLength.load()), allocatedLength(other.allocatedLength), bytes(other.bytes), destructorFunction(other.destructorFunction) {
                // Since we moved the bytes pointer from other, make sure it's not going to destroy it when it destructs.
                other.bytes = NULL;
                other.destructorFunction = [](){};
            }
            
            /** Destroy the underlying storage. Calls the provided destructor function. */
            ~UnderlyingStorage() {
                destructorFunction();
            }
        };

        /**
         * Construct a new Buffered instance.
         *
         * @param bytes A pointer to a buffer to use for the Buffered's underlying storage.
         * @param length The length of the buffer.
         * @param deleteOnDestruct If true, the bytes pointer will be deleted with delete[] when it's done being used.
         */
        Buffered (uint8_t *bytes, size_t length, const std::function<void(void)> &destructorFunction) : length(length), underlyingStorage(std::shared_ptr<UnderlyingStorage>::make_shared(UnderlyingStorage(length, length, bytes, destructorFunction))) {}
        
        /** Move constructor */
        Buffered (Buffered &&other) = default;
        
        /** Copy constructor */
        Buffered (const Buffered &other) = default;
        
        /** The number of bytes held by this object. */
        size_t length;
        
        /** The underlying storage for this object. Multiple Buffered instances can share the same underlying storage if they contain the same data, or a prefix. */
        std::shared_ptr<UnderlyingStorage> underlyingStorage;
        
        /** Get a pointer to this object's underlying bytes. */
        const uint8_t *bytes() const {
            return underlyingStorage->bytes;
        }
        
        /** 
         * Perform an internal append operation with another byte vector object. This performs a safe in-place append in the
         * underlying storage if possible and returns a new Buffered instance if so, and otherwise returns nothing.
         *
         * @tparam VR
         * @param rhs The storage value sum type to be appended to this storage instance.
         */
        template <typename VR> ftl::maybe<Buffered> internalAppend (VR &&rhs) const {
            size_t otherLength = bytevector::length(std::forward<VR>(rhs));
            if (underlyingStorage != nullptr && otherLength <= underlyingStorage->allocatedLength - length) {
                // We may have enough leftover storage to append the thing at the end of the current underlying storage, so just copy it in.
                // Atomically increment the used length, if and only if the used length matches our current length.
                // If they don't match, then somebody else incremented it and the bytes beyond our end are in use!
                // We must not overwrite them in that case.
                size_t localLength = length; // compare_exchange won't take a const variable for some reason
                if (underlyingStorage->usedLength.compare_exchange_strong(localLength, length + otherLength)) {
                    Buffered appended = *this;
                    read(std::forward<VR>(rhs), underlyingStorage->bytes + length, 0, otherLength, true);
                    appended.length += otherLength;
                    return ftl::just(appended);
                }
            }
            
            if (otherLength <= kMaximumAlwaysInternalAppendSize) {
                // Allocate a new underlying storage with enough room for the new data.
                size_t newLength = std::max({length + otherLength, length * kReallocationFactor, kMinimumUnderlyingStorageSize});
                auto newStorage = new uint8_t[newLength];
                memcpy(newStorage, bytes(), length);
                read(std::forward<VR>(rhs), newStorage + length, 0, otherLength, true);
                
                Buffered appended = *this;
                appended.underlyingStorage = std::shared_ptr<UnderlyingStorage>::make_shared(UnderlyingStorage(length + otherLength, newLength, newStorage, [=](){ delete [] newStorage; }));
                appended.length += otherLength;
                return ftl::just(appended);
            }
            
            // We don't meet the criteria for an internal append, so return nothing.
            return ftl::nothing<Buffered>();
        }
    };
    
    
    /** Directly store up to MaxBytes. */
    struct DirectValue {
        /**
         * Construct a DirectValue from the provided integral value.
         *
         * @tparam N The integral type.
         * @param v The integral value.
         */
        template <typename N> explicit DirectValue (const N v) : length(sizeof(N)), values(v) {
            static_assert(std::is_integral<N>::value, "Provided value must be an integral type");
        }
        
        /**
         * Construct a new instance containing @a length bytes initialized to @a value. If @a length is larger than
         * MaxBytes, an assertion will fire.
         */
        DirectValue (uint8_t value, size_t length) : length(length) {
            assert(length <= sizeof(values.array));
            memset(values.array, value, length);
        }
        
        /**
         * Construct a new instance with the given array initializer list. If the array is larger than
         * MaxBytes, an assertion will fire.
         *
         * @param bytes Array initializer.
         */
        explicit DirectValue (const std::initializer_list<uint8_t> &bytes) : length(bytes.size()), values(bytes) {}
        
        /**
         * Construct a new instance by copying @a length bytes from @a bytes. If @a length is larger than
         * MaxBytes, an assertion will fire.
         *
         * @param bytes The bytes to be copied.
         * @param length The total number of bytes to copy.
         */
        explicit DirectValue (const void *bytes, size_t length) : length(length) {
            assert(length <= sizeof(values.array));
            memcpy(values.array, bytes, length);
        }
        
        /** Move constructor */
        DirectValue (DirectValue &&other) = default;
        
        /** Copy constructor */
        DirectValue (const DirectValue &other) = default;
    
        /** Length in bytes of the directly encoded value. */
        const std::size_t length;
        
        /** Direct value storage */
        union Values {
            
            /**
             * Construct a new Values union with the given initializer list.
             *
             * @param bytes Initialization value.
             *
             * The size of @a bytes must not exceed MaxBytes.
             */
            Values (const std::initializer_list<uint8_t> &bytes) {
                assert(bytes.size() <= sizeof(values.array));
                
                size_t i = 0;
                for (auto b : bytes) {
                    array[i] = b;
                    i++;
                }
            };
            
            /**
             * Construct a new empty Values union.
             */
            Values () {}
            
            /*
             * Convenience constructors to support type-overload-based
             * initialization.
             *
             * @sa ByteVector::Value
             */
            Values (int8_t v) : int8(v) {};
            Values (uint8_t v) : uint8(v) {};
            Values (int16_t v) : int16(v) {};
            Values (uint16_t v) : uint16(v) {};
            Values (int32_t v) : int32(v) {};
            Values (uint32_t v) : uint32(v) {};
            Values (int64_t v) : int64(v) {};
            Values (uint64_t v) : uint64(v) {};
            
            
            /* Integral value storage */
            int8_t int8;
            uint8_t uint8;
            int16_t int16;
            uint16_t uint16;
            int32_t int32;
            uint32_t uint32;
            int64_t int64;
            uint64_t uint64;
            
            /** Byte-based storage, sized to match our largest supported integral type. */
            uint8_t array[8];
        } values;

        /** Maximum number of bytes that may be stored. */
        static constexpr size_t MaxBytes = sizeof(values.array);
    };
    
    /** A file-backed store. */
    struct File {
        /** A wrapper around a file descriptor so we can use a shared_ptr to share a single fd among multiple instances. */
        struct FDWrapper {
            /** The underlying file descriptor. */
            int fd;
            
            /** Construct a wrapper with a file descriptor. */
            FDWrapper (int fd) : fd(fd) {}
            
            /** Move constructor. We need to wipe out the other instance's fd so it doesn't try to close it. */
            FDWrapper (FDWrapper &&other) noexcept : fd(other.fd) {
                other.fd = -1;
            }
            
            /* Explicitly delete our copy constructor, as we have no mechanism for sharing a reference to our
             * backing file descriptor; reference counting must be performed externally. */
            FDWrapper (const FDWrapper &other) = delete;
            
            /** Destroy the wrapper, closing the file descriptor if it's valid. */
            ~FDWrapper () {
                if (fd >= 0) {
                    close(fd);
                }
            }
        };
        
        /** The underlying file descriptor, potentially shared among multiple instances. */
        const std::shared_ptr<FDWrapper> fdWrapper;
        
        /** The underlying file length. */
        const std::size_t length;
        
        /** Construct a new File instance with a given file descriptor and file length. */
        File (int fd, size_t length) : fdWrapper(std::make_shared<FDWrapper>(FDWrapper(fd))), length(length) {}
        
        /** Move constructor */
        File (File &&other) = default;
        
        /** Copy constructor */
        File (const File &other) = default;
        
        /** Create a new File instance from a file path. */
        static ftl::either<ParseError, File> Create(const std::string &path) {
            int fd = open(path.c_str(), O_RDONLY);
            if (fd < 0) {
                return errnoFail<File>("open");
            }
            
            struct stat st;
            int result = fstat(fd, &st);
            if (result < 0) {
                close(fd);
                return errnoFail<File>("fstat");
            }
            
            // TODO: switch away from size_t so we can represent files larger than memory?
            off_t length = st.st_size;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
            assert(length >= 0);
            if (length > std::numeric_limits<size_t>::max()) {
                length = std::numeric_limits<size_t>::max();
            }
#pragma clang diagnostic pop
            
            return yield(File(fd, size_t(length)));
        }
        
        /** Generate an error from the current errno value and the given operation name. */
        template <typename T>
        static ftl::either<ParseError, T> errnoFail (const std::string &operation) {
            int err = errno;
            std::string errString = strerror(err);
            return fail<T>(operation + " failed: " + errString + " (" + std::to_string(err) + ")");
        }
        
        /** Read bytes from the file, returning either an error or the number of bytes read. */
        ftl::either<ParseError, std::size_t> read(void *bytes, std::size_t offset, std::size_t len) const {
            std::size_t totalRead = 0;
            while (totalRead < len) {
                ssize_t result = pread(fdWrapper->fd, bytes, len - totalRead, offset + totalRead);
                if (result < 0 && errno != EINTR && errno != EAGAIN) {
                    return errnoFail<std::size_t>("pread");
                }
                if (result == 0) {
                    break;
                }
                if (result > 0) {
                    totalRead += result;
                }
            }
            return yield(totalRead);
        }
    };
    
    /* Specializations of getValue() for all supported types. */
    template<> constexpr int8_t    getValue<int8_t>    (const DirectValue &dv) { return dv.values.int8; }
    template<> constexpr uint8_t   getValue<uint8_t>   (const DirectValue &dv) { return dv.values.uint8; }
    
    template<> constexpr int16_t   getValue<int16_t>   (const DirectValue &dv) { return dv.values.int16; }
    template<> constexpr uint16_t  getValue<uint16_t>  (const DirectValue &dv) { return dv.values.uint16; }
    
    template<> constexpr int32_t   getValue<int32_t>   (const DirectValue &dv) { return dv.values.int32; }
    template<> constexpr uint32_t  getValue<uint32_t>  (const DirectValue &dv) { return dv.values.uint32; }
    
    template<> constexpr int64_t   getValue<int64_t>   (const DirectValue &dv) { return dv.values.int64; }
    template<> constexpr uint64_t  getValue<uint64_t>  (const DirectValue &dv) { return dv.values.uint64; }
    
    /** 
     * A view into a backing vector.
     *
     * Views are never computed over the entirity of an Append, as this would
     * result in a recursive definition. Instead, the view is nested within and (if necessary) split across the
     * operands of the Append.
     */
    struct View {
        typedef ftl::sum_type<Buffered, DirectValue, File> VType;
        
        View (const VType &storage, size_t offset, size_t length) : storage(storage), offset(offset), length(length) {}
        
        /** Move constructor */
        View (View &&other) = default;
        
        /** Copy constructor */
        View (const View &other) = default;
        
        /** Backing storage */
        const VType storage;
        
        /** Offset into storage projected by this view */
        const size_t offset;
        
        /** Length of this view's projection; must be less than the length of the backing storage. */
        const size_t length;
    };
    
    /**
      * A binary append operator; represents the appending of two vector storage values.
      */
    struct Append {
    public:
        typedef ftl::sum_type<Buffered, DirectValue, File, Empty, View, std::shared_ptr<Append>> VType;
        
    private:
        /* Map a VTypeAll instance to a supported Append::VType value */
        static VType wrap (const VTypeAll &r) {
            return r.match (
                [](const Append &r)   {
                    auto recursive_ptr = std::make_shared<Append>(r.lhs, r.rhs);
                    return make_sum<Append::VType>(recursive_ptr);
                },
                [](const Buffered &r)       { return make_sum<Append::VType>(r); },
                [](const DirectValue &r)    { return make_sum<Append::VType>(r); },
                [](const File &r)           { return make_sum<Append::VType>(r); },
                [](const Empty &r)          { return make_sum<Append::VType>(r); },
                [](const View &r)           { return make_sum<Append::VType>(r); }
            );
        };
        
    public:
        /**
         * Construct an append instance with the given VType operands.
         *
         * @param lhs First element to append.
         * @param rhs Second element append.
         *
         * The sum of lhs' and rhs' lengths must not exceed SIZE_T_MAX.
         *
         * @todo Verify that callers check for SIZE_T_MAX overflow and return an appropriate error?
         */
        Append (const VType &lhs, const VType &rhs) : lhs(lhs), rhs(rhs), length(bytevector::length(lhs) + bytevector::length(rhs)) {
            /* A length greater than SIZE_T_MAX is unsupported (and overflowed) */
            assert(SIZE_T_MAX - bytevector::length(lhs) >= bytevector::length(rhs));
        }
        
        /**
         * Construct an append instance with a VTypeAll lhs operand.
         *
         * @param lhs First element to append.
         * @param rhs Second element append.
         */
        Append (const VTypeAll &lhs, const VType &rhs) : Append(wrap(lhs), rhs) {}
        
        /**
         * Construct an append instance with a VTypeAll rhs operand.
         *
         * @param lhs First element to append.
         * @param rhs Second element append.
         */
        Append (const VType &lhs, const VTypeAll &rhs) : Append(lhs, wrap(rhs)) {}

        /**
         * Construct an append instance with the given VTypeAll operands.
         *
         * @param lhs First element to append.
         * @param rhs Second element append.
         */
        Append (const VTypeAll &lhs, const VTypeAll &rhs) : Append(wrap(lhs), wrap(rhs)) {}
        
        /** First element to append. */
        const VType lhs;
        
        /** Second element to append. */
        const VType rhs;
        
        /** Total length of both elements. */
        const std::size_t length;
    };

    /**
     * Return the byte length of the provided storage value.
     *
     * @tparam VT The storage value's enclosing sum type (@sa VTypeAll, Append::VType, ...).
     * @param v The storage value (enclosed in a supported sum type).
     *
     * @todo This implementation only provides efficient direct access when operating directly on a DirectValue type.
     */
    template <typename VT> static inline std::size_t length (const VT &v) {
        return v.match(
            [](const std::shared_ptr<Append> &bv) { return bv->length; },
            [](const Append &bv)        { return bv.length; },
            [](const Buffered &bv)      { return bv.length; },
            [](const DirectValue &bv)   { return bv.length; },
            [](const File &bv)          { return bv.length; },
            [](const Empty &)           { return 0; },
            [](const View &bv)          { return bv.length; }
        );
    }

    /**
     * Read a single integral value at @a offset, returning the remainder.
     *
     * @tparam VT The storage value's enclosing sum type (@sa VTypeAll, Append::VType, ...).
     * @tparam N The integral type.
     * @param v The storage value.
     * @param offset The offset at which to perform the read.
     *
     * @todo This implementation only provides efficient direct access when operating directly on a DirectValue type.
     */
    template <typename VT, typename N> ftl::either<ParseError, std::tuple<N, VTypeAll>> static read (const VT &v, std::size_t offset) {
        static_assert(std::is_integral<N>::value, "The requested value type must be an integral");

        const auto full_length = length(v);
        
        /* Verify sufficient data exists for our read */
        if (full_length < sizeof(N)) {
            return fail<std::tuple<N, VTypeAll>>("Requested read of " + std::to_string(sizeof(N)) + " bytes exceeds buffer length of " + std::to_string(full_length));
        }
        
        /* Calculate our remainder */
        return view(v, sizeof(N), full_length - sizeof(N)) >>= [&v, offset](const VTypeAll &remainder) {
            /* Read our value and map to (value, remainder) */
            N value;
            return [&value, &remainder](const std::size_t) {
                return std::make_tuple(value, remainder);
            } % read(v, &value, offset, sizeof(N), true);
        };
        
    };

    /**
     * Read up to a maximum of @a length bytes at @a offset from this byte vector into @a bytes.
     *
     * @tparam VT The storage value's enclosing sum type (@sa VTypeAll, Append::VType, ...).
     * @param v The storage value (as a sum type) from which up to @a len bytes will be read.
     * @param[out] bytes Output buffer to which the bytes will be written.
     * @param offset The offset at which to perform the read.
     * @param len The maximum number of bytes to be read.
     * @param require_full_read If true, an error will be returned if less than the requested number of bytes can be read.
     */
    template <typename VT> ftl::either<ParseError, std::size_t> read (const VT &v, void *bytes, std::size_t offset, std::size_t len, bool require_full_read) {
        /* Verify that offset is within our storage bounds. */
        if (offset > length(v))
            return fail<std::size_t>("Requested read offset of " + std::to_string(offset) + " bytes exceeds vector length of " + std::to_string(length(v)));
        
        return v.match(
            /* Recursively unwrap pointer references */
            [&](const std::shared_ptr<Append> &bv) { return read(make_sum<VTypeAll>(*bv), bytes, offset, len, require_full_read); },
            [&](const Append &bv) -> ftl::either<ParseError, std::size_t>     {
                
                /* If the offset falls within lhs, perform the first half of the read */
                auto lhs_read = yield(std::size_t(0));
                size_t lhs_len = length(bv.lhs);
                if (offset < lhs_len) {
                    lhs_read = read(bv.lhs, bytes, offset, std::min(lhs_len - offset, len), true);
                }
                
                return lhs_read.match(
                    [&](std::size_t lhs_read_size) {
                        /* Perform the rhs half of the read (if any). */
                        auto rhs_read = yield(std::size_t(0));
                        if (lhs_read_size < len) {
                            /* Calculate the remaining offset */
                            size_t rhs_offset = 0;
                            if (lhs_len < offset)
                                rhs_offset = offset - lhs_len;
                            
                            rhs_read = read(bv.rhs, ((uint8_t *) bytes) + lhs_read_size, rhs_offset, len - lhs_read_size, require_full_read);
                        }
                        
                        return [&](std::size_t rhs_read_size) {
                            return lhs_read_size + rhs_read_size;
                        } % rhs_read;
                    },
                    [&](ftl::otherwise) { return lhs_read; }
                );
            },
            [&](const Buffered &bv) -> ftl::either<ParseError, std::size_t>   {
                /* Verify that _offset + _bytes won't overflow */
                if (UINTPTR_MAX - offset < (uintptr_t) bv.bytes())
                    return yield(std::size_t(0));
                                
                /* Perform the read */
                memcpy(bytes, bv.bytes() + offset, std::min(len, bv.length));
                return yield(std::min(len, bv.length));
            },
            [&](const DirectValue &bv) -> ftl::either<ParseError, std::size_t>   {
                /* Verify that _offset + bytes won't overflow */
                if (UINTPTR_MAX - offset < (uintptr_t) bv.values.array)
                    return yield(std::size_t(0));
                                
                /* Perform the read */
                memcpy(bytes, bv.values.array + offset, std::min(len, bv.length));
                return yield(std::min(len, bv.length));
            },
            [&](const File &bv) -> ftl::either<ParseError, std::size_t>       { return bv.read(bytes, offset, len); },
            [&](const Empty &) -> ftl::either<ParseError, std::size_t>        { return yield(std::size_t(0)); },
            [&](const View &bv) -> ftl::either<ParseError, std::size_t>       {
                /* Verify that _offset + offset won't overflow */
                if (SIZE_MAX - bv.offset < offset)
                    return yield(std::size_t(0));
                
                /* Let the backing storage perform the read */
                return read(bv.storage, bytes, bv.offset + offset, std::min(len, bv.length), require_full_read);
            }
        );
    }
    
    /**
     * Yield a successful (ftl::Right) return value.
     *
     * @tparam T The value type.
     * @param value The return value to yield.
     */
    template <typename T> const ftl::either<ParseError, T> yield (const T &value) {
        return ftl::make_right<ParseError>(value);
    }

    /**
     * Return a new storage object containing the contents of @a lhs followed by the contents of @rhs.
     *
     * @tparam VL The lhs storage value's enclosing sum type (@sa VTypeAll, Append::VType, etc.).
     * @tparam VR The rhs storage value's enclosing sum type (@sa VTypeAll, Append::VType, etc.).
     * @param lhs The first value (represented as a sum type) to be included in the returned storage object.
     * @param rhs The second value (represented as a sum type) to be included in the returned storage object.
     */
    template <typename VL, typename VR> VTypeAll append (const VL &lhs, const VR &rhs) {
        return lhs.match(
            [&](const Append &bv) {
                if (bv.rhs.is<Buffered>()) {
                    ftl::maybe<Buffered> maybeResult = ftl::get<Buffered>(bv.rhs).internalAppend(rhs);
                    if (maybeResult.is<Buffered>()) {
                        return make_sum<VTypeAll>(Append(bv.lhs, make_sum<VTypeAll>(ftl::get<Buffered>(maybeResult))));
                    }
                }
                return make_sum<VTypeAll>(Append(lhs, rhs));
            },
            [&](const Buffered &bv) {
                ftl::maybe<Buffered> maybeResult = bv.internalAppend(rhs);
                if (maybeResult.is<Buffered>()) {
                    return make_sum<VTypeAll>(ftl::get<Buffered>(maybeResult));
                } else {
                    return make_sum<VTypeAll>(Append(lhs, rhs));
                }
            },
            [&](const DirectValue &lhsDirect) {
                return rhs.match(
                    [&](const DirectValue &rhsDirect) {
                        // Turn DirectValue + DirectValue into a Buffered so that constructing a vector from lots of DirectValues
                        // can use the efficient Buffered append path. Note that this code assumes internalAppend always succeeds
                        // in this case, which is true as long as kMaximumAlwaysInternalAppendSize is equal to or greater than
                        // twice the maximum size of a DirectValue.
                        static_assert(Buffered::kMaximumAlwaysInternalAppendSize >= (DirectValue::MaxBytes*2), "The internal append size must be >= (DirectValue::MaxBytes * 2)");
                        
                        Buffered empty = Buffered(nullptr, 0, [](){});
                        Buffered first = ftl::get<Buffered>(empty.internalAppend(lhs));
                        Buffered second = ftl::get<Buffered>(first.internalAppend(rhs));
                        return make_sum<VTypeAll>(second);
                    },
                    [&](ftl::otherwise) {
                        return make_sum<VTypeAll>(Append(lhs, rhs));
                    }
                );
            },
            [&](const Empty&) {
                return rhs;
            },
            [&](ftl::otherwise) {
                return make_sum<VTypeAll>(Append(lhs, rhs));
            }
        );
    }

    /**
     * Construct a projection at @a offset with @a len within @a v.
     *
     * @tparam VT The storage value's enclosing sum type (@sa VTypeAll, Append::VType, ...).
     * @param v The storage value over which the projection will be constructed.
     * @param offset The offset within @a v at which the projection will start.
     * @param len The total length of the projection.
     */
    template <typename VT> static ftl::either<ParseError, VTypeAll> view (const VT &v, std::size_t offset, std::size_t len) {
        /* Verify that offset is within our storage bounds. */
        const size_t storage_len = length(v);
        if (offset > storage_len)
            return fail<VTypeAll>("Requested offset of " + std::to_string(offset) + " bytes exceeds buffer length of " + std::to_string(storage_len));
        
        /* Verify that offset + len will not overflow */
        if (SIZE_MAX - offset < len)
            return fail<VTypeAll>("Requested offset of " + std::to_string(offset) + " and length of " + std::to_string(len) + " bytes would overflow maximum value of size_t");
        
        /* Verify that offset + len is within our storage bounds */
        if (offset + len > storage_len)
            return fail<VTypeAll>("Requested offset of " + std::to_string(offset) + " and length of " + std::to_string(len) + " bytes exceeds buffer length of " + std::to_string(storage_len));

        return v.match(
            /* Recursively unwrap pointer references */
            [&](const std::shared_ptr<Append> &bv) { return view(make_sum<VTypeAll>(*bv), offset, len); },
   
            [&](const Append &bv)   {
                /* If either side can service the request alone, let it, abandoning the wrapping Append(). */
                auto lhs_len = length(bv.lhs);
                if (offset >= lhs_len) {
                    /* Calculate the remaining offset */
                    size_t rhs_offset = 0;
                    if (lhs_len < offset)
                        rhs_offset = offset - lhs_len;
                    
                    return view(bv.rhs, rhs_offset, len);
                } else if (offset + len < length(bv.lhs)) {
                    return view(bv.lhs, offset, len);
                }
 
                /* If we were unable to discard either the lhs or rhs operand, the view spans both operands, and
                 * we'll need to construct a new Append over views into the operands. */
                auto result = view(bv.lhs, offset, length(bv.lhs) - offset) >>= [&](const VTypeAll &lhs) {
                    return ftl::fmap([&](const VTypeAll &rhs) {
                        return Append(lhs, rhs);
                    }, view(bv.rhs, 0, len - length(lhs)));
                };
                
               
                return [](const Append &a) { return make_sum<VTypeAll>(a); } % result;
            },

            [&](const Buffered &bv)  {
               /* Wrap our buffer in a view */
               auto view = View(make_sum<View::VType>(bv), offset, len);
               return yield(make_sum<VTypeAll>(view));
            },
                       
            [&](const DirectValue &bv)  {
               /* Wrap our buffer in a view */
               auto view = View(make_sum<View::VType>(bv), offset, len);
               return yield(make_sum<VTypeAll>(view));
            },

            [&](const File &bv)          {
                auto view = View(make_sum<View::VType>(bv), offset, len);
                return yield(make_sum<VTypeAll>(view));
            },
            [&](const Empty &)       { return fail<VTypeAll>("Requested " + std::to_string(len) + " bytes from an empty vector"); },

            [&](const View &bv)      {
               /* Verify that offset + bv.offset won't overflow */
               if (SIZE_MAX - offset < bv.offset)
                   return fail<VTypeAll>("Requested offset of " + std::to_string(offset) + " bytes would overflow maximum value of size_t");
               
               return view(bv.storage, bv.offset + offset, len);
            }
        );
    }
    
    /**
     * Describe a storage object in human-readable terms.
     *
     * @tparam VT The storage value's enclosing sum type (@sa VTypeAll, Append::VType, ...).
     * @param v The storage object to describe, represented via a supported sum type.
     * @param stream The stringstream object to write the description into.
     * @param indentLevel The indentation level to use when writing the description.
     */
    template <typename VT> static void describe (const VT &v, std::ostream &stream, int indentLevel) {
        auto indent = [&]{
            for (int i = 0; i < indentLevel; i++) {
                stream << "\t";
            }
        };
        
        v.matchE(
            /* Recursively unwrap pointer references */
            [&](const std::shared_ptr<Append> &bv) {
                describe(make_sum<VTypeAll>(*bv), stream, indentLevel);
            },
            [&](const Append &bv)        {
                stream << "Append(\n";
                
                indent(); stream << "\tlhs = ";
                describe(bv.lhs, stream, indentLevel + 2);
                stream << "\n";
                
                indent(); stream << "\trhs = ";
                describe(bv.rhs, stream, indentLevel + 2);
                stream << "\n";
                
                indent(); stream << ")";
            },
            [&](const Buffered &bv)      {
                const size_t maxToPrint = 64;
                stream << "Buffered(length=" << bv.length << " bytes=";
                stream.write((const char *)bv.bytes(), std::min(bv.length, maxToPrint));
                if (bv.length > maxToPrint) {
                    stream << "...";
                }
                stream << ")";
            },
            [&](const DirectValue &bv)   {
                stream << "DirectValue(length=" << bv.length << " values=";
                stream.write((const char *)bv.values.array, bv.length);
                stream << ")";
            },
            [&](const File &bv) {
                stream << "File(fd=" << bv.fdWrapper->fd << " length=" << bv.length << ")";
            },
            [&](const Empty &)           {
                stream << "Empty";
            },
            [&](const View &bv)          {
                stream << "View(offset=" << bv.offset << " length=" << bv.length << " storage:\n";
                describe(bv.storage, stream, indentLevel + 1);
                stream << "\n";
                indent(); stream << ")";
            }
        );
    }
    
    /**
     * Iterate over contiguous byte ranges in a value.
     *
     * @param v The storage object to iterate over.
     * @param f The function to call for each contiguous byte range.
     * @param stop A pointer to a boolean, initially false, which causes iteration to stop when the function sets it to true.
     * @return unit if iteration succeeded, or an error if some data couldn't be read.
     */
    template <typename VT> static ftl::either<ParseError, pl::Unit> iterate(const VT &v, const std::function<void(const uint8_t *ptr, size_t length, const std::function<void(void)> destructor, bool *stop)> &f, bool provideLongTermBytes, bool *stop) {
        if (*stop) {
            return yield(pl::unit);
        }
        
        // A destructor to pass to the iteration function when provideLongTermBytes=false, to trap when
        // it's called by accident.
        auto poisonDestructor = [](){
            fprintf(stderr, "The destructor callback was invoked when provideLongTermBytes=false, this should never happen.\n");
            abort();
        };
        
        return v.match(
            /* Recursively unwrap pointer references */
            [&](const std::shared_ptr<Append> &bv) {
                iterate(make_sum<VTypeAll>(*bv), f, provideLongTermBytes, stop);
                return yield(pl::unit);
            },
            [&](const Append &bv) -> ftl::either<ParseError, pl::Unit> {
                auto result = iterate(bv.lhs, f, provideLongTermBytes, stop);
                if (result.is<ftl::Left<ParseError>>()) {
                    return result;
                }
                
                // Premature optimization: bail out early if iteration stopped during the first call.
                if (*stop) {
                    return yield(pl::unit);
                }
                
                return iterate(bv.rhs, f, provideLongTermBytes, stop);
            },
            [&](const Buffered &bv)      {
                if (provideLongTermBytes) {
                    Buffered *copy = new Buffered(bv);
                    f(copy->bytes(), copy->length, [=](){ delete copy; }, stop);
                } else {
                    f(bv.bytes(), bv.length, poisonDestructor, stop);
                }
                return yield(pl::unit);
            },
            [&](const DirectValue &bv)   {
                if (provideLongTermBytes) {
                    DirectValue *copy = new DirectValue(bv);
                    f(copy->values.array, copy->length, [=](){ delete copy; }, stop);
                } else {
                    f(bv.values.array, bv.length, poisonDestructor, stop);
                }
                return yield(pl::unit);
            },
            [&](const File &bv) -> ftl::either<ParseError, pl::Unit> {
                uint8_t localBuffer[1024];
                size_t toRead = bv.length;
                size_t cursor = 0;
                
                while (toRead > 0 && !*stop) {
                    uint8_t *buffer;
                    uint8_t *heapBuffer = NULL;
                    
                    if (provideLongTermBytes) {
                        heapBuffer = (uint8_t *)malloc(sizeof(localBuffer));
                        buffer = heapBuffer;
                    } else {
                        buffer = localBuffer;
                    }
                    
                    auto result = bv.read(buffer, cursor, sizeof(localBuffer));
                    if (result.is<ftl::Left<ParseError>>()) {
                        free(heapBuffer);
                        return ftl::make_left<pl::Unit>(ftl::fromLeft(result));
                    }
                    
                    size_t amountRead = ftl::fromRight(result);
                    toRead -= amountRead;
                    cursor += amountRead;
                    
                    f(buffer, amountRead, [=](){ free(heapBuffer); }, stop);
                }
                
                return yield(pl::unit);
            },
            [&](const Empty &)           {
                return yield(pl::unit);
            },
            [&](const View &bv)          {
                size_t toSkip = bv.offset;
                size_t toPass = bv.length;
                auto wrapperF = [&](const uint8_t *ptr, size_t length, const std::function<void(void)> destructor, bool *stop) {
                    // If we're completely within the first skip region, account for the bytes
                    // we see here, and return early.
                    if (toSkip >= length) {
                        toSkip -= length;
                        return;
                    }
                    
                    // If there are any bytes remaining to skip, jigger the pointer and length
                    // to account for them, and note that no bytes remain to be skipped.
                    if (toSkip > 0) {
                        ptr += toSkip;
                        length -= toSkip;
                        toSkip = 0;
                    }
                    
                    // If there are no bytes remaining to pass, return early.
                    if (toPass == 0) {
                        return;
                    }
                    
                    // If the bytes to pass exceeds this length, account for it and call the original function.
                    if (toPass > length) {
                        toPass -= length;
                        f(ptr, length, destructor, stop);
                    }
                    
                    // The bytes to pass fit entirely within this chunk. Call the original function with that
                    // smaller number of bytes, and zero toPass.
                    if (toPass <= length) {
                        f(ptr, toPass, destructor, stop);
                        toPass = 0;
                    }
                };
                return iterate(bv.storage, wrapperF, provideLongTermBytes, stop);
            }
        );
    }

}

/**
 * An immutable vector of bytes.
 */
struct ByteVector {
private:
    /** Storage types supported by the root ByteVector class. */
    typedef bytevector::VTypeAll VType;

public:
    /** An empty byte vector */
    static ByteVector Empty;
    
    /**
     * Construct a new byte vector with the given byte array value.
     *
     * @param bytes A byte array.
     */
    ByteVector (const std::initializer_list<uint8_t> &bytes) : _storage(VType(ftl::constructor<bytevector::Empty>(), bytevector::Empty{})) {
        using namespace bytevector;

        /* Prefer DirectValue encoding over allocation. */
        if (bytes.size() <= DirectValue::MaxBytes) {
            _storage = VType(ftl::constructor<DirectValue>(), DirectValue(bytes));
        } else {
            uint8_t *copied = new uint8_t[bytes.size()];
            memcpy(copied, bytes.begin(), bytes.size());
            _storage = VType(ftl::constructor<Buffered>(), Buffered(copied, bytes.size(), [=](){ delete[] copied; }));
        }
    }
    
    /**
     * Construct a new byte vector with the given integral value.
     *
     * @param value An integral value.
     */
    template <typename N> static const ByteVector Value (const N &value) {
        using namespace bytevector;
        return ByteVector(VType(ftl::constructor<DirectValue>(), DirectValue(value)));
    }
    
    /**
     * Construct a byte vector containing a @a count of bytes, with value @a value.
     *
     * @param value The byte value.
     * @param count The number of bytes.
     */
    static const ByteVector Memset (uint8_t value, std::size_t count) {
        using namespace bytevector;

        /* Try to handle this without an allocation */
        if (count <= DirectValue::MaxBytes) {
            return ByteVector(VType(ftl::constructor<DirectValue>(), DirectValue(value, count)));
        }
        
        /* Too large for DirectValue; have to allocate a backing buffer */
        auto bytes = new uint8_t[count];
        std::memset(bytes, value, count);
        return ByteVector(VType(ftl::constructor<Buffered>(), Buffered(bytes, count, [=](){ delete[] bytes; })));
    }

    /**
     * Construct a new byte vector with the given source data.
     *
     * @param src The source buffer.
     * @param length The number of bytes available in @a src.
     * @param copy If true, all bytes will be copied from @a src into an internally allocated buffer. If false,
     * a weak reference to @a src will be maintained.
     */
    static const ByteVector Bytes (const void *src, size_t length, bool copy) {
        using namespace bytevector;

        uint8_t *bytes;
        std::function<void(void)> destructor = [](){};
        if (length <= DirectValue::MaxBytes) {
            /* Avoid the allocation */
            return ByteVector(VType(ftl::constructor<DirectValue>(), DirectValue(src, length)));
        } else if (copy) {
            bytes = new uint8_t[length];
            destructor = [=](){ delete bytes; };
            memcpy(bytes, src, length);
        } else {
            bytes = (uint8_t *) src;
        }
        
        return ByteVector(VType(ftl::constructor<Buffered>(), Buffered(bytes, length, destructor)));
    }
    
    /**
     * Construct a new byte vector with the given C string. The trailing NUL *WILL* be included
     * in the returned byte vector.
     *
     * @param string A NUL-terminated C string.
     * @param copy If true, the string data will be coped. If false, the ByteVector will maintain a borrowed reference
     * to the string.
     */
    static const ByteVector CString (const char *string, bool copy) {
        return ByteVector::Bytes (string, strlen(string) + 1, copy);
    }
    
    /**
     * Construct a new byte vector with the given string. A trailing NUL *WILL NOT* be included
     * in the returned byte vector.
     *
     * @param string A NUL-terminated C string.
     * @param copy If true, the string data will be coped. If false, the ByteVector will maintain a borrowed reference
     * to the string.
     */
    static const ByteVector String (const std::string &string, bool copy) {
        return ByteVector::Bytes (string.c_str(), string.length(), copy);
    }

    /**
     * Construct a new byte vector with the contents of a given file.
     *
     * @param path The path to the file.
     * @return The byte vector, or an error.
     */
    static ftl::either<ParseError, ByteVector> File (const std::string &path) {
        auto result = bytevector::File::Create(path);
        auto summed = [](const bytevector::File &f) { return bytevector::make_sum<bytevector::VTypeAll>(f); } % result;
        auto promoted = promote % summed;
        return promoted;
    }
    
    /** Move constructor */
    ByteVector (ByteVector &&other) = default;
    
    /** Move assignment operator */
    ByteVector& operator= (ByteVector &&other) = default;
    
    /** Copy constructor */
    ByteVector (const ByteVector &other) = default;
    
    /** Copy assignment operator */
    ByteVector& operator= (const ByteVector &other) = default;

    /** Return the length, in bytes. */
    std::size_t length () const {
        return bytevector::length(_storage);
    }
    
    /**
     * Return a new byte vector representing this vector's data, followed by the provided vector's
     * data.
     *
     * @param rhs The vector to append.
     */
    ByteVector append (const ByteVector &rhs) const {
        return promote(bytevector::append(_storage, rhs._storage));
    }
    
    /**
     * Return a new vector of length @a n, containing up to @a n 0 bytes, followed by this vector's data.
     *
     * If this vector is larger than @a n, it cannot be padded to exactly @a n bytes, and an
     * error will be returned.
     *
     * @param n The exact size to which the returned byte vector will be padded.
     */
    ftl::either<ParseError, ByteVector> padLeft (std::size_t n) const {
        const size_t baselen = length();
        if (baselen > n) {
            return fail<ByteVector>("The requested padded size of " + std::to_string(n) + " bytes is smaller than the existing length of " + std::to_string(length()) + " bytes");
        }
        
        return yield(ByteVector::Memset(0, n - baselen).append(*this));
    }
    
    /**
     * Return a new vector of length @a n, containing this vector's data, followed by up to
     * @a n 0 bytes.
     *
     * If this vector is larger than @a n, it cannot be padded to exactly @a n bytes, and an
     * error will be returned.
     *
     * @param n The exact size to which the returned byte vector will be padded.
     */
    ftl::either<ParseError, ByteVector> padRight (std::size_t n) const {
        const size_t baselen = length();
        if (baselen > n) {
            return fail<ByteVector>("The requested padded size of " + std::to_string(n) + " bytes is smaller than the existing length of " + std::to_string(length()) + " bytes");
        }

        return yield(this->append(ByteVector::Memset(0, n - baselen)));
    }

    /**
     * Read exactly @a length bytes at @a offset from this byte vector into @a bytes, or return an error
     * if insufficient data is available.
     *
     * @param[out] bytes Output buffer to which the bytes will be written.
     * @param offset The offset at which to perform the read.
     * @param length The number of bytes to be read.
     */
    ftl::either<ParseError, pl::Unit> read (void *bytes, std::size_t offset, std::size_t length) const {
        return [length](const std::size_t nread) {
            assert(nread == length);
            return pl::unit;
        } % bytevector::read(_storage, bytes, offset, length, true);
    }

    /**
     * Read an integral value from this byte vector at @a offset, or return an error if insufficient data
     * is available.
     *
     * @tparam N The integral value type to be read.
     * @param offset The offset at which to perform the read.
     * @return On success, returns the consumed value and the remaining unconsumed ByteVector
     * data.
     */
    template <typename N> ftl::either<ParseError, std::tuple<N, ByteVector>> read (std::size_t offset) const {
        static_assert(std::is_integral<N>::value, "The requested value type must be an integral");
        return [](const std::tuple<N, VType> &value) {
            return std::make_tuple(std::get<0>(value), promote(std::get<1>(value)));
        } % bytevector::read<VType, N>(_storage, offset);
    };
    
    /**
     * Read an integral value from this byte vector, or return an error if insufficient data
     * is available.
     *
     * @tparam N The integral value type to be read.
     * @return On success, returns the consumed value and the remaining unconsumed ByteVector
     * data.
     */
    template <typename N> ftl::either<ParseError, std::tuple<N, ByteVector>> read () const {
        return read<N>(0);
    };

    /**
     * Return a new byte vector containing all but the first @a n bytes of this byte vector,
     * or an error if dropping @a n bytes would overrun the end of this byte vector.
     *
     * @param n The number of bytes to skip.
     */
    ftl::either<ParseError, ByteVector> drop (std::size_t n) const {
        return checkOffset(n) >> (promote % bytevector::view(_storage, n, length() - n));
    }
    
    /**
     * Return a new byte vector containing exactly @a length bytes from this byte vector,
     * or an error if insufficient data is available.
     *
     * @param[out] bytes Output buffer to which the bytes will be written.
     * @param length The maximum number of bytes to be read from the byte vector.
     */
    ftl::either<ParseError, ByteVector> take (std::size_t length) const {
        return promote % bytevector::view(_storage, 0, length);
    };

    /**
     * Return a new byte vector containing all but the last @a n bytes of this byte vector,
     * or an error if dropping @a n bytes would overrun the start of the byte vector.
     *
     * @param n The number of bytes to drop.
     */
    ftl::either<ParseError, ByteVector> dropRight (std::size_t n) const {
        return checkOffset(n) >> promote % bytevector::view(_storage, 0, length() - n);
    }
    
    /** Return true if empty. */
    bool isEmpty () const {
        return length() == 0;
    }
    
    /** Unsafe array subscript operator. */
    inline uint8_t operator[] (std::size_t idx) const { return unsafe_get(idx); };

    /**
     * Iterate over contiguous bytes in the vector, calling f for each one. f takes the following parameters:
     *
     * A pointer to a contiguous chunk of bytes.
     * The number of bytes in this chunk.
     * A destructor function to call when done with the bytes. This MUST be called exactly once after the provided bytes are no longer needed
     * if provideLongTermBytes is true.
     * A pointer to a bool that indicates whether iteration should stop. Write true to this pointer to stop iteration early.
     *
     * @param f The function to call for each chunk of contiguous bytes.
     * @param provideLongTermBytes If true, the pointer passed into the function remains valid until the passed in destructor function is called.
     */
    ftl::either<ParseError, pl::Unit> iterate(const std::function<void(const uint8_t *ptr, size_t length, const std::function<void(void)> destructor, bool *stop)> &f, bool provideLongTermBytes) const {
        bool stop = false;
        return bytevector::iterate(_storage, f, provideLongTermBytes, &stop);
    }
    
    /**
     * Return a human-readable description of the ByteVector's backing storage.
     *
     * This is primarily intended for internal debugging, and the format of the returned representation
     * is implementation-defined and may change in future releases.
     */
    std::string description () const;

    /** Return a string representation of the byte vector's contents. */
    inline std::string toString () const {
        std::stringstream ss;
        for (std::size_t i = 0; i < length(); i++) {
            ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)unsafe_get(i);
        }
        return ss.str();
    }

    /**
     * Write the byte vector's contents to a file.
     *
     * @param path The path to the destination file.
     * @return A success result, or a ParseError if writing failed.
     */
    inline ftl::either<ParseError, pl::Unit> writeToFile (std::string path) const {
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) {
            return bytevector::File::errnoFail<pl::Unit>("open");
        }
        
        auto result = writeToFileDescriptor(fd);
        close(fd);
        return result;
    }
    
    /**
     * Write the byte vector's contents to a file descriptor.
     *
     * @param fd The file descriptor to write to.
     * @return A success result, or a ParseError if writing failed.
     */
    inline ftl::either<ParseError, pl::Unit> writeToFileDescriptor (int fd) const {
        auto result = yield(pl::unit);
        
        auto iterateResult = iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
            while (len > 0) {
                ssize_t written = write(fd, ptr, len);
                if (written < 0 && errno != EINTR && errno != EAGAIN) {
                    result = bytevector::File::errnoFail<pl::Unit>("write");
                    *stop = true;
                    return;
                }
                
                ptr += written;
                len -= written;
            }
        }, /*provideLongTermBytes=*/false);
        
        if (iterateResult.is<ftl::Left<ParseError>>()) {
            return iterateResult;
        }
        
        return result;
    }
    
#ifdef __OBJC__
    /**
     * Convert a byte vector to an NSData instance. Note that this will copy all of the
     * underlying data, so beware the increased memory usage.
     *
     * @return A ParseError if reading the underlying data failed, or an NSData object.
     */
    ftl::either<ParseError, pl::ns_ptr<NSData *>> toNSData () const;
    
    /**
     * Convert an NSData instance to a ByteVector.
     *
     * @param data The NSData to be converted.
     */
    static const ByteVector fromNSData (NSData *data);
#endif
    
    /**
     * Convert this ByteVector to a std::vector<uint8_t> instance. Note that this will copy all of the
     * underlying data, so beware the increased memory usage.
     *
     * @return A ParseError if reading the underlying data failed, or a std::vector<uint8_t> instance.
     */
    inline ftl::either<ParseError, std::vector<uint8_t>> toVector () const {
        /* Allocate a vector large enough to hold the backing bytes */
        auto len = length();
        std::vector<uint8_t> vec(len);
        
        /* If we contain any bytes, perform a read from the bytevector, and on success, map to our now-populated std::vector.
         * &vec[0] actually accesses element zero, so it's bad if the vector is empty. Optimize/bypass that case. */
        if (len == 0) {
            return yield(vec);
        } else {
            return [&](const pl::Unit &) {
                return vec;
            } % read(&vec[0], 0, len);
        }
    }
    
    /**
     * Convert a std::vector<uint8_t> instance to a ByteVector.
     *
     * @param data The std::vector<uint8_t> to be converted.
     */
    static const ByteVector fromVector (const std::vector<uint8_t> &vec) {
        // TODO: Allow no-copy?
        /* &vec[0] actually accesses element zero, so it's bad if the vector is empty. Optimize/bypass that case. */
        return (vec.empty()
                ? ByteVector::Empty
                : ByteVector::Bytes(&vec[0], vec.size(), /*copy=*/true));
    }
    
private:
    /**
     * Construct a new ByteVector.
     *
     * @param storage Backing storage for this instance.
     */
    explicit ByteVector (const VType &storage) : _storage(storage) {}
    
    /**
     * Fetch a single byte at the given index; if the index is out-of-range, the implementation
     * will throw an exception or abort.
     *
     * This is used to support the unsafe subscript accessor.
     */
    inline uint8_t unsafe_get (std::size_t idx) const {
        // fromRight will throw an 
        return std::get<0>(ftl::fromRight(read<uint8_t>(idx)));
    }
    
    /** Promote a backing storage object to a ByteVector instance. */
    static inline const ByteVector promote (const VType &storage) {
        return ByteVector(storage);
    }
    
    /* Returns an error if @a offset is larger than the vector's length. */
    inline ftl::either<ParseError, ByteVector> checkOffset (std::size_t offset) const {
        if (offset > length())
            return fail<ByteVector>("Requested offset of " + std::to_string(offset) + " bytes exceeds buffer length of " + std::to_string(length()));
        
        return yield(Empty);
    }
    
    /** Backing storage. */
    VType _storage;
};

bool operator ==(const ByteVector &lhs, const ByteVector &rhs);

/* FTL concept implementation(s) */
namespace ftl {
    template<> struct monoid<ByteVector> {
        static inline ByteVector id() { return ByteVector::Empty; }
        static inline ByteVector append (const ByteVector &m1, const ByteVector &m2) {
            return m1.append(m2);
        }
        
        static constexpr bool instance = true;
    };
}
