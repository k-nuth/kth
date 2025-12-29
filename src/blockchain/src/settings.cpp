// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/settings.hpp>

#include <cstdint>

//TODO(fernando): Avoid this dependency
#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

settings::settings(domain::config::network net) {
    namespace abla = kth::domain::chain::abla;
    switch (net) {
        case domain::config::network::mainnet: {
#if defined(KTH_CURRENCY_BCH)
            asert_half_life = 2ull * 24 * 60 * 60;   // two days
            default_consensus_block_size = max_block_size::mainnet_new;
            abla_config = abla::default_config(default_consensus_block_size, false);
#endif
            break;
        }
        case domain::config::network::testnet: {
            easy_blocks = true;
#if defined(KTH_CURRENCY_BCH)
            asert_half_life = 60ull * 60;   // one hour
            default_consensus_block_size = max_block_size::testnet3;
            abla_config = abla::default_config(default_consensus_block_size, true);
#endif
            break;
        }
        case domain::config::network::regtest: {
            easy_blocks = true;
            retarget = false;

#if defined(KTH_CURRENCY_BCH)
            asert_half_life = 2ull * 24 * 60 * 60;   // two days
            default_consensus_block_size = max_block_size::regtest;
            abla_config = abla::default_config(default_consensus_block_size, false);
#endif
            break;
        }
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4: {
            easy_blocks = true;
            asert_half_life = 60ull * 60;   // one hour
            default_consensus_block_size = max_block_size::testnet4;
            abla_config = abla::default_config(default_consensus_block_size, true);
            break;
        }
        case domain::config::network::scalenet: {
            easy_blocks = true;
            asert_half_life = 2ull * 24 * 60 * 60;   // two days
            default_consensus_block_size = max_block_size::scalenet;
            abla_config = abla::default_config(default_consensus_block_size, false);
            break;
        }
        case domain::config::network::chipnet: {
            easy_blocks = true;
            asert_half_life = 60ull * 60;   // one hour
            default_consensus_block_size = max_block_size::chipnet;
            abla_config = abla::default_config(default_consensus_block_size, false);
            break;
        }
#endif
    }

    checkpoints = domain::config::default_checkpoints(net);

    // Pre-compute sorted list and max height
    checkpoints_sorted = infrastructure::config::checkpoint::sort(checkpoints);
    if (!checkpoints_sorted.empty()) {
        max_checkpoint_height = checkpoints_sorted.back().height();
    }
}

domain::script_flags_t settings::enabled_flags() const {
    using namespace domain::machine;
    using domain::script_flags_t;
    using domain::to_flags;

    script_flags_t flags = script_flags::no_rules;
    flags |= (easy_blocks ? script_flags::easy_blocks : 0);
    flags |= (retarget    ? script_flags::retarget    : 0);
    flags |= (bip16       ? script_flags::bip16_rule  : 0);
    flags |= (bip30       ? script_flags::bip30_rule  : 0);
    flags |= (bip34       ? script_flags::bip34_rule  : 0);
    flags |= (bip66       ? script_flags::bip66_rule  : 0);
    flags |= (bip65       ? script_flags::bip65_rule  : 0);
    flags |= (bip90       ? script_flags::bip90_rule  : 0);
    flags |= (bip68       ? script_flags::bip68_rule  : 0);
    flags |= (bip112      ? script_flags::bip112_rule : 0);
    flags |= (bip113      ? script_flags::bip113_rule : 0);

#if defined(KTH_CURRENCY_BCH)
    flags |= (bch_uahf        ? to_flags(upgrade::bch_uahf)        : 0);
    flags |= (bch_daa_cw144   ? to_flags(upgrade::bch_daa_cw144)   : 0);
    flags |= (bch_pythagoras  ? to_flags(upgrade::bch_pythagoras)  : 0);
    flags |= (bch_euclid      ? to_flags(upgrade::bch_euclid)      : 0);
    flags |= (bch_pisano      ? to_flags(upgrade::bch_pisano)      : 0);
    flags |= (bch_mersenne    ? to_flags(upgrade::bch_mersenne)    : 0);
    flags |= (bch_fermat      ? to_flags(upgrade::bch_fermat)      : 0);
    flags |= (bch_euler       ? to_flags(upgrade::bch_euler)       : 0);
    flags |= (bch_gauss       ? to_flags(upgrade::bch_gauss)       : 0);
    flags |= (bch_descartes   ? to_flags(upgrade::bch_descartes)   : 0);
    flags |= (bch_lobachevski ? to_flags(upgrade::bch_lobachevski) : 0);
    flags |= (bch_galois      ? to_flags(upgrade::bch_galois)      : 0);
    // flags |= (bch_leibniz     ? to_flags(upgrade::bch_leibniz)     : 0);
    // flags |= (bch_cantor     ? to_flags(upgrade::bch_cantor)     : 0);
    // flags |= (bch_unnamed     ? to_flags(upgrade::bch_unnamed)     : 0);
#else
    flags |= (bip141 ? script_flags::bip141_rule : 0);
    flags |= (bip143 ? script_flags::bip143_rule : 0);
    flags |= (bip147 ? script_flags::bip147_rule : 0);
#endif
    return flags;
}

} // namespace kth::blockchain
