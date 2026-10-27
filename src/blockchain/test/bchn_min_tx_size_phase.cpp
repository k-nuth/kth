// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// test/functional/feature_min_tx_size.py of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): the minimum transaction size is 100 bytes
// before Upgrade 9 and 65 after it (bad-txns-undersize), coinbase included. The phase is built with Knuth's per-upgrade settings passed to validate_block_batch
// (blockchain::settings bch_descartes and every later upgrade off, because the masks are cumulative); the effective flags are required. This reproduces the flag
// phase of the original, not its activation height. Controls: a coinbase of exactly 100 bytes before Upgrade 9 and of 65 bytes after it are accepted.
// The refusals (99 bytes before, 64 after) are a failed regression kept outside this tree (evidence round8/min_tx_size_phase).
// Original: Copyright (c) 2022-2023 The Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include "bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
using kth::domain::chain::block;
using kth::domain::chain::output_point;
using kth::domain::chain::script;
using kth::domain::chain::transaction;

namespace {

blockchain::settings settings_for(bool upgrade9) {
    blockchain::settings s(domain::config::network::regtest);
    if ( ! upgrade9) { s.bch_descartes = false; s.bch_lobachevski = false; s.bch_galois = false; s.bch_leibniz = false; }
    using domain::machine::script_flags;
    auto const f = s.enabled_flags();
    REQUIRE(bool(f & domain::script_flags_t(script_flags::bch_tokens)) == upgrade9);
    REQUIRE(bool(f & domain::script_flags_t(script_flags::bch_p2sh_32)) == upgrade9);
    return s;
}

// a coinbase of exactly `size` bytes: BIP34-style height push padded with zeros as scriptSig, one OP_TRUE output (61 bytes + the scriptSig)
code validate_coinbase_of_size(size_t size, bool upgrade9, char const* tag) {
    auto const s = settings_for(upgrade9);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const height = chain.height() + 1;
    data_chunk sig{0x01, uint8_t(height)};
    REQUIRE(size >= 61 + 2);
    sig.resize(size - 61, 0x00);
    domain::chain::input::list inputs;
    inputs.emplace_back(output_point{null_hash, domain::chain::point::null_index}, script(std::move(sig), false), 0xffffffff);
    domain::chain::output::list outputs;
    outputs.emplace_back(block::subsidy(height, false), script(domain::machine::operation::list{domain::machine::operation(domain::machine::opcode::push_positive_1)}), domain::chain::token_data_opt{});
    transaction coinbase(1, 0, std::move(inputs), std::move(outputs));
    REQUIRE(coinbase.serialized_size(true) == size);
    transaction::list all;
    all.push_back(std::move(coinbase));
    chain.append_raw(std::move(all));
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    return validate_chain(fixture, s, chain.blocks());
}

} // namespace

TEST_CASE("BCHN feature_min_tx_size - coinbases of the minimum size are accepted in their phase", "[bchn_non_vm][min_tx_size][phase]") {
    CHECK(validate_coinbase_of_size(100, false, "bchn_mts_100") == error::success);      // before Upgrade 9: 100 bytes
    CHECK(validate_coinbase_of_size(65, true, "bchn_mts_65") == error::success);          // after Upgrade 9: 65 bytes
    CHECK(validate_coinbase_of_size(100, true, "bchn_mts_100_post") == error::success);   // 100 bytes is above the minimum in both
}
