// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain/validate/validate_input.hpp>
#include <kth/domain.hpp>

using namespace kth;
using namespace kth::blockchain;
using namespace kd::chain;
using kth::decode_base16;

namespace {

// Known-good non-forkid P2PKH vector (same one the consensus suite uses in
// consensus__script_verify.cpp, "valid true non forkid"): a legacy CHECKSIG
// spend that must verify.
constexpr char TX_HEX[] =
    "0100000001f2ca1aebc2e51b345f87365bdfa4956aaa5443cfb38f58e75318e3a3d3f1462e"
    "000000006b483045022100845a35869063291e4610de4939ac76f123a0b11b74e0615694a0"
    "9206c30afbfc022063347ec313a582ab2025863ebdca71015ffc698eb37dbbf679b2865be9"
    "44cf79012102fee381c90149e22ae182156c16316c24fe680a0e617646c3d58531112ac82e"
    "29ffffffff01b2e60200000000001976a914b96b816f378babb1fe585b7be7a2cd16eb99b3"
    "e488ac00000000";
constexpr char PREV_SCRIPT_HEX[] =
    "76a914b96b816f378babb1fe585b7be7a2cd16eb99b3e488ac";

transaction make_tx() {
    auto const raw = decode_base16(TX_HEX);
    REQUIRE(raw);
    byte_reader reader(byte_span{raw->data(), raw->size()});
    auto tx = transaction::from_data(reader, true);
    REQUIRE(tx);
    return std::move(*tx);
}

output make_prevout() {
    auto const raw = decode_base16(PREV_SCRIPT_HEX);
    REQUIRE(raw);
    byte_reader reader(byte_span{raw->data(), raw->size()});
    auto scr = script::from_data(reader, false);   // raw script bytes, no prefix
    REQUIRE(scr);
    return output{0u, std::move(*scr), std::nullopt};
}

} // namespace

// Regression test for validate_input::verify_script under BCH. The BCH consensus
// checker is context-based: it needs the spent outputs (coins) to build the
// signature checker. verify_script used to supply that context only when native
// introspection or tokens were active, so for an ordinary CHECKSIG spend the
// checker had no context and every signature evaluated to false ("stack false").
//
// This drives verify_script exactly as callers do (populate the input's prevout
// cache, then verify), independent of any IBD, so it isolates the invocation from
// data resolution. It fails ("stack false") before the fix and passes after.
TEST_CASE("validate_input verify_script legacy P2PKH via prevout cache", "[verify_script][plumbing]") {
    using domain::machine::script_flags;

    auto const tx = make_tx();
    tx.inputs()[0].previous_output().validation.cache = make_prevout();

    // The domain round-trip must reproduce the original wire bytes; otherwise the
    // sighash preimage would differ and this would be a serialization bug rather
    // than the checker-context bug under test.
    REQUIRE(to_data_chunk(tx, true) == *decode_base16(TX_HEX));
    REQUIRE(to_data_chunk(tx.inputs()[0].previous_output().validation.cache.script(), false)
            == *decode_base16(PREV_SCRIPT_HEX));

    // A legacy P2PKH spend verifies with legacy flags, with or without P2SH.
    for (auto const flags : {script_flags::no_rules, script_flags::bip16_rule}) {
        auto const [ec, sig_checks] = validate_input::verify_script(tx, 0, flags);
        CHECK(ec == error::success);
        CHECK(sig_checks == 1u);
    }
}

// Parity control for the two script interpreters (the consensus library used
// by block validation and the native one): the same BCH Schnorr and ECDSA
// forkid P2PKH spends are accepted by both, and a one-bit change in the
// signature is rejected by both. Signing goes through the library under test,
// so this holds for any conforming secp256k1 regardless of nonce derivation.
TEST_CASE("validate_input P2PKH schnorr and ecdsa agree across interpreters", "[verify_script][parity]") {
    using domain::machine::script_flags;

    constexpr auto secret = "8010b1bb119ad37d4b65a1022a314897b1b3614b345974332cb1b9582cf03536"_base16;
    constexpr uint64_t value = 100000;
    constexpr uint8_t sighash_all_forkid = 0x41;
    static auto const previous = "dae852c88a00e95141cfe924ac6667a91af87431988d23eff268ea3509d6d83c"_hash;

    kd::script_flags_t flags = script_flags::bip16_rule;
    flags |= script_flags::bip65_rule;
    flags |= script_flags::bip66_rule;
    flags |= script_flags::bip112_rule;
    flags |= kd::to_flags(kd::machine::upgrade::bch_mersenne);

    ec_compressed point;
    REQUIRE(secret_to_public(point, secret));
    script const prevout_script(script::to_pay_public_key_hash_pattern(bitcoin_short_hash(point)));

    auto const make_spend = [&](script const& unlocking) {
        transaction tx(2, 0,
            {input(output_point(previous, 0), unlocking, max_uint32)},
            {output(value - 1000, prevout_script, std::nullopt)});
        tx.inputs()[0].previous_output().validation.cache = output{value, prevout_script, std::nullopt};
        return tx;
    };

    auto const unsigned_tx = make_spend(script{});

    for (auto const type : {endorsement_type::ecdsa, endorsement_type::schnorr}) {
        auto signature = script::create_endorsement(secret, prevout_script, unsigned_tx, 0,
            sighash_all_forkid, flags, value, type);
        REQUIRE(signature);
        REQUIRE(signature->back() == sighash_all_forkid);
        if (type == endorsement_type::schnorr) {
            REQUIRE(signature->size() == schnorr_signature_size + 1);
        }

        auto const spend = [&](data_chunk const& endorsement) {
            return make_spend(script(kd::machine::operation::list{
                kd::machine::operation(endorsement),
                kd::machine::operation(to_chunk(point))}));
        };

        auto const valid = spend(*signature);
        auto const consensus_valid = validate_input::verify_script(valid, 0, flags);
        CHECK(consensus_valid.first == error::success);
        CHECK(consensus_valid.second == 1u);
        CHECK(script::verify(valid, 0, flags) == error::success);

        (*signature)[10] ^= 0x01;
        auto const invalid = spend(*signature);
        CHECK(validate_input::verify_script(invalid, 0, flags).first != error::success);
        CHECK(script::verify(invalid, 0, flags) != error::success);
    }
}
