// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: get block transactions tests
TEST_CASE("get block transactions constructor 2 always equals params", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions constructor 3 always equals params", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    auto hash_dup = hash;
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto indexes_dup = indexes;

    auto const instance = message::get_block_transactions::create(hash_dup, std::move(indexes_dup)).value();
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions constructor 4 always equals params", "[get block transactions]") {
    auto const value = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(value);
    REQUIRE(value == instance);
}

TEST_CASE("get block transactions constructor 5 always equals params", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    auto value = message::get_block_transactions::create(hash, indexes).value();
    message::get_block_transactions instance(std::move(value));
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions from data insufficient bytes failure", "[get block transactions]") {
    data_chunk const raw{0xab, 0xcd};
    message::get_block_transactions instance{};

    byte_reader reader(raw);
    auto result = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
}

TEST_CASE("get block transactions from data valid input success", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {16,
         32,
         37,
         44}).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::get_block_transactions::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}

TEST_CASE("get block transactions block hash accessor 1 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash accessor 2 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash setter 1 roundtrip success", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    message::get_block_transactions const empty;
    REQUIRE(hash != empty.block_hash());

    auto const instance = message::get_block_transactions::create(hash, {}).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions block hash setter 2 roundtrip success", "[get block transactions]") {
    hash_digest hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    auto dup = hash;
    message::get_block_transactions const empty;
    REQUIRE(hash != empty.block_hash());

    auto const instance = message::get_block_transactions::create(dup, {}).value();
    REQUIRE(hash == instance.block_hash());
}

TEST_CASE("get block transactions indexes accessor 1 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes accessor 2 always returns initialized value", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto const instance = message::get_block_transactions::create(hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes are set at construction", "[get block transactions]") {
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};

    message::get_block_transactions const empty;
    REQUIRE(indexes != empty.indexes());

    auto const instance = message::get_block_transactions::create(null_hash, indexes).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions indexes are set at construction, by move", "[get block transactions]") {
    std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto dup = indexes;

    message::get_block_transactions const empty;
    REQUIRE(indexes != empty.indexes());

    auto const instance = message::get_block_transactions::create(null_hash, std::move(dup)).value();
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions operator assign equals always matches equivalent", "[get block transactions]") {
    hash_digest const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    const std::vector<uint64_t> indexes = {1u, 3454u, 4234u, 75123u, 455323u};
    auto value = message::get_block_transactions::create(hash, indexes).value();


    message::get_block_transactions instance;

    instance = std::move(value);
    REQUIRE(hash == instance.block_hash());
    REQUIRE(indexes == instance.indexes());
}

TEST_CASE("get block transactions operator boolean equals duplicates returns true", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions operator boolean equals differs returns false", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get block transactions operator boolean not equals duplicates returns false", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("get block transactions operator boolean not equals differs returns true", "[get block transactions]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 3454u, 4234u, 75123u, 455323u}).value();

    message::get_block_transactions instance;
    REQUIRE(instance != expected);
}

TEST_CASE("get block transactions indexes round trip as differentials", "[get block transactions][bip152]") {
    auto const expected = message::get_block_transactions::create(
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
        {1u, 2u, 5u}).value();
    auto const version = message::get_block_transactions::version_minimum;
    auto const data = kth::to_data_chunk(expected, version);

    // Absolute 1, 2, 5 encode as 1, then (2 - 1 - 1) = 0, then (5 - 2 - 1) = 2.
    REQUIRE(data.size() == null_hash.size() + 4u);
    REQUIRE(data.at(null_hash.size()) == 3u);
    REQUIRE(data.at(null_hash.size() + 1) == 1u);
    REQUIRE(data.at(null_hash.size() + 2) == 0u);
    REQUIRE(data.at(null_hash.size() + 3) == 2u);

    byte_reader reader(data);
    auto const parsed = message::get_block_transactions::from_data(reader, version);
    REQUIRE(parsed);
    REQUIRE(parsed->indexes() == std::vector<uint64_t>{1u, 2u, 5u});
}

TEST_CASE("get block transactions rejects indexes that cannot be differential", "[get block transactions][bip152]") {
    hash_digest const hash{};
    auto const decreasing = message::get_block_transactions::create(hash, {5u, 4u});
    REQUIRE_FALSE(decreasing);
    REQUIRE(decreasing.error() == error::invalid_size);
    auto const duplicate = message::get_block_transactions::create(hash, {5u, 5u});
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error() == error::invalid_size);

    auto const past = message::get_block_transactions::create(hash, {max_compact_transactions()});
    REQUIRE_FALSE(past);
    REQUIRE(past.error() == error::invalid_size);

    data_chunk const short_buffer(null_hash.size() + 1, 0);
    data_chunk claimed = short_buffer;
    claimed.back() = 1;
    byte_reader short_reader(claimed);
    auto const missing_index = message::get_block_transactions::from_data(short_reader,
        message::get_block_transactions::version_minimum);
    REQUIRE_FALSE(missing_index);
    REQUIRE(missing_index.error() == error::invalid_size);

    // First index 0, second differential uint64 max: the absolute sum overflows.
    data_chunk overflow(null_hash.size() + 1 + 1 + 9, 0);
    overflow.at(null_hash.size()) = 2;
    overflow.at(null_hash.size() + 2) = 0xff;
    for (size_t i = 0; i < 8; ++i) {
        overflow.at(null_hash.size() + 3 + i) = 0xff;
    }
    byte_reader overflow_reader(overflow);
    auto const overflowed = message::get_block_transactions::from_data(overflow_reader,
        message::get_block_transactions::version_minimum);
    REQUIRE_FALSE(overflowed);
    REQUIRE(overflowed.error() == error::invalid_size);
}

TEST_CASE("get block transactions bounds counts before allocation", "[get block transactions][bip152]") {
    data_chunk raw(hash_size + 9, 0);
    byte_writer writer(raw);
    REQUIRE(writer.write_hash(null_hash));
    REQUIRE(writer.write_variable_little_endian(max_compact_transactions() + 1));
    byte_reader reader(raw);
    auto const parsed = message::get_block_transactions::from_data(reader,
        message::get_block_transactions::version_minimum);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error() == error::invalid_size);
}

TEST_CASE("get block transactions checks differential widths and truncation", "[get block transactions][bip152]") {
    auto const version = message::get_block_transactions::version_minimum;
    auto const request = message::get_block_transactions::create(null_hash, {252, 505, 759}).value();
    auto const raw = kth::to_data_chunk(request, version);
    REQUIRE(raw.size() == hash_size + 1 + 1 + 1 + 3);
    REQUIRE(raw.at(hash_size + 3) == 0xfd);
    byte_reader reader(raw);
    auto const parsed = message::get_block_transactions::from_data(reader, version);
    REQUIRE(parsed);
    REQUIRE(*parsed == request);

    for (size_t size = 0; size < raw.size(); ++size) {
        CAPTURE(size);
        byte_reader truncated(byte_span{raw}.first(size));
        REQUIRE_FALSE(message::get_block_transactions::from_data(truncated, version));
    }
}

// End Test Suite
