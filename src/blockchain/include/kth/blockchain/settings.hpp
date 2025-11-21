// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_SETTINGS_HPP
#define KTH_BLOCKCHAIN_SETTINGS_HPP

#include <cstdint>
#include <filesystem>

#include <kth/domain.hpp>
#include <kth/infrastructure/config/endpoint.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

/// Common blockchain configuration settings, properties not thread safe.
struct KB_API settings {
    settings() = default;
    settings(domain::config::network net);

    /// Fork flags combiner.
    uint32_t enabled_forks() const;

    /// Properties.
    uint32_t cores = 0;
    bool priority = true;
    float byte_fee_satoshis = 0.1f;
    float sigop_fee_satoshis= 100.0f;
    uint64_t minimum_output_satoshis = 500;
    uint32_t notify_limit_hours = 24;
    uint32_t reorganization_limit = 256;
    infrastructure::config::checkpoint::list checkpoints;
    bool fix_checkpoints = true;
    bool allow_collisions = true;
    bool easy_blocks = false;
    bool retarget = true;
    bool bip16 = true;
    bool bip30 = true;
    bool bip34 = true;
    bool bip66 = true;
    bool bip65 = true;
    bool bip90 = true;
    bool bip68 = true;
    bool bip112 = true;
    bool bip113 = true;

#if defined(KTH_CURRENCY_BCH)

    bool bch_uahf = true;
    bool bch_daa_cw144 = true;
    bool bch_pythagoras = true;
    bool bch_euclid = true;
    bool bch_pisano = true;
    bool bch_mersenne = true;
    bool bch_fermat = true;          // 2020-May
    bool bch_euler = true;           // 2020-Nov
                                     // 2021-May (no HF)
    bool bch_gauss = true;           // 2022-May
    bool bch_descartes = true;       // 2023-May
    bool bch_lobachevski = true;     // 2024-May
    bool bch_galois = true;          // 2025-May

    // Those are post-upgrade constants, change them to true when the time comes
    // bool bch_leibniz = false;     // 2026-May
    // bool bch_cantor = false;      // 2027-May
    // bool bch_unnamed = false;     // ????-May

    ////2017-Aug-01 hard fork, defaults to 478559 (Mainnet)
    // size_t uahf_height = 478559;

    ////2017-Nov-13 hard fork, defaults to 504031 (Mainnet)
    // size_t daa_height = 504031;

    ////2018-May-15 hard fork, defaults to 1526400000
    // uint64_t pythagoras_activation_time = bch_pythagoras_activation_time;

    ////2018-Nov-15 hard fork, defaults to 1542300000
    // uint64_t euclid_activation_time = bch_euclid_activation_time;

    // //2019-May-15 hard fork, defaults to 1557921600: Wed, 15 May 2019 12:00:00 UTC protocol upgrade
    // uint64_t pisano_activation_time = bch_pisano_activation_time;

    // //2019-May-15 hard fork, defaults to 1573819200: Nov 15, 2019 12:00:00 UTC protocol upgrade
    // uint64_t mersenne_activation_time = bch_mersenne_activation_time;

    // //2020-May-15 hard fork, defaults to 1589544000: May 15, 2020 12:00:00 UTC protocol upgrade
    // uint64_t fermat_activation_time = std::to_underlying(bch_fermat_activation_time);

    // //2020-Nov-15 hard fork, defaults to 1605441600: Nov 15, 2020 12:00:00 UTC protocol upgrade
    // uint64_t euler_activation_time = std::to_underlying(bch_euler_activation_time);

    // 2021-May-15: No hard fork on May 15, 2021.

    // //2022-May-15 hard fork, defaults to 1652616000: May 15, 2022 12:00:00 UTC protocol upgrade
    // uint64_t gauss_activation_time = std::to_underlying(bch_gauss_activation_time);

    // //2023-May-15 hard fork, defaults to 1684152000: May 15, 2023 12:00:00 UTC protocol upgrade
    // uint64_t descartes_activation_time = std::to_underlying(bch_descartes_activation_time);

    // //2024-May-15 hard fork, defaults to 1715774400: May 15, 2024 12:00:00 UTC protocol upgrade
    // uint64_t lobachevski_activation_time = std::to_underlying(bch_lobachevski_activation_time);

    // // 2025-May-15 hard fork, defaults to 1747310400: May 15, 2025 12:00:00 UTC protocol upgrade
    // uint64_t galois_activation_time = std::to_underlying(bch_galois_activation_time);

    // 2026-May-15 hard fork, defaults to 1778846400: May 15, 2026 12:00:00 UTC protocol upgrade
    uint64_t leibniz_activation_time = std::to_underlying(bch_leibniz_activation_time);

    // 2027-May-15 hard fork, defaults to xxxxxxxxxx: May 15, 2027 12:00:00 UTC protocol upgrade
    uint64_t cantor_activation_time = std::to_underlying(bch_cantor_activation_time);

    // //????-???-?? hard fork, defaults to 9999999999: ??? ??, ???? 12:00:00 UTC protocol upgrade
    // uint64_t unnamed_activation_time = std::to_underlying(bch_unnamed_activation_time);

    // The half life for the ASERTi3-2d DAA. For every (asert_half_life) seconds behind schedule the blockchain gets, difficulty is cut in half.
    // Doubled if blocks are ahead of schedule.
    uint64_t asert_half_life = 2ull * 24 * 60 * 60;   //two days

    uint64_t default_consensus_block_size;
    kth::domain::chain::abla::config abla_config {};

#else
    // Just for Segwit coins
    bool bip141 = true;
    bool bip143 = true;
    bool bip147 = true;
#endif //KTH_CURRENCY_BCH

#if defined(KTH_WITH_MEMPOOL)
    size_t mempool_max_template_size = mining::mempool::max_template_size_default;
    size_t mempool_size_multiplier = mining::mempool::mempool_size_multiplier_default;
#endif

};

} // namespace kth::blockchain

#endif
