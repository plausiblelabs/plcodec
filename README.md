# plcodec

[![Build Status](https://travis-ci.org/plausiblelabs/plcodec.png?branch=master)](https://travis-ci.org/plausiblelabs/plcodec)

An ObjC++ library that provides combinators for purely functional, declarative encoding and decoding of binary data.  Its design is largely derived from that of the [scodec](https://github.com/scodec/scodec) library for Scala.

## Examples

The `codecs` namespace provides a number of predefined codecs.  In the following example, we use the `uint32` codec to encode a `u32` value to a `ByteVector` representation, and then decode the `ByteVector` back to its `u32` representation:

```cpp
uint32_t v0 = 258;
auto expected = ByteVector { 0x00, 0x00, 0x01, 0x02 };

/* Encode a uint32 value to a ByteVector */
auto codec = codecs::uint32;
auto encoded = codec.encode(v0);
encoded.matchE (
    [self](ftl::Left<ParseError> e) { XCTFail("Encoding failed: %s", e->message().c_str()); },
    [&](const ByteVector &bv) { XCTAssertTrue(expected == bv); }
);

/* Decode a uint32 value from a ByteVector */
auto decoded = codec.decode(expected);
decoded.matchE (
    [self](ftl::Left<ParseError> e) { XCTFail("Decoding failed: %s", e->message().c_str()); },
    [&](const DecodeResult<uint32_t> &result) { XCTAssertEqual(v0, result.value()); }
);
```

Here's an example of a more complex codec for a fictitious binary file format, which uses a number of the built-in combinators:

```cpp
/* File format version record */
PL_RECORD_STRUCT(
    TestRecordVersion,
    (uint8_t, compatVersion),
    (uint8_t, featureVersion)
);

/* File section record */
PL_RECORD_STRUCT(
    TestSectionRecord,
    (uint64_t, offset),
    (uint64_t, length)
);

/* File header */
PL_RECORD_STRUCT(
    TestFileHeader,
    (TestRecordVersion, version),
    (TestSectionRecord, dataSection),
    (TestSectionRecord, metaSection)
);

/* Define our codecs */
auto versionCodec = (
    ("record_compat_version"     | codecs::uint8  ) &
    ("record_feature_version"    | codecs::uint8  )
).as<TestRecordVersion>();

auto sectionCodec = (
    /* The base uintXX codecs assume big endian serialization; the `L' variants assume little endian */
    ("section_offset"            | codecs::uint64L  ) &
    ("section_length"            | codecs::uint64L  )
).as<TestSectionRecord>();

const auto fileMagic = ByteVector::Bytes("magic", 5, false);
auto headerCodec = (
    ("magic"                     | constant(fileMagic)    ) >>
    ("file_version"              | versionCodec           ) &
    ("data_sect"                 | sectionCodec           ) &
    ("meta_sect"                 | sectionCodec           )
).as<TestFileHeader>();

/* Verify round-trip encoding and decoding of TestFileHeader instances */
auto header = TestFileHeader(TestRecordVersion(1, 2), TestSectionRecord(10, 20), TestSectionRecord(30, 40));
XCTAssertRoundTrip(headerCodec, header);
```

More examples of specific codecs can be found in `src/codec_tests.mm`.

# License

`plcodec` is distributed under an MIT license.  See LICENSE for more details.
