// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The BIP68 transaction matrix of test/functional/feature_csv_activation.py (Bitcoin Cash Node, commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): 16 transactions whose nSequence is 10 with the four flags varied (disable flag 1 << 31, a
// random high bit 1 << 25, the type flag 1 << 22 and a random low bit 1 << 18), for transaction version 1 and for version 2. A funding
// transaction in one block creates 32 outputs; a test block `delta` blocks after it holds one matrix transaction. Regtest, blocks 600 s apart:
// the height lock of 10 is satisfied from delta 10, the time lock (10 * 512 s) from delta 9.
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#ifndef KTH_BLOCKCHAIN_TEST_BCHN_CSV_MATRIX_HPP
#define KTH_BLOCKCHAIN_TEST_BCHN_CSV_MATRIX_HPP

#include <test_helpers.hpp>

#include "bchn_block_builder.hpp"

namespace kth::test::csv_matrix {

constexpr uint32_t base_locktime = 10;
constexpr uint32_t disable_flag = 1u << 31;
constexpr uint32_t random_high_bit = 1u << 25;
constexpr uint32_t type_flag = 1u << 22;
constexpr uint32_t random_low_bit = 1u << 18;

struct entry {
    uint32_t sequence;
    bool disabled;
    bool time_type;
};

inline std::vector<entry> matrix() {
    std::vector<entry> rows;
    for (int mask = 0; mask < 16; ++mask) {
        bool const sdf = mask & 8, srhb = mask & 4, stf = mask & 2, srlb = mask & 1;
        uint32_t seq = base_locktime;
        if (sdf) seq |= disable_flag;
        if (srhb) seq |= random_high_bit;
        if (stf) seq |= type_flag;
        if (srlb) seq |= random_low_bit;
        rows.push_back({seq, sdf, stf});
    }
    return rows;
}

// A chain of `coin_height` blocks whose last block holds the funding transaction (32 outputs); `outputs` receives its outputs.
struct funded_chain {
    regtest_chain chain;
    domain::chain::transaction funding;

    explicit funded_chain(uint32_t coin_height, blockchain::settings const& s) : chain(coin_height + 40) {
        chain.extend_to(coin_height - 1);
        auto const& cb = chain.coinbase_at(1);
        domain::chain::output::list outs;
        auto const each = (cb.outputs()[0].value() - 100000) / 32;
        for (int i = 0; i < 32; ++i) outs.emplace_back(each, miner_script(), domain::chain::token_data_opt{});
        funding = signed_spend(cb, 0, std::move(outs), s.enabled_flags());
        chain.append({funding}, cb.outputs()[0].value() - 32 * each);
    }

    // The spend of output `index` of the funding transaction with the given version and sequence.
    domain::chain::transaction spend(uint32_t index, uint32_t version, uint32_t sequence, domain::script_flags_t flags) const {
        auto const value = funding.outputs()[index].value() - 1000;
        domain::chain::input::list inputs;
        inputs.emplace_back(domain::chain::output_point{funding.hash(), index}, domain::chain::script{}, sequence);
        domain::chain::output::list outs;
        outs.emplace_back(value, miner_script(), domain::chain::token_data_opt{});
        domain::chain::transaction unsigned_tx(version, 0, std::move(inputs), domain::chain::output::list(outs));
        unsigned_tx.inputs()[0].previous_output().validation.cache = funding.outputs()[index];
        auto endorsement = domain::chain::script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
            uint8_t(infrastructure::machine::sighash_algorithm::forkid_all), flags, funding.outputs()[index].value());
        REQUIRE(endorsement);
        domain::chain::input::list signed_in;
        signed_in.emplace_back(domain::chain::output_point{funding.hash(), index},
            domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern_unlocking(*endorsement, miner_public())), sequence);
        return domain::chain::transaction(version, 0, std::move(signed_in), std::move(outs));
    }

    // Validate the chain extended by `delta - 1` empty blocks and a block with `tx`, on a fresh node.
    code validate_at(uint32_t delta, domain::chain::transaction const& tx, blockchain::settings const& s, char const* tag) const {
        regtest_chain copy = chain;
        for (uint32_t i = 1; i < delta; ++i) copy.append({});
        copy.append({tx}, 1000);
        chain_fixture fixture(tag);
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        return validate_chain(fixture, s, copy.blocks());
    }
};

} // namespace kth::test::csv_matrix

#endif
