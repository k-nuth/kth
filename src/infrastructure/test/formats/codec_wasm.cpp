// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/bitcoin.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/formats/base_85.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace kth;
static unsigned checks = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x)) {                                                                                                    \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                  \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
static_assert(1.5_bitcoin == 150000000);
static_assert("0.00000001"_bitcoin == 1_satoshi);
static_assert(encode_base16("deadbeef"_base16) == "deadbeef");
static_assert(encode_base64("Zm9v"_base64) == "Zm9v");
static_assert(encode_base58(byte_array<3>{0, 0, 97}) == "112g");
static_assert(decode_base58("112g")->size() == 3);
static_assert(decode_base85<8>("HelloWorld")->front() == 0x86);
static_assert(encode_base85(*decode_base85<8>("HelloWorld")) == "HelloWorld");
static_assert("42"_base10 == 42);
static_assert("112g"_base58 == byte_array<3>{0, 0, 97});
static_assert("HelloWorld"_base85 == *decode_base85<8>("HelloWorld"));
static_assert(decode_base64<3>("Zm9v") == "Zm9v"_base64);

int main() {
    for (size_t n = 0; n <= 128; ++n) {
        data_chunk data(n);
        for (size_t i = 0; i < n; ++i)
            data[i] = uint8_t(i * 37 + n);
        auto text58 = encode_base58(data);
        CHECK(decode_base58(text58) == data);
        if (n % 4 == 0) {
            auto text85 = encode_base85(data);
            CHECK(text85);
            CHECK(decode_base85(*text85) == data);
        }
    }

    CHECK(decode_base10("42") == "42"_base10);
    CHECK(decode_base58<3>("112g") == "112g"_base58);
    CHECK(decode_base85<8>("HelloWorld") == "HelloWorld"_base85);
    CHECK(decode_base64<3>("Zm9v") == "Zm9v"_base64);
    CHECK(decode_base64<2>("Zm9v").error() == base64_errc::size_mismatch);
    CHECK(bitcoin_to_satoshi("1.5") == 150000000);
    CHECK(satoshi_to_bitcoin(1) == "0.00000001");
    CHECK(decode_base10("18446744073709551616").error() == base10_errc::overflow);
    for (size_t n = 0; n < 2048; ++n) {
        data_chunk data(n);
        for (size_t i = 0; i < n; ++i)
            data[i] = uint8_t(i * 37 + n);
        auto const encoded = encode_base64(data);
        CHECK(encoded == detail::encode_base64_scalar(data));
        auto decoded = decode_base64(encoded);
        CHECK(decoded);
        CHECK(*decoded == data);
        std::vector<uint8_t> buffer(n + 2, 0xa5);
        auto count = decode_base64(encoded, std::span(buffer).subspan(1, n));
        CHECK(count == n);
        CHECK(buffer.front() == 0xa5);
        CHECK(buffer.back() == 0xa5);
        if (n > 0)
            CHECK(!decode_base64(encoded, std::span(buffer).subspan(1, n - 1)));
        auto const hex = encode_base16(data);
        auto unhex = decode_base16(hex);
        CHECK(unhex);
        CHECK(*unhex == data);
    }
    for (auto const *input : {"", "Zg", "Zg==", "Zh==", "Zm8", "Zm8=", " Zm9v\n", "Zg=", "Z", "====", "AA=A", "AA==A",
             "Zm9v=", "Zg==\v", "Zg==\f"}) {
        auto const a = decode_base64(input);
        auto const b = detail::decode_base64_scalar(input);
        CHECK(bool(a) == bool(b));
        if (a && b)
            CHECK(*a == *b);
    }
    for (size_t n : {size_t(15), size_t(16), size_t(17), size_t(31), size_t(32), size_t(33), size_t(1024)}) {
        data_chunk data(n, 0xff);
        auto text = encode_base64(data);
        for (size_t at = 0; at < text.size(); ++at) {
            auto changed = text;
            changed[at] = '!';
            CHECK(!decode_base64(changed));
        }
    }
    std::printf("codec smoke: %u checks passed\n", checks);
}
