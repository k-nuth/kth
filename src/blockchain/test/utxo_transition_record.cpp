// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <set>

#include <kth/database/utxo_transition_record.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// The record that says a transition did not finish (#600)
// =============================================================================
//
// It is the authority on WHETHER the last transition finished, and every way of
// failing to read it has to stay distinguishable from "there is nothing here".
// A damaged record reported as absent is the whole defect this exists to
// prevent, so the decoder's refusals are what these tests are about.

namespace {

utxo_transition_record sample() {
    return utxo_transition_record{
        .format_version = utxo_transition_record::current_format_version,
        .type = transition_type::connect_batch,
        .operation_id = 0x0123456789ABCDEFull,
        .first_height = 700'000u,
        .intended_last_height = 701'000u,
        .state = transition_state::in_progress};
}

// Re-checksum after damaging a field, so a test aimed at one refusal is not
// answered by the checksum instead.
void reseal(data_chunk& bytes) {
    auto const payload = bytes.size() - checksum_size;
    auto const sum = bitcoin_checksum(std::span<uint8_t const>{bytes.data(), payload});
    for (size_t i = 0; i < checksum_size; ++i) {
        bytes[payload + i] = static_cast<uint8_t>((sum >> (8 * i)) & 0xFFu);
    }
}

} // namespace

TEST_CASE("a transition record round-trips", "[transition_record]") {
    auto const original = sample();
    auto const bytes = encode(original);

    REQUIRE(bytes.size() == utxo_transition_record::version_1_size);

    auto const decoded = decode_transition_record(bytes);
    REQUIRE(decoded);
    CHECK(*decoded == original);
}

TEST_CASE("the envelope is where the format says it is", "[transition_record]") {
    // The two rules a future version may not touch: the version is the first
    // two bytes, the checksum is the last four. Everything else about the
    // layout may change; if these move, the validation order stops being sound
    // because a reader would have to trust a field to find the check that
    // covers it.
    auto const bytes = encode(sample());

    uint16_t const version = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
    CHECK(version == utxo_transition_record::current_format_version);

    auto const payload = bytes.size() - checksum_size;
    uint32_t stored = 0;
    for (size_t i = 0; i < checksum_size; ++i) {
        stored |= uint32_t(bytes[payload + i]) << (8 * i);
    }
    CHECK(stored == bitcoin_checksum(std::span<uint8_t const>{bytes.data(), payload}));
}

TEST_CASE("the encoding is byte-for-byte what the format states", "[transition_record]") {
    // Pinned against the literal bytes rather than against a round-trip: a
    // round-trip agrees with itself even if both halves drift onto native
    // layout or the machine's endianness. A record has to be readable by a
    // build that is not this one.
    auto const bytes = encode(utxo_transition_record{
        .format_version = 1u,
        .type = transition_type::reorg,
        .operation_id = 0x0807060504030201ull,
        .first_height = 0x11223344u,
        .intended_last_height = 0x55667788u,
        .state = transition_state::in_progress});

    data_chunk const expected_payload{
        0x01, 0x00,                                       // version 1
        0x02, 0x00,                                       // reorg
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,   // id, little-endian
        0x44, 0x33, 0x22, 0x11,                           // first height
        0x88, 0x77, 0x66, 0x55,                           // intended last height
        0x01};                                            // in_progress

    REQUIRE(bytes.size() == expected_payload.size() + checksum_size);
    CHECK(std::equal(expected_payload.begin(), expected_payload.end(), bytes.begin()));
}

TEST_CASE("a truncated record is refused", "[transition_record]") {
    auto const full = encode(sample());

    // Every prefix, including ones long enough to look like an envelope.
    for (size_t len = 0; len < full.size(); ++len) {
        data_chunk const truncated(full.begin(), full.begin() + len);
        auto const decoded = decode_transition_record(truncated);
        REQUIRE_FALSE(decoded);
        CHECK((decoded.error() == transition_decode_error::too_short ||
               decoded.error() == transition_decode_error::checksum_mismatch ||
               decoded.error() == transition_decode_error::wrong_size));
    }
}

TEST_CASE("a damaged record is refused rather than read", "[transition_record]") {
    // Every single-byte corruption, at every position. None may decode.
    auto const full = encode(sample());

    for (size_t i = 0; i < full.size(); ++i) {
        auto damaged = full;
        damaged[i] ^= 0xFFu;
        auto const decoded = decode_transition_record(damaged);
        CHECK_FALSE(decoded);
    }
}

TEST_CASE("a future version is refused, not guessed at", "[transition_record]") {
    auto bytes = encode(sample());
    bytes[0] = 0x02;    // version 2
    reseal(bytes);

    auto const decoded = decode_transition_record(bytes);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == transition_decode_error::unknown_version);
}

TEST_CASE("an unknown transition type is refused", "[transition_record]") {
    auto bytes = encode(sample());
    bytes[2] = 0x09;    // no such kind
    reseal(bytes);

    auto const decoded = decode_transition_record(bytes);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == transition_decode_error::unknown_type);
}

TEST_CASE("an unknown state is refused", "[transition_record]") {
    auto bytes = encode(sample());
    bytes[20] = 0x07;
    reseal(bytes);

    auto const decoded = decode_transition_record(bytes);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == transition_decode_error::unknown_state);
}

TEST_CASE("the right version at the wrong length is refused", "[transition_record]") {
    auto bytes = encode(sample());
    bytes.insert(bytes.end() - checksum_size, uint8_t{0x00});
    reseal(bytes);

    auto const decoded = decode_transition_record(bytes);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == transition_decode_error::wrong_size);
}

TEST_CASE("the checksum is checked before the version is believed",
          "[transition_record]") {
    // A record carrying a future version AND a broken checksum must report the
    // checksum. Reporting the version instead would mean the decoder read a
    // field to decide how to validate the field — the cycle the fixed envelope
    // exists to break.
    auto bytes = encode(sample());
    bytes[0] = 0x02;    // future version, deliberately NOT resealed

    auto const decoded = decode_transition_record(bytes);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == transition_decode_error::checksum_mismatch);
}

TEST_CASE("operation ids do not repeat", "[transition_record]") {
    // Their whole job is telling two operations apart in a diagnosis. A clock
    // or a pid would collide exactly when several nodes are started together by
    // one script, which is when the ids are worth having.
    REQUIRE_FALSE(pseudo_random::check_available());

    std::set<uint64_t> seen;
    constexpr int draws = 2000;
    for (int i = 0; i < draws; ++i) {
        seen.insert(make_operation_id());
    }
    CHECK(seen.size() == size_t(draws));
}

TEST_CASE("the intended height is named as intended", "[transition_record]") {
    // It is written before the work, so it records what was attempted. This
    // test exists to keep the field's meaning attached to it: a record for a
    // batch that reached nothing carries the same value as one that reached
    // everything, and nothing may read it as progress.
    auto const record = utxo_transition_record{
        .format_version = utxo_transition_record::current_format_version,
        .type = transition_type::connect_batch,
        .operation_id = make_operation_id(),
        .first_height = 500u,
        .intended_last_height = 1500u,
        .state = transition_state::in_progress};

    auto const decoded = decode_transition_record(encode(record));
    REQUIRE(decoded);
    CHECK(decoded->first_height == 500u);
    CHECK(decoded->intended_last_height == 1500u);
}

TEST_CASE("both transition kinds survive the round trip", "[transition_record]") {
    // The two have different rebuild answers, so a record that lost which one
    // it was would send a diagnosis to the wrong place.
    for (auto const kind : {transition_type::connect_batch, transition_type::reorg}) {
        auto record = sample();
        record.type = kind;
        record.operation_id = make_operation_id();

        auto const decoded = decode_transition_record(encode(record));
        REQUIRE(decoded);
        CHECK(decoded->type == kind);
        CHECK(decoded->operation_id == record.operation_id);
        CHECK(decoded->first_height == record.first_height);
        CHECK(decoded->intended_last_height == record.intended_last_height);
    }
}
