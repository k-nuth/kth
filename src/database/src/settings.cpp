// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/settings.hpp>

#include <filesystem>

#include <kth/infrastructure/utility/units.hpp>

namespace kth::database {

using namespace std::filesystem;
using namespace kth; // for _GiB literals

// BCH blockchain is ~215 GB as of Jan 2026 (block ~934k)
constexpr auto db_size_pruned_mainnet  = 200_GiB;
constexpr auto db_size_default_mainnet = 400_GiB;
constexpr auto db_size_full_mainnet    = 600_GiB;

constexpr auto db_size_pruned_testnet4  =  5_GiB;
constexpr auto db_size_default_testnet4 = 20_GiB;
constexpr auto db_size_full_testnet4    = 50_GiB;

constexpr
auto get_db_max_size_mainnet(db_mode_type mode) {
    return mode == db_mode_type::pruned
        ? db_size_pruned_mainnet
        : mode == db_mode_type::blocks
            ? db_size_default_mainnet
            : db_size_full_mainnet;
}

constexpr auto get_db_max_size_testnet4(db_mode_type mode) {
    return mode == db_mode_type::pruned
        ? db_size_pruned_testnet4
        : mode == db_mode_type::blocks
            ? db_size_default_testnet4
            : db_size_full_testnet4;
}

settings::settings()
    : directory(u8"blockchain")
    , db_mode(db_mode_type::blocks)
    , reorg_pool_limit(100)      //TODO(fernando): look for a good default
    , db_max_size(get_db_max_size_mainnet(db_mode))
    , safe_mode(true)
    , cache_capacity(0)
    , parallel_block_push(true)  // default to parallel (original behavior)
{}

settings::settings(domain::config::network context)
    : settings()
{
    switch (context) {
        case domain::config::network::mainnet: {
            break;
        }
        case domain::config::network::testnet: {
            break;
        }
        case domain::config::network::regtest: {
            break;
        }

#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4: {
            db_max_size = get_db_max_size_testnet4(db_mode);
            break;
        }
        case domain::config::network::scalenet: {
            db_max_size = get_db_max_size_testnet4(db_mode);
            break;
        }
        case domain::config::network::chipnet: {
            db_max_size = get_db_max_size_testnet4(db_mode);
            break;
        }

#endif // defined(KTH_CURRENCY_BCH)
    }
}

} // namespace kth::database
