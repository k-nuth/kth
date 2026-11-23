// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/endian.hpp>

using namespace kth;
using namespace kd;

namespace {

uint64_t convert_to_uint64t(std::string const& rawdata) {
    uint64_t value;
    std::istringstream iss(rawdata);
    iss >> value;
    return value;
}

chain::header const test_header{10u,
    "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,
    "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash,
    531234u,
    6523454u,
    68644u};

message::compact_block::short_id_list const test_short_ids{
    convert_to_uint64t("aaaaaaaaaaaa"),
    convert_to_uint64t("bbbbbbbbbbbb"),
    convert_to_uint64t("0f0f0f0f0f0f"),
    convert_to_uint64t("f0f0f0f0f0f0")};

message::prefilled_transaction::list const test_transactions{
    message::prefilled_transaction::create(0, chain::transaction(1, 48, {}, {})).value(),
    message::prefilled_transaction::create(2, chain::transaction(2, 32, {}, {})).value(),
    message::prefilled_transaction::create(6, chain::transaction(4, 16, {}, {})).value()};

message::compact_block make_compact_block(uint64_t nonce = 453245u) {
    return message::compact_block(test_header, nonce, test_short_ids, test_transactions);
}

} // namespace

// Start Test Suite: compact block tests

TEST_CASE("compact block create always equals params", "[compact block]") {
    uint64_t const nonce = 453245u;
    auto const instance = message::compact_block(test_header, nonce, test_short_ids, test_transactions);
    REQUIRE(test_header == instance.header());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(test_short_ids == instance.short_ids());
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block copy equals params", "[compact block]") {
    auto const value = make_compact_block();
    message::compact_block instance(value);
    REQUIRE(value == instance);
    REQUIRE(test_header == instance.header());
    REQUIRE(453245u == instance.nonce());
    REQUIRE(test_short_ids == instance.short_ids());
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block from data insufficient bytes failure", "[compact block]") {
    data_chunk const raw{0xab, 0xcd};
    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

// TODO(fernando): review - original hex had 267 chars (odd), base16_literal truncated last char silently. Fixed to 266 chars.
TEST_CASE("compact block from data insufficient bytes mid transaction failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000"_base16);

    byte_reader reader(raw);
    auto result = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data insufficient version failure", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = kth::to_data_chunk(expected, message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(raw);
    auto result = message::compact_block::from_data(reader2, message::compact_block::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("compact block from data valid input success", "[compact block]") {
    auto const raw = to_chunk("0a0000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d61900000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a221b08003e8a6300240c0100d20400000000000004000000121212121212343434343434565656565678789a9a0201000000010000000000000100000001000000000000"_base16);

    byte_reader reader(raw);
    auto result_exp = message::compact_block::from_data(reader, message::compact_block::version_minimum);
    REQUIRE(result_exp);
    auto const expected = std::move(*result_exp);
    auto const data = kth::to_data_chunk(expected, message::compact_block::version_minimum);
    REQUIRE(raw == data);

    byte_reader reader2(data);
    auto const result_exp2 = message::compact_block::from_data(reader2, message::compact_block::version_minimum);
    REQUIRE(result_exp2);
    auto const result = std::move(*result_exp2);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::compact_block::version_minimum));
    REQUIRE(expected.serialized_size(message::compact_block::version_minimum) == result.serialized_size(message::compact_block::version_minimum));
}

TEST_CASE("compact block header accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_header == instance.header());
}

TEST_CASE("compact block nonce accessor always returns initialized value", "[compact block]") {
    uint64_t const nonce = 453245u;
    auto const instance = make_compact_block(nonce);
    REQUIRE(nonce == instance.nonce());
}

TEST_CASE("compact block short ids accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_short_ids == instance.short_ids());
}

TEST_CASE("compact block transactions accessor always returns initialized value", "[compact block]") {
    auto const instance = make_compact_block();
    REQUIRE(test_transactions == instance.transactions());
}

TEST_CASE("compact block operator boolean equals duplicates returns true", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    message::compact_block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("compact block operator boolean equals differs returns false", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    auto const instance = message::compact_block(test_header, 99999u, test_short_ids, test_transactions);
    REQUIRE(instance != expected);
}

TEST_CASE("compact block operator boolean not equals duplicates returns false", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    message::compact_block instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("compact block operator boolean not equals differs returns true", "[compact block]") {
    auto const expected = make_compact_block(12334u);
    auto const instance = message::compact_block(test_header, 99999u, test_short_ids, test_transactions);
    REQUIRE(instance != expected);
}

TEST_CASE("compact block short ids follow the BIP152 construction", "[compact block][bip152]") {
    chain::transaction::list const transactions{
        chain::transaction(1, 0, {}, {}),
        chain::transaction(2, 0, {}, {})};
    chain::block const body(chain::header{1, null_hash, null_hash, 1, 1, 1}, transactions);
    chain::header const header{1, null_hash, body.generate_merkle_root(), 1, 1, 1};
    message::block const block(header, transactions);
    uint64_t const nonce = 0x0807060504030201;
    auto preimage = kth::to_data_chunk(header, true);
    preimage.insert(preimage.end(), {1, 2, 3, 4, 5, 6, 7, 8});
    auto const key = sha256_hash(preimage);
    REQUIRE(message::hash(block, nonce) == key);
    REQUIRE(message::hash(message::compact_block(header, nonce, {}, {})) == key);

    auto const k0 = from_little_endian_unsafe<uint64_t>(key);
    auto const k1 = from_little_endian_unsafe<uint64_t>(std::span{key}.subspan(sizeof(uint64_t)));
    auto const txid = transactions[1].hash();
    auto const expected = sip_hasher(k0, k1).write(txid.data(), txid.size()).finalize()
        & uint64_t{0x0000ffffffffffff};
    REQUIRE(message::compact_short_id(key, transactions[1].hash()) == expected);
    REQUIRE(expected <= uint64_t{0x0000ffffffffffff});

    auto const built = message::compact_block::factory_from_block(block);
    REQUIRE(built.transactions().size() == 1u);
    REQUIRE(built.transactions().front().index() == 0u);
    REQUIRE(built.transactions().front().transaction() == transactions.front());
    REQUIRE(built.short_ids().size() == 1u);
    REQUIRE(message::hash(built) == message::hash(block, built.nonce()));
    REQUIRE(built.short_ids().front() == message::compact_short_id(message::hash(built), transactions[1].hash()));
}

TEST_CASE("compact block short id matches the 32 byte SipHash vector", "[compact block][bip152]") {
    hash_digest bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(i);
    }
    // SipHash's key is 00..0f and its message is 00..1f.
    REQUIRE(message::compact_short_id(bytes, bytes) == uint64_t{0x512f72f27cce});
}

TEST_CASE("compact block prefilled indexes are differentially encoded", "[compact block][bip152]") {
    auto const tx = chain::transaction(1, 0, {}, {});
    auto const version = message::compact_block::version_minimum;
    // Absolute 0, 2, 4. Each later index loses one more than the gap, so the
    // wire bytes are 0, 1, 1.
    message::compact_block const compact(test_header, 99u, {1u, 2u}, {
        message::prefilled_transaction::create(0, tx).value(),
        message::prefilled_transaction::create(2, tx).value(),
        message::prefilled_transaction::create(4, tx).value()});

    auto const data = kth::to_data_chunk(compact, version);
    auto const tx_size = tx.serialized_size(true);
    size_t offset = chain::header::satoshi_fixed_size() + sizeof(uint64_t) + 1 + (2u * 6u) + 1;
    REQUIRE(data.at(offset) == 0u);
    offset += 1 + tx_size;
    REQUIRE(data.at(offset) == 1u);
    offset += 1 + tx_size;
    REQUIRE(data.at(offset) == 1u);

    byte_reader reader(data);
    auto const parsed = message::compact_block::from_data(reader, version);
    REQUIRE(parsed);
    REQUIRE(parsed->transactions().at(0).index() == 0u);
    REQUIRE(parsed->transactions().at(1).index() == 2u);
    REQUIRE(parsed->transactions().at(2).index() == 4u);
    REQUIRE(*parsed == compact);
}

TEST_CASE("compact block empty body round trips", "[compact block][bip152]") {
    message::compact_block const empty(test_header, 7u, {}, {});
    auto const version = message::compact_block::version_minimum;
    auto const data = kth::to_data_chunk(empty, version);
    byte_reader reader(data);
    auto const parsed = message::compact_block::from_data(reader, version);
    REQUIRE(parsed);
    REQUIRE(*parsed == empty);
    REQUIRE(parsed->short_ids().empty());
    REQUIRE(parsed->transactions().empty());
}

TEST_CASE("compact block prefilled size uses differential widths", "[compact block][bip152]") {
    auto const tx = chain::transaction(1, 0, {}, {});
    auto const version = message::compact_block::version_minimum;
    message::compact_block const compact(test_header, 7, message::compact_block::short_id_list(253, 0), {
        message::prefilled_transaction::create(0, tx).value(),
        message::prefilled_transaction::create(253, tx).value(),
        message::prefilled_transaction::create(254, tx).value()});
    // Absolute indexes 253 and 254 need three bytes; their deltas 252 and 0 need one.
    REQUIRE(compact.serialized_size(version) == 80 + 8 + 3 + 253 * 6 + 1 + 3 * (1 + 10));
    auto const raw = kth::to_data_chunk(compact, version);
    byte_reader reader(raw);
    auto const parsed = message::compact_block::from_data(reader, version);
    REQUIRE(parsed);
    REQUIRE(*parsed == compact);
}

TEST_CASE("compact block refuses a prefilled index past the transaction count", "[compact block][bip152]") {
    auto const tx = chain::transaction(1, 0, {}, {});
    auto const version = message::compact_block::version_minimum;
    message::compact_block const compact(test_header, 7u, {}, {
        message::prefilled_transaction::create(1, tx).value()});
    data_chunk raw(compact.serialized_size(version));
    byte_writer writer(raw);
    REQUIRE(compact.to_data(writer, version));

    byte_reader reader(raw);
    auto const parsed = message::compact_block::from_data(reader, version);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error() == error::invalid_compact_block);
}

TEST_CASE("compact block refuses a short id count the buffer cannot hold", "[compact block][bip152]") {
    message::compact_block const empty(test_header, 7u, {}, {});
    auto const version = message::compact_block::version_minimum;
    auto raw = kth::to_data_chunk(empty, version);
    raw.at(chain::header::satoshi_fixed_size() + sizeof(uint64_t)) = 2;
    raw.resize(chain::header::satoshi_fixed_size() + sizeof(uint64_t) + 1);

    byte_reader reader(raw);
    auto const parsed = message::compact_block::from_data(reader, version);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error() == error::invalid_compact_block);
}

TEST_CASE("compact block to_data refuses indexes that are not strictly increasing", "[compact block][bip152]") {
    auto const tx = chain::transaction(1, 0, {}, {});
    auto const version = message::compact_block::version_minimum;
    message::compact_block const unsorted(test_header, 1u, {}, {
        message::prefilled_transaction::create(2, tx).value(),
        message::prefilled_transaction::create(1, tx).value()});
    REQUIRE(unsorted.serialized_size(version) > 0u);
    data_chunk raw(unsorted.serialized_size(version));
    byte_writer writer(raw);
    auto const written = unsorted.to_data(writer, version);
    REQUIRE_FALSE(written);
    REQUIRE(written.error() == error::invalid_compact_block);
}

TEST_CASE("compact block refuses impossible counts and differential indexes", "[compact block][bip152]") {
    auto const version = message::compact_block::version_minimum;
    auto const tx = chain::transaction(1, 0, {}, {});
    auto const prefix = kth::to_data_chunk(test_header, true);

    SECTION("short id count above the block limit") {
        data_chunk raw(prefix.size() + 8 + 9);
        byte_writer writer(raw);
        REQUIRE(writer.write_bytes(prefix));
        REQUIRE(writer.write_little_endian<uint64_t>(0));
        REQUIRE(writer.write_variable_little_endian(max_compact_transactions() + 1));
        byte_reader reader(raw);
        auto const parsed = message::compact_block::from_data(reader, version);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error() == error::invalid_compact_block);
    }

    SECTION("prefilled count above the block limit") {
        data_chunk raw(prefix.size() + 8 + 1 + 9);
        byte_writer writer(raw);
        REQUIRE(writer.write_bytes(prefix));
        REQUIRE(writer.write_little_endian<uint64_t>(0));
        REQUIRE(writer.write_byte(0));
        REQUIRE(writer.write_variable_little_endian(max_compact_transactions() + 1));
        byte_reader reader(raw);
        auto const parsed = message::compact_block::from_data(reader, version);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error() == error::invalid_compact_block);
    }

    SECTION("prefilled count cannot fit the bytes") {
        message::compact_block const compact(test_header, 0, {}, {
            message::prefilled_transaction::create(0, tx).value()});
        auto raw = kth::to_data_chunk(compact, version);
        raw.at(prefix.size() + 8 + 1) = 2;
        byte_reader reader(raw);
        auto const parsed = message::compact_block::from_data(reader, version);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error() == error::invalid_compact_block);
    }

    SECTION("later differential overflows an absolute index") {
        data_chunk raw(prefix.size() + 8 + 2 + 1 + 10 + 9 + 10);
        byte_writer writer(raw);
        REQUIRE(writer.write_bytes(prefix));
        REQUIRE(writer.write_little_endian<uint64_t>(0));
        REQUIRE(writer.write_byte(0));
        REQUIRE(writer.write_byte(2));
        REQUIRE(writer.write_byte(0));
        REQUIRE(tx.to_data(writer, true));
        REQUIRE(writer.write_variable_little_endian(max_uint64));
        REQUIRE(tx.to_data(writer, true));
        byte_reader reader(raw);
        auto const parsed = message::compact_block::from_data(reader, version);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error() == error::invalid_compact_block);
    }

    SECTION("transaction has an input count without the input bytes") {
        message::compact_block const compact(test_header, 0, {}, {
            message::prefilled_transaction::create(0, tx).value()});
        auto raw = kth::to_data_chunk(compact, version);
        raw.at(prefix.size() + 8 + 2 + 1 + 4) = 1;
        byte_reader reader(raw);
        REQUIRE_FALSE(message::compact_block::from_data(reader, version));
    }

    SECTION("prefilled differential is not minimally encoded") {
        message::compact_block const compact(test_header, 0, {}, {
            message::prefilled_transaction::create(0, tx).value()});
        auto raw = kth::to_data_chunk(compact, version);
        auto const index_offset = prefix.size() + 8 + 2;
        raw.at(index_offset) = 0xfd;
        raw.insert(raw.begin() + index_offset + 1, {0, 0});
        byte_reader reader(raw);
        auto const parsed = message::compact_block::from_data(reader, version);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error() == error::invalid_size);
    }
}

TEST_CASE("compact block truncated wire fields fail", "[compact block][bip152]") {
    auto const version = message::compact_block::version_minimum;
    message::compact_block const compact(test_header, 0, {0x060504030201}, {
        message::prefilled_transaction::create(0, chain::transaction(1, 0, {}, {})).value()});
    auto const raw = kth::to_data_chunk(compact, version);
    auto const start = chain::header::satoshi_fixed_size() + sizeof(uint64_t) + 1;
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE(raw.at(start + i) == i + 1);
    }
    for (size_t size = 0; size < raw.size(); ++size) {
        CAPTURE(size);
        byte_reader reader(byte_span{raw}.first(size));
        REQUIRE_FALSE(message::compact_block::from_data(reader, version));
    }
}

// End Test Suite
