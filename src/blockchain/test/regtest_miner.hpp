// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_REGTEST_MINER_HPP
#define KTH_BLOCKCHAIN_TEST_REGTEST_MINER_HPP

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::test {

// Blocks that actually satisfy consensus, built in-process.
//
// The reorg tests need competing chains whose blocks pass the same validation a
// peer's blocks would: real proof of work, real merkle roots, real signatures on
// a real spend of a matured coinbase. Anything weaker and the test would be
// exercising the paths that skip validation rather than the ones that run it.
//
// Regtest makes this cheap: the target is so loose that a nonce is found in a
// couple of tries, so a hundred blocks cost milliseconds.

// Regtest's proof-of-work limit — the difficulty every block here carries.
inline constexpr uint32_t regtest_bits = 0x207fffff;

// One key for the whole test chain: every coinbase pays to it, so any earlier
// coinbase can be spent later without threading key material through the test.
inline ec_secret const& miner_secret() {
    static ec_secret const secret =
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"_hash;
    return secret;
}

inline domain::wallet::ec_public const& miner_public() {
    static domain::wallet::ec_public const public_key = [] {
        ec_compressed point{};
        secret_to_public(point, miner_secret());
        return domain::wallet::ec_public::from_verified_point(point, true);
    }();
    return public_key;
}

// The P2PKH script every mined output pays to.
inline domain::chain::script const& miner_script() {
    static domain::chain::script const locking = [] {
        auto const key_hash = bitcoin_short_hash(miner_public().to_data());
        return domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern(key_hash));
    }();
    return locking;
}

// The coinbase input script: the block height, then a salt that keeps otherwise
// identical coinbases (and so their txids, and so the blocks) distinct across
// branches. `operation`'s minimal encoding gives OP_1..OP_16 below height 17 and
// a minimal number push above it, which is what is_coinbase_pattern expects.
inline domain::chain::script coinbase_script(uint32_t height, uint32_t salt) {
    auto const height_number = infrastructure::machine::number::from_int(int64_t(height));
    domain::machine::operation::list ops;
    ops.emplace_back(height_number->data(), true);
    ops.emplace_back(data_chunk{uint8_t(salt), uint8_t(salt >> 8), uint8_t(salt >> 16), uint8_t(salt >> 24)}, false);
    return domain::chain::script(std::move(ops));
}

inline domain::chain::transaction coinbase_tx(uint32_t height, uint32_t salt, uint64_t value) {
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{null_hash, domain::chain::point::null_index},
                        coinbase_script(height, salt), 0xffffffff);

    domain::chain::output::list outputs;
    outputs.emplace_back(value, miner_script(), domain::chain::token_data_opt{});

    return domain::chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

// A P2PKH spend of `prev_tx`'s output `index`, paying `value` back to the same
// key. Whatever is left over is the fee, which the next coinbase may claim.
inline domain::chain::transaction spend_p2pkh(
    domain::chain::transaction const& prev_tx, uint32_t index, uint64_t value,
    domain::script_flags_t active_flags) {

    auto const prev_value = prev_tx.outputs()[index].value();
    auto const prev_hash = prev_tx.hash();

    // Signed over the finished transaction, so the input goes in unsigned first
    // and is replaced once the endorsement exists.
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{prev_hash, index}, domain::chain::script{}, 0xffffffff);

    domain::chain::output::list outputs;
    outputs.emplace_back(value, miner_script(), domain::chain::token_data_opt{});

    domain::chain::transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));

    // BCH replay protection: the fork id must be in the sighash type, and the
    // prevout value is part of what is signed.
    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);

    auto endorsement = domain::chain::script::create_endorsement(
        miner_secret(), miner_script(), unsigned_tx, 0, sighash_type, active_flags, prev_value);
    if ( ! endorsement) {
        throw std::runtime_error("regtest_miner: could not sign the spend");
    }

    domain::chain::input::list signed_inputs;
    signed_inputs.emplace_back(
        domain::chain::output_point{prev_hash, index},
        domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern_unlocking(
            *endorsement, miner_public())),
        0xffffffff);

    return domain::chain::transaction(1, 0, std::move(signed_inputs),
                                      domain::chain::output::list(unsigned_tx.outputs()));
}

// Mine a block on top of `prev`. `txs` are appended after the coinbase, and the
// coinbase claims the subsidy plus their fees.
inline domain::chain::block mine_block(
    hash_digest const& prev, uint32_t height, uint32_t timestamp, uint32_t salt,
    std::vector<domain::chain::transaction> txs, uint64_t fees) {

    domain::chain::transaction::list all;
    all.reserve(txs.size() + 1);
    all.push_back(coinbase_tx(height, salt,
        domain::chain::block::subsidy(height, /*retarget*/ false) + fees));
    for (auto& tx : txs) {
        all.push_back(std::move(tx));
    }

    domain::chain::block candidate(
        domain::chain::header{0x20000000, prev, null_hash, timestamp, regtest_bits, 0}, std::move(all));

    auto const merkle = candidate.generate_merkle_root();

    // Grind on the header alone — a block's hash is its header's, so there is no
    // need to rebuild the transaction list per attempt. At regtest difficulty
    // roughly every other nonce works, so this is a handful of hashes rather than
    // a mining loop. Bounded all the same: an exhausted nonce range means the
    // target is not what this expects, and that should fail loudly rather than
    // wrap around forever.
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{0x20000000, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return domain::chain::block(std::move(header),
                domain::chain::transaction::list(candidate.transactions()));
        }
    }

    throw std::runtime_error("regtest_miner: no nonce satisfied the regtest target");
}

} // namespace kth::test

#endif // KTH_BLOCKCHAIN_TEST_REGTEST_MINER_HPP
