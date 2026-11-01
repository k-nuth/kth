// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <cstdint>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/infrastructure/math/hash.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

namespace {
using domain::chain::block;
using domain::chain::input;
using domain::chain::output;
using domain::chain::output_point;
using domain::chain::script;
using domain::chain::transaction;

struct spec {
    uint32_t len = 123;
    bool extra_coinbase = false;
    bool bip34 = true;
    domain::config::network network = domain::config::network::regtest;
    bool wrong_coinbase_height = false;
    int transaction_order = 0;
    bool dependent_transactions = false;
    uint32_t padding_transactions = 0;
    uint32_t padding_height = 121;
};
uint32_t base_time(uint32_t) { return 1'700'000'000; }
block mine(hash_digest const& prev, uint32_t timestamp, transaction::list txs) {
    block candidate(domain::chain::header{0x20000000, prev, null_hash, timestamp, regtest_bits, 0}, std::move(txs));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{0x20000000, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return block(std::move(header), transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

std::vector<domain::message::block::const_ptr> build_chain(spec const& sp, uint32_t base, domain::script_flags_t) {
    std::vector<domain::message::block::const_ptr> blocks;
    auto prev = block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= sp.len; ++h) {
        auto cb = coinbase_tx(sp.wrong_coinbase_height && h == 1 ? h + 1 : h, 0, block::subsidy(h, false));
        if (h <= sp.padding_transactions) {
            domain::machine::operation::list ops;
            ops.emplace_back(domain::machine::opcode::push_positive_1);
            output::list outs;
            outs.emplace_back(cb.outputs().front().value(), script{std::move(ops)}, domain::chain::token_data_opt{});
            cb = transaction(cb.version(), cb.locktime(), input::list(cb.inputs()), std::move(outs));
        }
        transaction::list txs{std::move(cb)};
        if (sp.extra_coinbase && h == 1) txs.push_back(coinbase_tx(h, 1, 0));
        if (sp.padding_transactions && h == sp.padding_height) {
            for (uint32_t fund = 1; fund <= sp.padding_transactions; ++fund) {
                auto const& source = blocks[fund - 1]->transactions().front();
                input::list inputs;
                inputs.emplace_back(output_point{source.hash(), 0}, script{}, 0xffffffff);
                output::list outputs;
                domain::machine::operation::list ops;
                ops.emplace_back(domain::machine::opcode::push_positive_1);
                outputs.emplace_back(source.outputs().front().value(), script{std::move(ops)}, domain::chain::token_data_opt{});
                txs.emplace_back(1, 0, std::move(inputs), std::move(outputs));
            }
            if (sp.dependent_transactions) {
                auto const parent = txs.back();
                input::list inputs;
                inputs.emplace_back(output_point{parent.hash(), 0}, script{}, 0xffffffff);
                transaction child(1, 0, std::move(inputs), output::list(parent.outputs()));
                txs.push_back(std::move(child));
                if (sp.transaction_order == 3) std::swap(txs[1], txs[2]);
            }
            // Consensus compares numeric transaction IDs, least significant byte first in storage.
            if (sp.transaction_order < 3) std::sort(txs.begin() + 1, txs.end(), [&](auto const& a, auto const& b) {
                auto const& ah = a.hash(); auto const& bh = b.hash();
                return sp.transaction_order == 1
                    ? std::lexicographical_compare(ah.rbegin(), ah.rend(), bh.rbegin(), bh.rend())
                    : std::lexicographical_compare(bh.rbegin(), bh.rend(), ah.rbegin(), ah.rend());
            });
        }
        auto mined = mine(prev, base + h * 600, std::move(txs));
        prev = mined.hash();
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return blocks;
}
code run(char const* name, spec const& sp) {
    chain_fixture fixture(name);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    // Keep regtest PoW/index while selecting the contextual consensus network.
    settings s(sp.network);
    s.bip34 = sp.bip34;
    s.retarget = false;
    auto const blocks = build_chain(sp, base_time(sp.len), s.enabled_flags());
    domain::message::header::list headers;
    for (auto const& blk : blocks) headers.push_back(blk->header());
    auto const added = fixture.organizer().add_headers(headers);
    REQUIRE((added.headers_added == blocks.size() || added.error == error::stale_chain));
    return validate_block_batch(fixture.chain(), s, sp.network, blocks, 1);
}
} // namespace

TEST_CASE("complete batch validation rejects a second zero-value coinbase", "[batch_structure][extra_coinbase]") {
    spec valid;
    valid.len = 1;
    REQUIRE(run("audit_one_coinbase", valid) == error::success);
    spec invalid = valid;
    invalid.extra_coinbase = true;
    auto const blocks = build_chain(invalid, base_time(invalid.len), settings(domain::config::network::regtest).enabled_flags());
    REQUIRE(blocks.front()->check_body() == error::extra_coinbases);
    CHECK(run("audit_extra_coinbase", invalid) == error::extra_coinbases);
}

TEST_CASE("complete batch validation enforces the raw BIP34 height", "[batch_structure][bip34_height]") {
    spec valid;
    valid.len = 1;
    REQUIRE(run("audit_bip34_valid", valid) == error::success);
    spec invalid = valid;
    invalid.wrong_coinbase_height = true;
    auto const blocks = build_chain(invalid, base_time(invalid.len), settings(domain::config::network::regtest).enabled_flags());
    REQUIRE_FALSE(blocks.front()->is_valid_coinbase_script(1));
    CHECK(run("audit_bip34_wrong", invalid) == error::coinbase_height_mismatch);
    invalid.bip34 = false;
    CHECK(run("bip34_disabled", invalid) == error::success);
}

TEST_CASE("complete batch validation enforces canonical transaction ordering", "[batch_structure][ctor_order]") {
    spec valid;
    valid.padding_transactions = 2;
    valid.transaction_order = 1;
    REQUIRE(run("audit_ctor_valid", valid) == error::success);
    spec invalid = valid;
    invalid.transaction_order = 2;
    auto const blocks = build_chain(invalid, base_time(invalid.len), settings(domain::config::network::regtest).enabled_flags());
    REQUIRE_FALSE(blocks[invalid.padding_height - 1]->is_canonical_ordered());
    CHECK(run("audit_ctor_wrong", invalid) == error::non_canonical_ordered);
    invalid.network = domain::config::network::mainnet; // Before mainnet Euclid height.
    CHECK(run("ctor_before_activation", invalid) == error::success);
}

TEST_CASE("complete batch validation rejects forward references before Euclid", "[batch_structure][forward_reference]") {
    spec valid;
    valid.network = domain::config::network::mainnet;
    valid.padding_transactions = 1;
    valid.dependent_transactions = true;
    valid.transaction_order = 4; // Parent before child.
    REQUIRE(run("ltor_parent_first", valid) == error::success);
    spec invalid = valid;
    invalid.transaction_order = 3; // Child before parent.
    auto const blocks = build_chain(invalid, base_time(invalid.len), settings(invalid.network).enabled_flags());
    REQUIRE(blocks[invalid.padding_height - 1]->is_forward_reference());
    CHECK(run("ltor_child_first", invalid) == error::forward_reference);
}

#endif // KTH_CURRENCY_BCH
