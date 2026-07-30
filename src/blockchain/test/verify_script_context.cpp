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
