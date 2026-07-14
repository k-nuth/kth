// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include <test_helpers.hpp>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

using namespace kth;

// Start Test Suite: pseudo random tests

TEST_CASE("pseudo random concept admits integers and rejects the unsafe", "[pseudo random]") {
    static_assert(randomizable<uint8_t>);
    static_assert(randomizable<uint64_t>);
    static_assert(randomizable<int32_t>);

    // Only 0 and 1 are valid bool object representations, so filling one with a
    // random byte is undefined behaviour.
    static_assert( ! randomizable<bool>);

    // Padding would be filled with entropy that equality never looks at.
    struct padded { uint8_t a; uint64_t b; };
    static_assert( ! randomizable<padded>);

    static_assert( ! randomizable<double>);
    static_assert( ! randomizable<uint64_t*>);

    // Arrays are drawable exactly when the element is, and it composes.
    static_assert(randomizable<std::array<uint8_t, 32>>);
    static_assert(randomizable<byte_array<16>>);
    static_assert(randomizable<std::array<std::array<uint8_t, 4>, 2>>);
    // std::array<bool, N> has no padding, so a "trivially copyable, unique
    // representation" test would wrongly admit it.
    static_assert( ! randomizable<std::array<bool, 4>>);
    static_assert( ! randomizable<std::array<double, 4>>);

    static_assert(randomizable_range<data_chunk>);
    static_assert(randomizable_range<std::array<uint64_t, 2>>);
    static_assert( ! randomizable_range<std::set<uint64_t>>);   // not contiguous
    REQUIRE(true);
}

TEST_CASE("pseudo random check_available succeeds on a working system", "[pseudo random]") {
    // The startup probe. If this ever fails on a build machine, every other
    // test in this file is meaningless -- and so is the node.
    REQUIRE( ! pseudo_random::check_available());
}

TEST_CASE("pseudo random generate returns distinct values", "[pseudo random]") {
    // Not a randomness test -- a wiring test. If generate<> returned the raw
    // stack value, or filled only the first byte, this collides immediately.
    std::set<uint64_t> seen;
    for (size_t i = 0; i < 64; ++i) {
        seen.insert(pseudo_random::generate<uint64_t>());
    }
    REQUIRE(seen.size() == 64u);
}

TEST_CASE("pseudo random generate covers the whole width", "[pseudo random]") {
    // Every byte lane must vary, which catches a short fill.
    std::array<uint8_t, sizeof(uint64_t)> seen_high{};
    for (size_t i = 0; i < 256; ++i) {
        auto const value = pseudo_random::generate<uint64_t>();
        for (size_t byte = 0; byte < sizeof(uint64_t); ++byte) {
            seen_high[byte] |= uint8_t((value >> (byte * 8)) & 0xff);
        }
    }
    for (auto const lane : seen_high) {
        REQUIRE(lane != 0u);
    }
}

TEST_CASE("pseudo random fill writes every byte of a range", "[pseudo random]") {
    // A run this long is all-zero with probability 256^-64.
    data_chunk chunk(64, 0x00);
    pseudo_random::fill(chunk);
    REQUIRE(std::ranges::any_of(chunk, [](auto b) { return b != 0x00; }));

    // Re-filling produces a different result.
    auto const first = chunk;
    pseudo_random::fill(chunk);
    REQUIRE(first != chunk);
}

TEST_CASE("pseudo random fill handles an empty range", "[pseudo random]") {
    data_chunk empty;
    REQUIRE_NOTHROW(pseudo_random::fill(empty));
    REQUIRE(empty.empty());
}

TEST_CASE("pseudo random fill handles a request larger than one syscall", "[pseudo random]") {
    // getrandom short-reads above 256 bytes and getentropy refuses outright, so
    // the loop in fill() is what makes this work.
    data_chunk chunk(64 * 1024, 0x00);
    pseudo_random::fill(chunk);

    // The tail is what a missing loop would leave untouched.
    auto const tail = data_chunk(chunk.end() - 512, chunk.end());
    REQUIRE(std::ranges::any_of(tail, [](auto b) { return b != 0x00; }));
}

TEST_CASE("pseudo random generates an array whole", "[pseudo random]") {
    auto const salt = pseudo_random::generate<byte_array<32>>();
    REQUIRE(std::ranges::any_of(salt, [](auto b) { return b != 0u; }));
    REQUIRE(salt != pseudo_random::generate<byte_array<32>>());
}

TEST_CASE("pseudo random fill works on a non-byte range", "[pseudo random]") {
    std::array<uint64_t, 4> salt{};
    pseudo_random::fill(salt);
    REQUIRE(std::ranges::any_of(salt, [](auto v) { return v != 0u; }));
}

TEST_CASE("pseudo random wipe zeroes a range", "[pseudo random]") {
    auto salt = pseudo_random::generate<byte_array<32>>();
    REQUIRE(std::ranges::any_of(salt, [](auto b) { return b != 0u; }));

    pseudo_random::wipe(salt);
    REQUIRE(std::ranges::all_of(salt, [](auto b) { return b == 0u; }));

    data_chunk chunk(64);
    pseudo_random::fill(chunk);
    pseudo_random::wipe(chunk);
    REQUIRE(std::ranges::all_of(chunk, [](auto b) { return b == 0u; }));

    // NOTE: no unit test can show that the wipe survives the optimizer. Reading
    // the bytes back is exactly what stops the store being dead, so a plain
    // std::fill would pass this too. The guarantee comes from wipe_bytes being
    // an out-of-line call to explicit_bzero (or a memset behind a barrier); the
    // evidence is in the disassembly, quoted in the PR.
}

TEST_CASE("pseudo random wipe handles an empty range", "[pseudo random]") {
    data_chunk empty;
    REQUIRE_NOTHROW(pseudo_random::wipe(empty));
}

// End Test Suite
