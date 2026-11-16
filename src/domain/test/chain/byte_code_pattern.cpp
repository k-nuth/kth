// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/domain/chain/byte_code_pattern.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

using namespace kth;
using namespace kth::domain::chain;

// The vectors come from a separate implementation of the same rules: a run of
// pushes becomes its count (OP_1 to OP_16, then a script number), OP_1NEGATE
// and OP_1 to OP_16 count as pushes of their value, OP_RESERVED does not, and a
// script that ends inside a push keeps its last item from the byte before the
// point where reading stopped.
TEST_CASE("make_byte_code_pattern replaces each run of pushes by its count", "[chain][byte code pattern]") {
    struct vector {
        char const* name;
        char const* script;
        char const* pattern;
        char const* fingerprint;
        std::vector<char const*> data;
        bool error;
    };
    std::string sig;
    for (int i = 0; i < 71; ++i) sig += "30";
    std::string key;
    for (int i = 0; i < 33; ++i) key += "02";
    std::vector<vector> const vectors{
        {"p2pkh", "76a914000102030405060708090a0b0c0d0e0f1011121388ac", "76a95188ac",
         "516879464c7ddd10dc73a3ecb9708b0fd24343287a16387dd6b961363b87714b",
         {"000102030405060708090a0b0c0d0e0f10111213"}, false},
        {"seventeen pushes", "5151515151515151515151515151515151", "0111",
         "26e7f98cfafee5b213726e22632923bf31bf3e988233235f8f5ca5466b3ac0ed",
         std::vector<char const*>(17, "01"), false},
        {"OP_1NEGATE, OP_RESERVED, OP_ADD", "4f5093", "515093",
         "501bc8605e327d6b7178bcb36fa190e11666269c36cd27117803248c2d135b0a", {"81"}, false},
        {"OP_0", "00", "51",
         "4ae81572f06e1b88fd5ced7a1a000945432e83e1551e6f721ee9c00b8cc33260", {""}, false},
        {"empty", "", "",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", {}, false},
        {"OP_PUSHDATA1 past the end", "4c05aa", "",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", {"05aa"}, true},
        {"OP_PUSHDATA2 without its length", "4d01", "",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", {"4d01"}, true},
    };
    for (auto const& v : vectors) {
        CAPTURE(v.name);
        auto const script = decode_base16(std::string_view{v.script});
        REQUIRE(script);
        auto const parsed = make_byte_code_pattern(*script);
        CHECK(encode_base16(parsed.pattern) == v.pattern);
        CHECK(encode_base16(sha256_hash(parsed.pattern)) == v.fingerprint);
        REQUIRE(parsed.data.size() == v.data.size());
        for (size_t i = 0; i < v.data.size(); ++i) {
            CHECK(encode_base16(parsed.data[i]) == v.data[i]);
        }
        CHECK(parsed.error == v.error);
    }

    // A signature and a public key: one run of two pushes.
    auto const script = decode_base16("47" + sig + "21" + key);
    REQUIRE(script);
    auto const parsed = make_byte_code_pattern(*script);
    CHECK(encode_base16(parsed.pattern) == "52");
    CHECK(encode_base16(sha256_hash(parsed.pattern)) ==
        "8c2574892063f995fdf756bce07f46c1a5193e54cd52837ed91e32008ccf41ac");
    REQUIRE(parsed.data.size() == 2);
    CHECK(encode_base16(parsed.data[0]) == sig);
    CHECK(encode_base16(parsed.data[1]) == key);
}

TEST_CASE("byte code pattern preserves explicit push encodings and truncated suffixes", "[chain][byte code pattern]") {
    for (auto const hex : {"4c00", "4d0000", "4e00000000"}) {
        auto const bytes = decode_base16(std::string_view{hex});
        REQUIRE(bytes);
        auto const parsed = make_byte_code_pattern(*bytes);
        CHECK_FALSE(parsed.error);
        CHECK(parsed.pattern == data_chunk{0x51});
        REQUIRE(parsed.data.size() == 1);
        CHECK(parsed.data.front().empty());
    }
    struct vector {
        char const* script;
        char const* suffix;
    };
    for (auto const& v : {vector{"02aa", "02aa"}, vector{"4c", "4c"},
            vector{"4d0200aa", "00aa"}, vector{"4e02000000aa", "00aa"},
            vector{"4e010203", "4e010203"}}) {
        auto const bytes = decode_base16(std::string_view{v.script});
        REQUIRE(bytes);
        auto const parsed = make_byte_code_pattern(*bytes);
        CHECK(parsed.error);
        CHECK(parsed.pattern.empty());
        REQUIRE(parsed.data.size() == 1);
        CHECK(encode_base16(parsed.data.front()) == v.suffix);
    }
    for (size_t const count : {size_t(16), size_t(17), size_t(127), size_t(128), size_t(256)}) {
        auto const parsed = make_byte_code_pattern(data_chunk(count, 0x51));
        auto const expected = count == 16 ? "60" : count == 17 ? "0111" :
            count == 127 ? "017f" : count == 128 ? "028000" : "020001";
        CHECK_FALSE(parsed.error);
        CHECK(encode_base16(parsed.pattern) == expected);
        CHECK(parsed.data.size() == count);
    }
    // Pattern extraction is structural: it must not impose the interpreter's
    // legacy 520-byte push limit on the shared decoder.
    data_chunk large{0x4e, 0x00, 0x00, 0x01, 0x00};
    large.resize(5 + 65536, 0xaa);
    auto const parsed = make_byte_code_pattern(large);
    CHECK_FALSE(parsed.error);
    CHECK(parsed.pattern == data_chunk{0x51});
    REQUIRE(parsed.data.size() == 1);
    CHECK(parsed.data.front() == data_chunk(65536, 0xaa));
}
