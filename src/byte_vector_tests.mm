/*
 * Copyright (c) 2015 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 */

#import "XSmallTest.h"

#import "byte_vector.hpp"
#import "PLSmallTestExt.hpp"

#include <vector>

#include <PLStdCPP/ftl/concepts/foldable.h>
#include <PLStdCPP/ftl/vector.h>

#include <limits.h>

xsm_given("ByteVector") {
    auto buffer = ByteVector { '1', '2', '3', '4' };
    
    xsm_when("initializing via runtime memset()") {
        xsm_when("using a size that fits within the DirectValue storage") {
            xsm_then("the bytes should be initialized with the provided value") {
                auto mset = ByteVector::Memset(0xA, 3);
                auto expected = ByteVector { 0xA, 0xA, 0xA };
                XCTAssertTrue(mset == expected);
            }
        }

        xsm_when("using a size greater than what fits DirectValue storage") {
            xsm_then("the bytes should be initialized with the provided value") {
                using namespace bytevector;
                auto mset = ByteVector::Memset(0xA, DirectValue::MaxBytes+1);
                
                uint8_t bytes[DirectValue::MaxBytes+1];
                memset(bytes, 0xA, DirectValue::MaxBytes+1);
                auto expected = ByteVector::Bytes(bytes, sizeof(bytes), false);
            }
        }
    }
    
    xsm_when("initializing with direct values") {
        auto v1 = ByteVector::Value<uint8_t>(42);
        
        xsm_then("The value should be identical to its buffer-based representation") {
            auto v2 = ByteVector { 42 };
            XCTAssertTrue(v1 == v2);
        }
    }
    
    xsm_when("initializing with a C string") {
        const char *cstr = "hello, world";
        auto v1 = ByteVector::CString(cstr, false);
        
        xsm_then("the full string + NUL must be included") {
            XCTAssertTrue(v1 == (ByteVector { 'h', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '\0' }));
        }
    }
    
    xsm_when("initializing with a string") {
        auto v1 = ByteVector::String("hello", false);
        
        xsm_then("the full string (but not NUL) must be included") {
            XCTAssertTrue(v1 == (ByteVector { 'h', 'e', 'l', 'l', 'o' }));
        }
    }
    
    
    xsm_when("right padding") {
        xsm_then("byte vectors smaller than the requested size should be padded with zeros") {
            auto result = ftl::fromRight(buffer.padRight(8));
            XCTAssertTrue(result == buffer.append(ByteVector{0, 0, 0, 0}));
        }
        
        xsm_then("byte vectors larger than the requested size should return an error") {
            XSMAssertLeft(buffer.padRight(3));
        }
    }
    
    xsm_when("left padding") {
        xsm_then("byte vectors smaller than the requested size should be padded with zeros") {
            auto result = ftl::fromRight(buffer.padLeft(8));
            XCTAssertTrue(result == (ByteVector {0, 0, 0, 0}).append(buffer));
        }
        
        xsm_then("byte vectors larger than the requested size should return an error") {
            XSMAssertLeft(buffer.padLeft(3));
        }
    }
    
    xsm_when("append") {
        auto input = std::vector<ByteVector> { buffer, buffer, buffer };
        auto appended = ftl::fold(input);
        XCTAssertTrue(appended == ByteVector::Bytes("123412341234", 12, false), @"appended should have been 123412341234 but is actually %s", appended.description().c_str());
    }

    xsm_when("length") {
        XCTAssertEqual(buffer.length(), 4);
    }
    
    xsm_when("read") {
        char data[1];
        auto result = buffer.read(&data, (size_t) 1, sizeof(data));
        XSMAssertRight(result);
        XCTAssertEqual(data[0], '2');
    }

    /* Verifies the input buffer's length, consumes a single character, and verifies the character's
     * value. */
    auto consumeChar = ftl::curry([self](char expected, size_t length, const ByteVector &next) -> ftl::either<ParseError, ByteVector> {
        /* Verify the length */
        if (next.length() != length)
            return fail<ByteVector>("Failed to consume expected number of bytes (" + std::to_string(next.length()) + ", " + std::to_string(length) + ")");
        
        /* Read and verify next char */
        char c;
        auto result = next.read(&c, 0, 1);
        XSMAssertRight(result);
        
        if (c != expected) {
            return fail<ByteVector>("Incorrect data read");
        } else {
            return next.drop(1);
        }
    });
    
    xsm_when("drop") {
        auto result = (buffer.drop(2) >>= consumeChar('3', 2)) >>= consumeChar('4', 1);
        XSMAssertRight(result);
    }

    xsm_when("dropRight") {
        auto result = (buffer.dropRight(2) >>= consumeChar('1', 2)) >>= consumeChar('2', 1);
        XSMAssertRight(result);
    }

    xsm_when("toString") {
        auto bv = ByteVector { 0x01, 0x02, 0x03, 0x04 };
        XCTAssertEqualObjects([NSString stringWithUTF8String: bv.toString().c_str()], @"01020304");
    }

    xsm_when("monoid<ByteVector>") {
        auto buffer = ftl::mappend(ByteVector::Bytes("12", 2, false), ByteVector::Bytes("34", 2, false));
        
        xsm_then("the two buffers must be appended successfully") {
            XCTAssertTrue(buffer == ByteVector::Bytes("1234", 4, false), @"buffer should have been 1234 but is actually %s", buffer.description().c_str());
        }
    }
    
    xsm_when("concatenating simultaneously on multiple threads") {
        int iterations = 100;
        xsm_then("the different appends should not interfere") {
            for (int iteration = 0; iteration < iterations; iteration++) {
                auto initialBuffer = ByteVector::Bytes("a", 1, false);
                auto secondBuffer = initialBuffer.append(ByteVector::Bytes("b", 1, false)); // Make sure we're in Buffered mode before starting with the threads.
                
                int threads = 100;
                std::vector<ByteVector> buffers;
                for (int i = 0; i < threads; i++) {
                    buffers.push_back(ByteVector::Empty);
                }
                auto buffersPtr = buffers.begin();
                
                auto buildBuffer = [&](size_t i) {
                    ByteVector tmp = secondBuffer;
                    char toAppend = (i % 62) + 'A';
                    return tmp.append(ByteVector::Bytes(&toAppend, 1, true));
                };
                
                dispatch_apply(threads, dispatch_get_global_queue(0, 0), ^(size_t i) {
                    buffersPtr[i] = buildBuffer(i);
                });
                
                for (int i = 0; i < threads; i++) {
                    auto should = buildBuffer(i);
                    auto is = buffers[i];
                    XCTAssert(should == is, @"%s should be %s", should.description().c_str(), is.description().c_str());
                }
            }
        }
    }
    
    // This slows down the unit test runtime considerable
    // Temporarily disabled until we add proper performance test support to XSmallTest;
#if XSM_TODO_PERFORMANCE_TEST
    xsm_when("concatenating lots of tiny stuff") {
        int iterations = 1000000;
        char bytesArray[] = "0123456789";
        char *bytes = bytesArray; // Make the block happy.
        
        xsm_then("it should be pretty fast") {
            [self measureBlock: ^{
                auto buffer = ByteVector::Empty;
                for(int i = 0; i < iterations; i++) {
                    buffer = buffer.append(ByteVector::Bytes(bytes + (i % 10), 1, false));
                }
            }];
        }
    }
#endif
    
    xsm_when("reading from files") {
        NSString *uniquePath = [@"/tmp" stringByAppendingPathComponent: [[NSProcessInfo processInfo] globallyUniqueString]];
        std::string uniquePathCPP = [uniquePath fileSystemRepresentation];
        auto open = [&]() { return ByteVector::File(uniquePathCPP); };
        auto openFail = [&]() { XSMAssertLeft(open()); };
        auto openSuccess = [&]() {
            auto result = open();
            XSMAssertRight(result);
            return ftl::fromRight(result);
        };
        
        auto write = [&](NSData *data) {
            BOOL success = [data writeToFile: uniquePath atomically: NO];
            XCTAssertTrue(success, @"Couldn't write file");
        };
        
        xsm_then("creating byte vectors from files should produce the file contents") {
            openFail();
            
            write([NSData data]);
            auto emptyFile = openSuccess();
            XCTAssertEqual(emptyFile.length(), 0);
            
            write([@"1234567890" dataUsingEncoding: NSUTF8StringEncoding]);
            auto file = openSuccess();
            auto should = ByteVector::Bytes("1234567890", 10, false);
            XCTAssert(file == should, @"%s should be %s", file.description().c_str(), should.description().c_str());
        }
        
        [[NSFileManager defaultManager] removeItemAtPath: uniquePath error: NULL];
    }
    
    xsm_when("iterating byte ranges") {
        // Construct a vector from a file to avoid internal coalescing optimizations, to ensure we get multiple byte ranges
        NSString *uniquePath = [@"/tmp" stringByAppendingPathComponent: [[NSProcessInfo processInfo] globallyUniqueString]];
        BOOL writeSuccess = [[@"defg" dataUsingEncoding: NSUTF8StringEncoding] writeToFile: uniquePath atomically: NO];
        XCTAssertTrue(writeSuccess, @"Couldn't write file");
        
        std::string uniquePathCPP = [uniquePath fileSystemRepresentation];
        auto fileVectorResult = ByteVector::File(uniquePathCPP);
        XSMAssertRight(fileVectorResult);
        auto fileVector = ftl::fromRight(fileVectorResult);
        
        auto bytesVector = ByteVector{ 'a', 'b', 'c' };
        auto bytesVector2 = ByteVector{ 'h', 'i', 'j', 'k', 'l' };
        
        auto concatVector = bytesVector.append(fileVector).append(bytesVector2);
        
        xsm_then("iterating byte ranges should return the three vectors sequentially") {
            int iteration = 0;
            concatVector.iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
                if (iteration == 0) {
                    XCTAssertEqual(len, size_t(3));
                    XCTAssertTrue(memcmp(ptr, "abc", 3) == 0);
                } else if (iteration == 1) {
                    XCTAssertEqual(len, size_t(4));
                    XCTAssertTrue(memcmp(ptr, "defg", 4) == 0);
                } else {
                    XCTAssertEqual(len, size_t(5));
                    XCTAssertTrue(memcmp(ptr, "hijkl", 5) == 0);
                }
                iteration++;
            }, false);
        }
        
        xsm_then("iterating with long term bytes should allow access to the memory after iteration completes") {
            std::vector<const uint8_t *> ptrs;
            std::vector<size_t> lens;
            std::vector<std::function<void(void)>> destructors;
            concatVector.iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
                ptrs.push_back(ptr);
                lens.push_back(len);
                destructors.push_back(destructor);
            }, true);
            
            XCTAssertEqual(int(ptrs.size()), 3);
            XCTAssertEqual(lens[0], size_t(3));
            XCTAssertTrue(memcmp(ptrs[0], "abc", 3) == 0);
            XCTAssertEqual(lens[1], size_t(4));
            XCTAssertTrue(memcmp(ptrs[1], "defg", 4) == 0);
            XCTAssertEqual(lens[2], size_t(5));
            XCTAssertTrue(memcmp(ptrs[2], "hijkl", 5) == 0);
            
            destructors[0]();
            destructors[1]();
            destructors[2]();
        }
        
        xsm_then("stopping iteration should actually stop iteration") {
            int iterations = 0;
            concatVector.iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
                iterations++;
                *stop = true;
            }, false);
            XCTAssertEqual(iterations, 1);
            
            iterations = 0;
            concatVector.iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
                iterations++;
                if (iterations == 2) {
                    *stop = true;
                }
            }, false);
            XCTAssertEqual(iterations, 2);
        }
        
        xsm_then("iterating a large file should produce the entire thing") {
            NSMutableData *data = [NSMutableData data];
            for (unsigned i = 0; i <= USHRT_MAX; i++) {
                unsigned short value = i;
                [data appendBytes: &value length: sizeof(value)];
            }
            
            writeSuccess = [data writeToFile: uniquePath atomically: NO];
            XCTAssertTrue(writeSuccess, @"Couldn't write file");
            
            auto fileVectorResult = ByteVector::File(uniquePathCPP);
            XSMAssertRight(fileVectorResult);
            auto fileVector = ftl::fromRight(fileVectorResult);
            
            int cursor = 0;
            fileVector.iterate([&](const uint8_t *ptr, size_t len, std::function<void(void)> destructor, bool *stop) {
                for (size_t i = 0; i < len; i++) {
                    unsigned short value = cursor / 2;
                    uint8_t *valuePtr = (uint8_t *)&value;
                    XCTAssertEqual(ptr[i], valuePtr[i % 2]);
                    cursor++;
                }
            }, false);
        }
        
        [[NSFileManager defaultManager] removeItemAtPath: uniquePath error: NULL];
    }
    
    xsm_when("writing to a file") {
        NSString *uniquePath = [@"/tmp" stringByAppendingPathComponent: [[NSProcessInfo processInfo] globallyUniqueString]];
        std::string uniquePathCPP = [uniquePath fileSystemRepresentation];
        
        NSMutableData *data = [NSMutableData data];
        for (unsigned i = 0; i <= USHRT_MAX; i++) {
            unsigned short value = i;
            [data appendBytes: &value length: sizeof(value)];
        }
        
        auto vector = ByteVector::Bytes([data bytes], [data length], false);
        auto result = vector.writeToFile(uniquePathCPP);
        XSMAssertRight(result);
        
        NSData *fileData = [NSData dataWithContentsOfFile: uniquePath];
        XCTAssertEqualObjects(data, fileData);
    }
    
    xsm_when("converting a ByteVector buffer to NSData") {
        NSMutableData *data = [NSMutableData data];
        for (unsigned i = 0; i <= USHRT_MAX; i++) {
            unsigned short value = i;
            [data appendBytes: &value length: sizeof(value)];
        }
        
        auto vector = ByteVector::Bytes([data bytes], [data length], false);
        
        auto vectorDataResult = vector.toNSData();
        XSMAssertRight(vectorDataResult);
        NSData *vectorData = ftl::fromRight(vectorDataResult).get();
        
        XCTAssertEqualObjects(data, vectorData);
    }
    
    xsm_when("converting a ByteVector view to NSData") {
        NSMutableData *data = [NSMutableData data];
        for (unsigned char i = 0; i < 8; i++) {
            [data appendBytes: &i length: sizeof(i)];
        }
        
        auto bytes = ByteVector::Bytes([data bytes], [data length], false);
        
        auto viewResult = bytes.drop(2) >>= [](const ByteVector &bv) { return bv.take(4); };
        XSMAssertRight(viewResult);
        auto view = ftl::fromRight(viewResult);
        auto viewDataResult = view.toNSData();
        XSMAssertRight(viewDataResult);
        auto viewData = ftl::fromRight(viewDataResult).get();
        
        NSData *expected = [data subdataWithRange: NSMakeRange(2, 4)];
        XCTAssertEqualObjects(expected, viewData);
    }
    
    xsm_when("converting a ByteVector buffer to std::vector") {
        // TODO
    }
    
    xsm_when("converting a ByteVector view to std::vector") {
        // TODO
    }
}

