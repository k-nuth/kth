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
}

uint32_t settings::enabled_forks() const {
    using namespace domain::machine;

    uint32_t forks = rule_fork::no_rules;
    forks |= (easy_blocks ? rule_fork::easy_blocks : 0);
    forks |= (retarget    ? rule_fork::retarget    : 0);
    forks |= (bip16       ? rule_fork::bip16_rule  : 0);
    forks |= (bip30       ? rule_fork::bip30_rule  : 0);
    forks |= (bip34       ? rule_fork::bip34_rule  : 0);
    forks |= (bip66       ? rule_fork::bip66_rule  : 0);
    forks |= (bip65       ? rule_fork::bip65_rule  : 0);
    forks |= (bip90       ? rule_fork::bip90_rule  : 0);
    forks |= (bip68       ? rule_fork::bip68_rule  : 0);
    forks |= (bip112      ? rule_fork::bip112_rule : 0);
    forks |= (bip113      ? rule_fork::bip113_rule : 0);

#if defined(KTH_CURRENCY_BCH)
    forks |= (bch_uahf        ? rule_fork::bch_uahf : 0);
    forks |= (bch_daa_cw144   ? rule_fork::bch_daa_cw144 : 0);
    forks |= (bch_pythagoras  ? rule_fork::bch_pythagoras : 0);
    forks |= (bch_euclid      ? rule_fork::bch_euclid : 0);
    forks |= (bch_pisano      ? rule_fork::bch_pisano : 0);
    forks |= (bch_mersenne    ? rule_fork::bch_mersenne : 0);
    forks |= (bch_fermat      ? rule_fork::bch_fermat : 0);
    forks |= (bch_euler       ? rule_fork::bch_euler : 0);
    forks |= (bch_gauss       ? rule_fork::bch_gauss : 0);
    forks |= (bch_descartes   ? rule_fork::bch_descartes : 0);
    forks |= (bch_lobachevski ? rule_fork::bch_lobachevski : 0);
    forks |= (bch_galois      ? rule_fork::bch_galois : 0);
    forks |= (bch_leibniz     ? rule_fork::bch_leibniz : 0);
    // forks |= (bch_cantor     ? rule_fork::bch_cantor : 0);
    // forks |= (bch_unnamed     ? rule_fork::bch_unnamed : 0);
#else
    forks |= (bip141 ? rule_fork::bip141_rule : 0);
    forks |= (bip143 ? rule_fork::bip143_rule : 0);
    forks |= (bip147 ? rule_fork::bip147_rule : 0);
#endif
    return forks;
}

} // namespace kth::blockchain
