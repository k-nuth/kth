// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <string>

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/murmur_hash.hpp>

using namespace kth;

// Usable in constant expressions.
static_assert(murmur_hash3(0x00000000, {}) == 0x00000000);
static_assert(murmur_hash3(0xfba4c795, {}) == 0x6a396f08);
static_assert(murmur_hash3(0x00000000, std::array<uint8_t, 5>{0x00, 0x11, 0x22, 0x33, 0x44}) == 0xe2301fa8);

TEST_CASE("murmur hash 3 matches its known answers", "[infrastructure][murmur_hash]") {
    // Every tail length, zero to three bytes, with and without a whole word
    // before it.
    struct answer { uint32_t hash; uint32_t seed; char const* data; };
    for (auto const& [hash, seed, data] : {
            answer{0x00000000, 0x00000000, ""}, answer{0x6a396f08, 0xfba4c795, ""},
            answer{0x81f16f39, 0xffffffff, ""}, answer{0x514e28b7, 0x00000000, "00"},
            answer{0xea3f0b17, 0xfba4c795, "00"}, answer{0xfd6cf10d, 0x00000000, "ff"},
            answer{0x16c6b7ab, 0x00000000, "0011"}, answer{0x8eb51c3d, 0x00000000, "001122"},
            answer{0xb4471bf8, 0x00000000, "00112233"}, answer{0xe2301fa8, 0x00000000, "0011223344"},
            answer{0xfc2e4a15, 0x00000000, "001122334455"}, answer{0xb074502c, 0x00000000, "00112233445566"},
            answer{0x8034d2a0, 0x00000000, "0011223344556677"},
            answer{0xb4698def, 0x00000000, "001122334455667788"}}) {
        CAPTURE(seed, data);
        auto const bytes = decode_base16(std::string{data});
        REQUIRE(bytes);
        CHECK(murmur_hash3(seed, *bytes) == hash);
    }
}
