// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <random>
#include <vector>

#include <kth/infrastructure/math/hash_batch.hpp>

using namespace kth;

// bitcoin_hash_batch has to give, for every message, exactly what bitcoin_hash
// gives. These go through whichever way of hashing this machine selects, so on
// a processor with the SHA extensions they exercise the messages-in-step path.

namespace {

std::vector<data_chunk> random_messages(std::vector<size_t> const& sizes, uint32_t seed) {
    std::mt19937 gen(seed);
    std::vector<data_chunk> messages;
    for (auto const size : sizes) {
        data_chunk m(size);
        for (auto& b : m) b = uint8_t(gen());
        messages.push_back(std::move(m));
    }
    return messages;
}

void check_equals_one_by_one(std::vector<data_chunk> const& messages) {
    std::vector<byte_span> spans(messages.begin(), messages.end());
    std::vector<hash_digest> out(messages.size(), hash_digest{0x5a});
    bitcoin_hash_batch(spans, out);
    for (size_t i = 0; i < messages.size(); ++i) {
        INFO("message " << i << " of " << messages.size() << ", " << messages[i].size() << " bytes");
        REQUIRE(out[i] == bitcoin_hash(messages[i]));
    }
}

} // namespace

TEST_CASE("hash batch: says how it hashes", "[hash_batch]") {
    CHECK_FALSE(bitcoin_hash_batch_implementation().empty());
}

TEST_CASE("hash batch: no messages", "[hash_batch]") {
    std::vector<byte_span> const none;
    std::vector<hash_digest> out;
    bitcoin_hash_batch(none, out);
    CHECK(out.empty());
}

TEST_CASE("hash batch: an empty message", "[hash_batch]") {
    check_equals_one_by_one({data_chunk{}});
    // Among others, and more than one of them.
    check_equals_one_by_one({data_chunk{1, 2, 3}, data_chunk{}, data_chunk{}, data_chunk{4}, data_chunk{}});
}

TEST_CASE("hash batch: every length around the block and padding boundaries", "[hash_batch]") {
    // 55 and 56 bytes are where the padding starts needing a second block, 64
    // where a whole block appears, and so on for the next blocks.
    std::vector<size_t> sizes;
    for (size_t size = 0; size <= 300; ++size) sizes.push_back(size);
    check_equals_one_by_one(random_messages(sizes, 1));
}

TEST_CASE("hash batch: any number of messages", "[hash_batch]") {
    // Fewer than are hashed in step, exactly as many, and one or two more.
    for (size_t count : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 15u, 16u, 17u, 63u, 64u, 65u}) {
        std::vector<size_t> sizes(count);
        for (size_t i = 0; i < count; ++i) sizes[i] = 20 + 37 * i;
        check_equals_one_by_one(random_messages(sizes, uint32_t(count)));
    }
}

TEST_CASE("hash batch: messages of very different lengths side by side", "[hash_batch]") {
    // A long message keeps going while short ones come and go beside it.
    std::vector<size_t> sizes{100'000, 1, 2, 3, 4, 5, 64, 65, 127, 128, 129, 0, 7, 50'000, 9, 10, 11, 12, 13, 250'000};
    for (size_t i = 0; i < 200; ++i) sizes.push_back(i * 7 % 191);
    check_equals_one_by_one(random_messages(sizes, 7));
}

TEST_CASE("hash batch: many messages of transaction size", "[hash_batch]") {
    std::mt19937 gen(42);
    std::vector<size_t> sizes(5000);
    for (auto& size : sizes) size = 60 + gen() % 900;
    check_equals_one_by_one(random_messages(sizes, 43));
}

TEST_CASE("hash batch: a known answer", "[hash_batch]") {
    // The double SHA-256 of "hello".
    data_chunk const hello{'h', 'e', 'l', 'l', 'o'};
    std::vector<byte_span> const spans{hello, hello, hello, hello, hello};
    std::vector<hash_digest> out(spans.size());
    bitcoin_hash_batch(spans, out);
    auto const expected = decode_base16("9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50");
    REQUIRE(expected);
    for (auto const& hash : out) {
        CHECK(std::equal(hash.begin(), hash.end(), expected->begin()));
    }
}
