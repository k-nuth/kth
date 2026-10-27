// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_BCHN_BLOCK_BUILDER_HPP
#define KTH_BLOCKCHAIN_TEST_BCHN_BLOCK_BUILDER_HPP

// Regtest blocks for the scenarios of Bitcoin Cash Node's block tests
// (test/functional/feature_block.py and friends) that are driven through
// validate_block_batch. Built on regtest_miner.hpp: real proof of work, real
// merkle root, real signatures. Nothing here validates anything itself.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/infrastructure/utility/timer.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

namespace kth::test {

using block_ptr = domain::message::block::const_ptr;

// Mine `all` (the coinbase first) on `prev` at regtest difficulty.
inline domain::chain::block mine_block_raw(
    hash_digest const& prev, uint32_t timestamp, domain::chain::transaction::list all,
    uint32_t version = 0x20000000) {
    domain::chain::block candidate(
        domain::chain::header{version, prev, null_hash, timestamp, regtest_bits, 0}, std::move(all));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{version, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return domain::chain::block(std::move(header),
                domain::chain::transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("bchn_block_builder: no nonce satisfied the regtest target");
}

// Canonical transaction order (CTOR) as BCHN defines it: base_blob::Compare looks at the
// hash from its most significant byte, which is the LAST byte of the little-endian
// digest. Both digests are captured in locals so the ranges belong to one object.
inline bool ctor_less(domain::chain::transaction const& a, domain::chain::transaction const& b) {
    auto const ha = a.hash();
    auto const hb = b.hash();
    return std::lexicographical_compare(ha.rbegin(), ha.rend(), hb.rbegin(), hb.rend());
}

inline void sort_ctor(std::vector<domain::chain::transaction>& txs) {
    std::sort(txs.begin(), txs.end(), ctor_less);
}

// True when the transactions after the coinbase are strictly ascending by ctor_less.
inline bool is_ctor_sorted(domain::chain::block const& blk) {
    auto const& txs = blk.transactions();
    for (size_t i = 2; i < txs.size(); ++i) {
        if ( ! ctor_less(txs[i - 1], txs[i])) return false;
    }
    return true;
}

// P2PKH spend of `prev_tx`[index] to arbitrary `outputs`, signed with the miner key.
inline domain::chain::transaction signed_spend(
    domain::chain::transaction const& prev_tx, uint32_t index,
    domain::chain::output::list outputs, domain::script_flags_t flags) {
    auto const prev_value = prev_tx.outputs()[index].value();
    auto const prev_hash = prev_tx.hash();
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{prev_hash, index}, domain::chain::script{}, 0xffffffff);
    domain::chain::transaction unsigned_tx(1, 0, std::move(inputs), domain::chain::output::list(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = prev_tx.outputs()[index];
    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    auto endorsement = domain::chain::script::create_endorsement(
        miner_secret(), miner_script(), unsigned_tx, 0, sighash_type, flags, prev_value);
    if ( ! endorsement) {
        throw std::runtime_error("bchn_block_builder: could not sign the spend");
    }
    domain::chain::input::list signed_inputs;
    signed_inputs.emplace_back(
        domain::chain::output_point{prev_hash, index},
        domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern_unlocking(
            *endorsement, miner_public())),
        0xffffffff);
    return domain::chain::transaction(1, 0, std::move(signed_inputs), std::move(outputs));
}

// A chain on top of the regtest genesis, one block at a time.
class regtest_chain {
public:
    explicit regtest_chain(uint32_t planned_length)
        : prev_(domain::chain::block::genesis_regtest().hash())
        , base_time_(uint32_t(zulu_time()) - (planned_length + 30) * 600) {}

    [[nodiscard]] uint32_t height() const { return uint32_t(blocks_.size()); }
    [[nodiscard]] std::vector<block_ptr> const& blocks() const { return blocks_; }

    // The block at `h` (1-based).
    [[nodiscard]] domain::chain::block const& at(uint32_t h) const { return *blocks_.at(h - 1); }
    [[nodiscard]] domain::chain::transaction const& coinbase_at(uint32_t h) const {
        return at(h).transactions().front();
    }

    // Next block with a standard coinbase: subsidy plus `fees`, then `txs`.
    domain::chain::block const& append(std::vector<domain::chain::transaction> txs, uint64_t fees = 0) {
        auto const h = height() + 1;
        domain::chain::transaction::list all;
        all.reserve(txs.size() + 1);
        all.push_back(coinbase_tx(h, 0, domain::chain::block::subsidy(h, /*retarget*/ false) + fees));
        for (auto& tx : txs) all.push_back(std::move(tx));
        return append_raw(std::move(all));
    }

    // Next block with the transactions exactly as given (coinbase first).
    domain::chain::block const& append_raw(domain::chain::transaction::list all, uint32_t version = 0x20000000) {
        auto const h = height() + 1;
        auto mined = mine_block_raw(prev_, base_time_ + h * 600, std::move(all), version);
        prev_ = mined.hash();
        blocks_.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
        return *blocks_.back();
    }

    // Coinbase-only blocks until the chain is `h` long.
    void extend_to(uint32_t h) {
        while (height() < h) append({});
    }

private:
    hash_digest prev_;
    uint32_t base_time_;
    std::vector<block_ptr> blocks_;
};

// Headers into the index first, as on a node, then the real batch from height 1.
inline code validate_chain(chain_fixture& fixture, blockchain::settings const& s,
                           std::vector<block_ptr> const& blocks) {
    domain::message::header::list headers;
    headers.reserve(blocks.size());
    for (auto const& blk : blocks) headers.push_back(blk->header());
    auto const added = fixture.organizer().add_headers(headers);
    if (added.headers_added != blocks.size()) {
        throw std::runtime_error("bchn_block_builder: the header index did not take every header");
    }
    return blockchain::validate_block_batch(fixture.chain(), s, domain::config::network::regtest, blocks, 1);
}

} // namespace kth::test

#endif // KTH_BLOCKCHAIN_TEST_BCHN_BLOCK_BUILDER_HPP
