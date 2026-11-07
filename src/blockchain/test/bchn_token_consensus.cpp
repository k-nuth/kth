// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CashTokens consensus scenarios of Bitcoin Cash Node (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b), src/test/token_tests.cpp
// (with_mempool_check_*, check_consensus_rejection_of_coinbase_tokens) and
// test/functional/feature_token_coinbase_forbidden.py, driven through the real
// validate_block_batch on a regtest chain. BCHN mines each transaction into a
// block through its mempool path; here the block is the unit.
// Original: Copyright (c) 2022-present The Bitcoin developers, MIT license.
//
// Only the accepting controls are here. validate_block_batch applies none of the
// token ledger rules, so the rejecting halves (unknown category, overspend, NFT ex
// nihilo, amount overflow, 129-byte commitment, tokens in a coinbase) are accepted:
// failed_regression token_conservation_not_enforced_in_blocks in
// test/bchn_non_vm/coverage.json, preserved as a runnable port in the evidence.

#include <test_helpers.hpp>

#include <limits>

#include "bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
namespace chain = kth::domain::chain;

namespace {

// Coinbase `h` spent into outputs `outs`, the rest to the miner as fee-free change.
chain::transaction spend_into(regtest_chain const& c, uint32_t h, chain::output::list outs, domain::script_flags_t flags) {
    return signed_spend(c.coinbase_at(h), 0, std::move(outs), flags);
}

chain::output token_output(uint64_t value, chain::token_data_t td) {
    return chain::output(value, miner_script(), chain::token_data_opt{std::move(td)});
}

// A chain whose block 102 holds `funding` (spends coinbase 1) and whose block 103 holds `follow_up`.
code validate_two(char const* tag, blockchain::settings const& s, regtest_chain const& chain) {
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    return validate_chain(fixture, s, chain.blocks());
}

hash_digest genesis_category(regtest_chain const& c, uint32_t h) {
    // The category of a genesis is the txid of the spent output's transaction, output index 0.
    return c.coinbase_at(h).hash();
}

constexpr uint64_t output_value = 1'000'000;

} // namespace

// token_tests/with_mempool_check_valid_genesis_token (post-activation half)
TEST_CASE("BCHN with_mempool_check_valid_genesis_token - a genesis mints tokens", "[bchn_non_vm][token][consensus]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto genesis = spend_into(chain, 1,
        chain::output::list{token_output(output_value, chain::make_fungible(genesis_category(chain, 1), 1))}, s.enabled_flags());
    chain.append({std::move(genesis)}, chain.coinbase_at(1).outputs()[0].value() - output_value);
    CHECK(validate_two("bchn_tok_genesis", s, chain) == error::success);
}

// token_tests/with_mempool_check_token_overspend, control only: spending exactly the amount held.
// The violating half (2 tokens out of 1 in) is accepted by validate_block_batch and lives in the
// evidence port, token_conservation_not_enforced_in_blocks.
TEST_CASE("BCHN with_mempool_check_token_overspend control - spending the exact amount held is accepted", "[bchn_non_vm][token][consensus]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const category = genesis_category(chain, 1);
    auto genesis = spend_into(chain, 1,
        chain::output::list{token_output(output_value, chain::make_fungible(category, 1))}, s.enabled_flags());
    auto const fee1 = chain.coinbase_at(1).outputs()[0].value() - output_value;
    auto const funded = chain.append({genesis}, fee1).transactions()[1];
    auto spend = signed_spend(funded, 0,
        chain::output::list{token_output(output_value - 1000, chain::make_fungible(category, 1))}, s.enabled_flags());
    chain.append({std::move(spend)}, 1000);
    CHECK(validate_two("bchn_tok_overspend_ok", s, chain) == error::success);
}

// token_tests/with_mempool_check_invalid_nft_mint, control only: the NFT that is in the inputs.
TEST_CASE("BCHN with_mempool_check_invalid_nft_mint control - spending the NFT held is accepted", "[bchn_non_vm][token][consensus]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const category = genesis_category(chain, 1);
    auto genesis = spend_into(chain, 1, chain::output::list{token_output(output_value,
        chain::make_both(category, 1, chain::capability_t::none, data_chunk(3, 0xaa)))}, s.enabled_flags());
    auto const fee1 = chain.coinbase_at(1).outputs()[0].value() - output_value;
    auto const funded = chain.append({genesis}, fee1).transactions()[1];
    auto spend = signed_spend(funded, 0, chain::output::list{token_output(output_value - 1000,
        chain::make_both(category, 1, chain::capability_t::none, data_chunk(3, 0xaa)))}, s.enabled_flags());
    chain.append({std::move(spend)}, 1000);
    CHECK(validate_two("bchn_tok_nft_ok", s, chain) == error::success);
}

// token_tests/with_mempool_check_oversized_token_commitment, control only: 128 bytes at Leibniz rules.
TEST_CASE("BCHN with_mempool_check_oversized_token_commitment control - a 128-byte commitment is accepted", "[bchn_non_vm][token][consensus]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const category = genesis_category(chain, 1);
    auto genesis = spend_into(chain, 1, chain::output::list{token_output(output_value,
        chain::make_both(category, 1, chain::capability_t::minting, data_chunk(128, 0xaa)))}, s.enabled_flags());
    chain.append({std::move(genesis)}, chain.coinbase_at(1).outputs()[0].value() - output_value);
    CHECK(validate_two("bchn_tok_commit_ok", s, chain) == error::success);
}
