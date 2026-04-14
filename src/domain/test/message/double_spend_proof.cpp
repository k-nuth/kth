// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Test hashes — stable bit patterns used across the suite.
static auto const k_hash_a = "1122334455667788" "99aabbccddeeff00"
                             "1122334455667788" "99aabbccddeeff00"_hash;
static auto const k_hash_b = "deadbeefdeadbeef" "deadbeefdeadbeef"
                             "deadbeefdeadbeef" "deadbeefdeadbeef"_hash;
static auto const k_hash_c = "cafebabecafebabe" "cafebabecafebabe"
                             "cafebabecafebabe" "cafebabecafebabe"_hash;

// Builds a spender with populated fields and no push_data, so its
// serialized size is exactly 108 bytes (4+4+4+32+32+32). This makes
// from_data round-trips deterministic (push_data is read via
// read_remaining_bytes() and will be empty when we feed exactly 108 bytes).
static message::double_spend_proof::spender make_spender(uint32_t version = 1) {
    message::double_spend_proof::spender s;
    s.version = version;
    s.out_sequence = 2;
    s.locktime = 3;
    s.prev_outs_hash = k_hash_a;
    s.sequence_hash = k_hash_b;
    s.outputs_hash = k_hash_c;
    return s;
}

// Builds an output_point with a non-null hash and an arbitrary index.
static chain::output_point make_out_point() {
    return chain::output_point(k_hash_a, 7u);
}

// ===========================================================================
// spender
// ===========================================================================

// ---------------------------------------------------------------------------
// Lifecycle / predicates
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof::spender  default construct  is invalid", "[double_spend_proof::spender]") {
    message::double_spend_proof::spender s;
    REQUIRE( ! s.is_valid());
    REQUIRE(s.version == 0u);
    REQUIRE(s.out_sequence == 0u);
    REQUIRE(s.locktime == 0u);
    REQUIRE(s.prev_outs_hash == null_hash);
    REQUIRE(s.sequence_hash == null_hash);
    REQUIRE(s.outputs_hash == null_hash);
    REQUIRE(s.push_data.empty());
}

TEST_CASE("double_spend_proof::spender  any nonzero field  is valid", "[double_spend_proof::spender]") {
    {
        message::double_spend_proof::spender s;
        s.version = 1;
        REQUIRE(s.is_valid());
    }
    {
        message::double_spend_proof::spender s;
        s.out_sequence = 1;
        REQUIRE(s.is_valid());
    }
    {
        message::double_spend_proof::spender s;
        s.locktime = 1;
        REQUIRE(s.is_valid());
    }
    {
        message::double_spend_proof::spender s;
        s.prev_outs_hash = k_hash_a;
        REQUIRE(s.is_valid());
    }
    {
        message::double_spend_proof::spender s;
        s.sequence_hash = k_hash_a;
        REQUIRE(s.is_valid());
    }
    {
        message::double_spend_proof::spender s;
        s.outputs_hash = k_hash_a;
        REQUIRE(s.is_valid());
    }
}

TEST_CASE("double_spend_proof::spender  reset  zeroes all fields", "[double_spend_proof::spender]") {
    auto s = make_spender();
    s.push_data = data_chunk{1, 2, 3};
    REQUIRE(s.is_valid());

    s.reset();

    REQUIRE( ! s.is_valid());
    REQUIRE(s.version == 0u);
    REQUIRE(s.out_sequence == 0u);
    REQUIRE(s.locktime == 0u);
    REQUIRE(s.prev_outs_hash == null_hash);
    REQUIRE(s.sequence_hash == null_hash);
    REQUIRE(s.outputs_hash == null_hash);
    REQUIRE(s.push_data.empty());
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof::spender  equality  reflexive and diverges under mutation", "[double_spend_proof::spender]") {
    auto a = make_spender();
    auto b = make_spender();
    REQUIRE(a == b);
    REQUIRE( ! (a != b));

    b.version = 99;
    REQUIRE(a != b);
    REQUIRE( ! (a == b));
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof::spender  serialized_size  is 108 with empty push_data", "[double_spend_proof::spender]") {
    auto const s = make_spender();
    REQUIRE(s.serialized_size() == 108u);
}

TEST_CASE("double_spend_proof::spender  serialized_size  grows with push_data", "[double_spend_proof::spender]") {
    auto s = make_spender();
    s.push_data = data_chunk{1, 2, 3, 4, 5};
    REQUIRE(s.serialized_size() == 113u);
}

TEST_CASE("double_spend_proof::spender  from_data insufficient bytes  failure", "[double_spend_proof::spender]") {
    data_chunk raw{0x00, 0x01, 0x02};
    byte_reader reader(raw);
    auto result = message::double_spend_proof::spender::from_data(reader, 0u);
    REQUIRE( ! result);
}

TEST_CASE("double_spend_proof::spender  round-trip with empty push_data  preserves fields", "[double_spend_proof::spender]") {
    auto const expected = make_spender();

    // Spender does not expose a public stand-alone to_data(), only the
    // templated writer variant. We feed it through the DSP serialization,
    // which calls spender::to_data internally; but to isolate the spender
    // round-trip we build the wire bytes manually with the same ordering.
    data_chunk raw;
    raw.reserve(expected.serialized_size());
    // version LE
    raw.push_back(static_cast<uint8_t>(expected.version & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.version >> 8) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.version >> 16) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.version >> 24) & 0xffu));
    // out_sequence LE
    raw.push_back(static_cast<uint8_t>(expected.out_sequence & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.out_sequence >> 8) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.out_sequence >> 16) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.out_sequence >> 24) & 0xffu));
    // locktime LE
    raw.push_back(static_cast<uint8_t>(expected.locktime & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.locktime >> 8) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.locktime >> 16) & 0xffu));
    raw.push_back(static_cast<uint8_t>((expected.locktime >> 24) & 0xffu));
    // three hashes in declaration order
    raw.insert(raw.end(), expected.prev_outs_hash.begin(), expected.prev_outs_hash.end());
    raw.insert(raw.end(), expected.sequence_hash.begin(), expected.sequence_hash.end());
    raw.insert(raw.end(), expected.outputs_hash.begin(), expected.outputs_hash.end());

    REQUIRE(raw.size() == 108u);

    byte_reader reader(raw);
    auto result = message::double_spend_proof::spender::from_data(reader, 0u);
    REQUIRE(result);
    REQUIRE(*result == expected);
}

// ===========================================================================
// double_spend_proof
// ===========================================================================

// ---------------------------------------------------------------------------
// Constructors / predicates
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  default construct  is invalid", "[double_spend_proof]") {
    message::double_spend_proof dsp;
    REQUIRE( ! dsp.is_valid());
}

TEST_CASE("double_spend_proof  three-arg construct  preserves fields", "[double_spend_proof]") {
    auto const out_point = make_out_point();
    auto const s1 = make_spender(1);
    auto const s2 = make_spender(2);

    message::double_spend_proof dsp(out_point, s1, s2);
    REQUIRE(dsp.is_valid());
    REQUIRE(dsp.out_point() == out_point);
    REQUIRE(dsp.spender1() == s1);
    REQUIRE(dsp.spender2() == s2);
}

TEST_CASE("double_spend_proof  is_valid  requires all three components", "[double_spend_proof]") {
    auto const op = make_out_point();
    auto const s = make_spender();

    // Valid out_point + two valid spenders → valid.
    REQUIRE(message::double_spend_proof(op, s, s).is_valid());

    // Default-constructed out_point makes the whole proof invalid.
    REQUIRE( ! message::double_spend_proof(chain::output_point{}, s, s).is_valid());

    // A default (all-zero) spender makes the proof invalid.
    REQUIRE( ! message::double_spend_proof(op, message::double_spend_proof::spender{}, s).is_valid());
    REQUIRE( ! message::double_spend_proof(op, s, message::double_spend_proof::spender{}).is_valid());
}

TEST_CASE("double_spend_proof  reset  drops to invalid", "[double_spend_proof]") {
    message::double_spend_proof dsp(make_out_point(), make_spender(), make_spender());
    REQUIRE(dsp.is_valid());

    dsp.reset();
    REQUIRE( ! dsp.is_valid());
    REQUIRE(dsp.out_point() == chain::output_point{});
    REQUIRE( ! dsp.spender1().is_valid());
    REQUIRE( ! dsp.spender2().is_valid());
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  setters  replace fields", "[double_spend_proof]") {
    message::double_spend_proof dsp;

    auto const op = make_out_point();
    dsp.set_out_point(op);
    REQUIRE(dsp.out_point() == op);

    auto const s1 = make_spender(1);
    dsp.set_spender1(s1);
    REQUIRE(dsp.spender1() == s1);

    auto const s2 = make_spender(2);
    dsp.set_spender2(s2);
    REQUIRE(dsp.spender2() == s2);

    REQUIRE(dsp.is_valid());
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  equality  reflexive and diverges under mutation", "[double_spend_proof]") {
    message::double_spend_proof a(make_out_point(), make_spender(1), make_spender(2));
    message::double_spend_proof b(make_out_point(), make_spender(1), make_spender(2));

    REQUIRE(a == b);
    REQUIRE( ! (a != b));

    b.set_spender1(make_spender(99));
    REQUIRE(a != b);
    REQUIRE( ! (a == b));
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  serialized_size  matches to_data length", "[double_spend_proof]") {
    message::double_spend_proof dsp(make_out_point(), make_spender(1), make_spender(2));

    auto const size = dsp.serialized_size(0);
    // outpoint (36) + spender1 (108) + spender2 (108) = 252
    REQUIRE(size == 252u);

    auto const raw = dsp.to_data(0);
    REQUIRE(raw.size() == size);
}

TEST_CASE("double_spend_proof  from_data insufficient bytes  failure", "[double_spend_proof]") {
    data_chunk const raw{0xab, 0xcd, 0xef};
    byte_reader reader(raw);
    auto result = message::double_spend_proof::from_data(reader, 0u);
    REQUIRE( ! result);
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  hash  is deterministic", "[double_spend_proof]") {
    message::double_spend_proof dsp(make_out_point(), make_spender(1), make_spender(2));

    auto const h1 = dsp.hash();
    auto const h2 = dsp.hash();
    REQUIRE(h1 == h2);
    REQUIRE(h1 != null_hash);

    // The free-function overload must agree with the member.
    REQUIRE(message::hash(dsp) == h1);
}

TEST_CASE("double_spend_proof  hash  changes under mutation", "[double_spend_proof]") {
    message::double_spend_proof dsp(make_out_point(), make_spender(1), make_spender(2));
    auto const h1 = dsp.hash();

    dsp.set_spender1(make_spender(99));
    auto const h2 = dsp.hash();
    REQUIRE(h1 != h2);
}

TEST_CASE("double_spend_proof  hash  differs between distinct proofs", "[double_spend_proof]") {
    message::double_spend_proof a(make_out_point(), make_spender(1), make_spender(2));
    message::double_spend_proof b(chain::output_point(k_hash_b, 7u), make_spender(1), make_spender(2));
    REQUIRE(a.hash() != b.hash());
}

// ---------------------------------------------------------------------------
// Static metadata
// ---------------------------------------------------------------------------

TEST_CASE("double_spend_proof  command  is dsproof-beta", "[double_spend_proof]") {
    REQUIRE(message::double_spend_proof::command == "dsproof-beta");
}
