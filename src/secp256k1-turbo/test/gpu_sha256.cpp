// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The GPU hashing kernels. The device code compiled for the host is checked
// against SHA-256 itself on every machine; the devices are checked against the
// host where there is one.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <secp256k1_turbo/gpu_sha256.hpp>

#include "gpu_backend.hpp"
#include "sha256.hpp"

using namespace secp256k1_turbo;

namespace {

using hash = std::array<std::uint8_t, 32>;

std::vector<std::uint8_t> random_bytes(std::size_t size, std::uint64_t seed) {
    std::vector<std::uint8_t> out(size);
    std::uint64_t x = seed * 0x9e3779b97f4a7c15ULL + 1;
    for (auto& b : out) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        b = static_cast<std::uint8_t>(x >> 24);
    }
    return out;
}

// SHA-256 twice, with this library's CPU implementation.
hash double_sha256(std::uint8_t const* data, std::size_t size) {
    hash first{}, second{};
    sha256(data, size, first.data());
    sha256(first.data(), first.size(), second.data());
    return second;
}

// The merkle root as the node computes it on the CPU: level by level, the
// last hash of an odd level paired with itself.
hash reference_merkle_root(std::vector<std::uint8_t> level, bool& mutated) {
    mutated = false;
    std::size_t count = level.size() / 32;
    while (count > 1) {
        std::vector<std::uint8_t> above(32 * ((count + 1) / 2));
        for (std::size_t i = 0; i < (count + 1) / 2; ++i) {
            std::uint8_t pair[64];
            std::memcpy(pair, &level[64 * i], 32);
            bool const has_right = 2 * i + 1 < count;
            std::memcpy(pair + 32, &level[has_right ? 64 * i + 32 : 64 * i], 32);
            if (has_right && std::memcmp(pair, pair + 32, 32) == 0) mutated = true;
            auto const node = double_sha256(pair, 64);
            std::memcpy(&above[32 * i], node.data(), 32);
        }
        level = std::move(above);
        count = (count + 1) / 2;
    }
    hash root{};
    std::memcpy(root.data(), level.data(), 32);
    return root;
}

// Sizes around every boundary of the tree: one hash, odd and even levels,
// powers of two and their neighbours.
std::size_t const tree_sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 33, 100, 127, 128, 129, 1000, 4097};

} // namespace

TEST_CASE("gpu double SHA-256 on the host matches SHA-256 twice", "[gpu][sha256]") {
    auto const in = random_bytes(64 * 300, 1);
    std::vector<std::uint8_t> out(32 * 300);
    gpu::sha256d64_host(in.data(), 300, out.data());
    for (std::size_t i = 0; i < 300; ++i) {
        auto const expected = double_sha256(&in[64 * i], 64);
        REQUIRE(std::memcmp(&out[32 * i], expected.data(), 32) == 0);
    }
}

TEST_CASE("gpu merkle root on the host matches the level-by-level root", "[gpu][sha256]") {
    for (std::size_t const count : tree_sizes) {
        auto const leaves = random_bytes(32 * count, 100 + count);
        bool expected_mutated = true;
        auto const expected = reference_merkle_root(leaves, expected_mutated);
        hash root{};
        bool mutated = true;
        gpu::merkle_root_host(leaves.data(), count, root.data(), &mutated);
        INFO("hashes: " << count);
        REQUIRE(root == expected);
        REQUIRE_FALSE(expected_mutated);
        REQUIRE_FALSE(mutated);
    }
}

TEST_CASE("gpu merkle root on the host reports equal siblings at any level", "[gpu][sha256]") {
    // Equal leaves side by side.
    {
        auto leaves = random_bytes(32 * 6, 7);
        std::memcpy(&leaves[32 * 3], &leaves[32 * 2], 32);
        bool expected_mutated = false, mutated = false;
        auto const expected = reference_merkle_root(leaves, expected_mutated);
        hash root{};
        gpu::merkle_root_host(leaves.data(), 6, root.data(), &mutated);
        REQUIRE(root == expected);
        REQUIRE(expected_mutated);
        REQUIRE(mutated);
    }
    // Equal subtrees: leaves 4..7 repeat 0..3, so the pair is equal two levels up.
    {
        auto leaves = random_bytes(32 * 8, 8);
        std::memcpy(&leaves[32 * 4], &leaves[0], 32 * 4);
        bool expected_mutated = false, mutated = false;
        auto const expected = reference_merkle_root(leaves, expected_mutated);
        hash root{};
        gpu::merkle_root_host(leaves.data(), 8, root.data(), &mutated);
        REQUIRE(root == expected);
        REQUIRE(expected_mutated);
        REQUIRE(mutated);
    }
    // The last hash of an odd level paired with itself is not a mutation, and
    // equal hashes that are not siblings are not one either.
    {
        auto leaves = random_bytes(32 * 5, 9);
        std::memcpy(&leaves[32 * 2], &leaves[32 * 1], 32);   // 1 and 2: neighbours, not siblings
        bool mutated = true;
        hash root{};
        gpu::merkle_root_host(leaves.data(), 5, root.data(), &mutated);
        REQUIRE_FALSE(mutated);
    }
}

namespace {

// Messages laid out in one buffer the way transactions are in a block: one
// after another, with a few bytes that belong to none of them in between, so
// they start at every alignment. `order` then lists them shuffled.
struct message_set {
    std::vector<std::uint8_t> data;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> sizes;
};

message_set make_messages(std::vector<std::uint32_t> const& lengths, std::uint64_t seed) {
    message_set set;
    std::size_t total = 0;
    for (std::size_t i = 0; i < lengths.size(); ++i) total += lengths[i] + (i % 4);
    set.data = random_bytes(total + 3, seed);
    std::size_t at = 0;
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        at += i % 4;                 // a gap, and a start at every alignment
        set.offsets.push_back(at);
        set.sizes.push_back(lengths[i]);
        at += lengths[i];
    }
    // Not in the order they lie in: the last third first.
    std::size_t const third = lengths.size() / 3;
    std::rotate(set.offsets.begin(), set.offsets.end() - std::ptrdiff_t(third), set.offsets.end());
    std::rotate(set.sizes.begin(), set.sizes.end() - std::ptrdiff_t(third), set.sizes.end());
    return set;
}

// Every length across the boundaries that matter: where the padding needs a
// second block (56), a whole block (64), and the same in the next blocks.
std::vector<std::uint32_t> every_length_to(std::uint32_t last) {
    std::vector<std::uint32_t> lengths;
    for (std::uint32_t n = 0; n <= last; ++n) lengths.push_back(n);
    return lengths;
}

} // namespace

TEST_CASE("gpu double SHA-256 of messages on the host matches SHA-256 twice", "[gpu][sha256]") {
    auto lengths = every_length_to(300);
    for (std::uint32_t const longer : {1000u, 4095u, 4096u, 4097u, 65536u, 100001u}) lengths.push_back(longer);
    auto const set = make_messages(lengths, 21);
    std::vector<std::uint8_t> out(32 * set.sizes.size(), 0xEE);
    gpu::sha256d_messages_host(set.data.data(), set.data.size(), set.offsets.data(), set.sizes.data(),
                               set.sizes.size(), out.data());
    for (std::size_t i = 0; i < set.sizes.size(); ++i) {
        auto const expected = double_sha256(set.data.data() + set.offsets[i], set.sizes[i]);
        INFO("message of " << set.sizes[i] << " bytes at " << set.offsets[i]);
        REQUIRE(std::memcmp(&out[32 * i], expected.data(), 32) == 0);
    }
}

TEST_CASE("gpu double SHA-256 of messages on the host: none, and one at the very end", "[gpu][sha256]") {
    std::vector<std::uint8_t> out(32, 0xEE);
    gpu::sha256d_messages_host(nullptr, 0, nullptr, nullptr, 0, out.data());
    REQUIRE(out[0] == 0xEE);

    // A message that ends on the last byte of the buffer, at an odd start:
    // nothing past the buffer may be needed.
    auto const data = random_bytes(131, 5);
    std::uint64_t const offset = 3;
    std::uint32_t const size = 128;
    gpu::sha256d_messages_host(data.data(), data.size(), &offset, &size, 1, out.data());
    auto const expected = double_sha256(data.data() + 3, 128);
    REQUIRE(std::memcmp(out.data(), expected.data(), 32) == 0);
}

TEST_CASE("gpu hashing reports that it did nothing when there is no device", "[gpu][sha256]") {
    if (gpu_sha256::available()) { WARN("a device is present: skipped"); return; }
    auto const in = random_bytes(64 * 4, 3);
    std::vector<std::uint8_t> out(32 * 4, 0xAB);
    hash root;
    root.fill(0xAB);
    CHECK(gpu_sha256::device_name().empty());
    CHECK_FALSE(gpu_sha256::worth_it(1u << 20));
    CHECK_FALSE(gpu_sha256::sha256d64(out.data(), in.data(), 4));
    CHECK_FALSE(gpu_sha256::merkle_root(root.data(), in.data(), 8));
    // Nothing was written.
    for (auto const b : out) REQUIRE(b == 0xAB);
    for (auto const b : root) REQUIRE(b == 0xAB);
}

TEST_CASE("gpu hashing threshold: zero and off keep callers on the CPU", "[gpu][sha256]") {
    auto const before = gpu_sha256::min_batch();
    // Without a device nothing is worth sending, whatever the threshold says.
    CHECK((before != 0) == gpu_sha256::available());
    gpu_sha256::set_min_batch(0);
    CHECK(gpu_sha256::min_batch() == 0);
    CHECK_FALSE(gpu_sha256::worth_it(1u << 20));
    gpu_sha256::set_min_batch(100);
    CHECK(gpu_sha256::min_batch() == 100);
    CHECK_FALSE(gpu_sha256::worth_it(99));
    CHECK(gpu_sha256::worth_it(100) == gpu_sha256::available());
    gpu_sha256::set_min_batch(before);
}

TEST_CASE("gpu device hashing of messages matches the host", "[gpu][sha256][device]") {
    if ( ! gpu_sha256::available()) { WARN("no device: skipped"); return; }

    // Short messages only, long ones among them, and very many.
    std::vector<std::vector<std::uint32_t>> cases;
    cases.push_back(every_length_to(300));
    cases.push_back({0, 1, 55, 56, 63, 64, 65, 100001, 7, 250000, 3});
    std::vector<std::uint32_t> many(20000);
    for (std::size_t i = 0; i < many.size(); ++i) many[i] = std::uint32_t(60 + (i * 7919) % 900);
    cases.push_back(std::move(many));

    std::uint64_t seed = 31;
    for (auto const& lengths : cases) {
        auto const set = make_messages(lengths, seed++);
        std::size_t const n = set.sizes.size();
        std::vector<std::uint8_t> expected(32 * n), out(32 * n, 0xEE);
        gpu::sha256d_messages_host(set.data.data(), set.data.size(), set.offsets.data(), set.sizes.data(),
                                   n, expected.data());
        std::vector<std::uint8_t const*> pointers(n);
        for (std::size_t i = 0; i < n; ++i) pointers[i] = set.data.data() + set.offsets[i];
        if ( ! gpu_sha256::sha256d_messages(out.data(), pointers.data(), set.sizes.data(), n)) {
            // A device without this kernel yet: nothing was written.
            WARN("the device does not hash messages: skipped");
            REQUIRE(out[0] == 0xEE);
            return;
        }
        INFO("messages: " << n);
        REQUIRE(out == expected);
    }
}

TEST_CASE("gpu device hashing matches the host", "[gpu][sha256][device]") {
    if (!gpu_sha256::available()) { WARN("no GPU device: skipped"); return; }
    INFO("device: " << gpu_sha256::device_name());

    for (std::size_t const count : {std::size_t(1), std::size_t(2), std::size_t(63), std::size_t(64), std::size_t(65),
                                    std::size_t(1000), std::size_t(70001)}) {
        auto const in = random_bytes(64 * count, 500 + count);
        std::vector<std::uint8_t> expected(32 * count), got(32 * count);
        gpu::sha256d64_host(in.data(), count, expected.data());
        REQUIRE(gpu_sha256::sha256d64(got.data(), in.data(), count));
        INFO("inputs: " << count);
        REQUIRE(got == expected);
    }

    for (std::size_t const count : tree_sizes) {
        auto const leaves = random_bytes(32 * count, 900 + count);
        hash expected{}, root{};
        bool expected_mutated = true, mutated = true;
        gpu::merkle_root_host(leaves.data(), count, expected.data(), &expected_mutated);
        REQUIRE(gpu_sha256::merkle_root(root.data(), leaves.data(), count, &mutated));
        INFO("hashes: " << count);
        REQUIRE(root == expected);
        REQUIRE(mutated == expected_mutated);
    }

    // A large tree, and the same one with two equal siblings deep inside.
    auto leaves = random_bytes(32 * 300001, 77);
    hash expected{}, root{};
    bool expected_mutated = true, mutated = true;
    gpu::merkle_root_host(leaves.data(), 300001, expected.data(), &expected_mutated);
    REQUIRE(gpu_sha256::merkle_root(root.data(), leaves.data(), 300001, &mutated));
    REQUIRE(root == expected);
    REQUIRE_FALSE(mutated);

    std::memcpy(&leaves[32 * 200001], &leaves[32 * 200000], 32);
    gpu::merkle_root_host(leaves.data(), 300001, expected.data(), &expected_mutated);
    REQUIRE(gpu_sha256::merkle_root(root.data(), leaves.data(), 300001, &mutated));
    REQUIRE(root == expected);
    REQUIRE(expected_mutated);
    REQUIRE(mutated);

    // No input: a batch of nothing is done; a tree of nothing has no root.
    CHECK(gpu_sha256::sha256d64(root.data(), leaves.data(), 0));
    CHECK_FALSE(gpu_sha256::merkle_root(root.data(), leaves.data(), 0));
}
