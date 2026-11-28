// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <string>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/hkdf.hpp>

using namespace kth;

namespace {

data_chunk bytes(std::string const& text) {
    auto const out = decode_base16(text);
    REQUIRE(out);
    return *out;
}

template <size_t Size>
byte_array<Size> fixed(std::string const& text) {
    auto const out = decode_base16<Size>(text);
    REQUIRE(out);
    return *out;
}

} // namespace

TEST_CASE("hkdf sha256 matches RFC 5869 appendix A", "[hkdf]") {
    struct vector {
        char const* input_key;
        char const* salt;
        char const* info;
        size_t length;
        char const* output;
    };
    // Test cases 1 to 3 (SHA-256).
    vector const vectors[] = {
        {"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
         "000102030405060708090a0b0c",
         "f0f1f2f3f4f5f6f7f8f9",
         42,
         "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"},
        {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",
         "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
         "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
         82,
         "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87"},
        {"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
         "",
         "",
         42,
         "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"},
    };
    for (auto const& v : vectors) {
        CAPTURE(v.output);
        auto const prk = hkdf_sha256_extract(bytes(v.salt), bytes(v.input_key));
        auto const output = hkdf_sha256_expand(prk, bytes(v.info), v.length);
        REQUIRE(output);
        CHECK(encode_base16(*output) == v.output);
    }
}

TEST_CASE("hkdf sha256 refuses more than 255 blocks of output", "[hkdf]") {
    hash_digest const prk{};
    CHECK(hkdf_sha256_expand(prk, {}, 255 * 32));
    CHECK_FALSE(hkdf_sha256_expand(prk, {}, 255 * 32 + 1));
    auto const none = hkdf_sha256_expand(prk, {}, 0);
    REQUIRE(none);
    CHECK(none->empty());
}

TEST_CASE("hkdf sha256 pair is the first 64 bytes with empty info", "[hkdf]") {
    // Noise's HKDF(ck, ikm) for two outputs: HMAC(HMAC(ck, ikm), 0x01), then
    // HMAC(HMAC(ck, ikm), first || 0x02).
    auto const chaining_key = fixed<32>("56ddb6161ea73531ce31d9e4eb60b63c0900a889c6f8245ef6f185a443c5341a");
    auto const input = bytes("df9dc9469ff2c5ad549a7b71f7ade014c8a21cd431ee992e402ab2617b77f67e0f");
    auto const [first, second] = hkdf_sha256_pair(chaining_key, input);
    CHECK(encode_base16(first) + encode_base16(second) ==
        "e0ece8697a8fbfa7aa3576c8941132c7cadae2f80ff054043e13db373b0482d7d42605ae4c65f2aaf440a21e4c080bec95febf04100b14207291294b5dfdb661");
}

TEST_CASE("hkdf sha256 pair takes an empty input key", "[hkdf]") {
    // Noise's Split: HKDF(ck, zerolen). The empty key comes as a span that
    // may hold a null pointer, which must never reach the HMAC's memcpy.
    // The expected keys were computed with Python's hmac module.
    auto const chaining_key = fixed<32>("56ddb6161ea73531ce31d9e4eb60b63c0900a889c6f8245ef6f185a443c5341a");
    auto const expected = "94751789db524453a802d8b2625383032593e301035e1a68dda67ac07dea0ca4b00a505be8994187de5e7dccd49eb67e2e24a180428af5fbd42630721fac8ace";
    auto const [first, second] = hkdf_sha256_pair(chaining_key, byte_span{});
    CHECK(encode_base16(first) + encode_base16(second) == expected);
    data_chunk const empty;
    auto const [again, other] = hkdf_sha256_pair(chaining_key, empty);
    CHECK(encode_base16(again) + encode_base16(other) == expected);
    CHECK(hkdf_sha256_extract(chaining_key, {}) == hkdf_sha256_extract(chaining_key, empty));
}
