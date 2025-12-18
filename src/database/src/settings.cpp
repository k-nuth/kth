// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/settings.hpp>

#include <filesystem>

namespace kth::database {

using namespace std::filesystem;

//TODO(fernando): look for good defaults
constexpr auto db_size_pruned_mainnet  = 100 * (uint64_t(1) << 30); //100 GiB
constexpr auto db_size_default_mainnet = 200 * (uint64_t(1) << 30); //200 GiB
constexpr auto db_size_full_mainnet    = 600 * (uint64_t(1) << 30); //600 GiB

constexpr auto db_size_pruned_testnet4  =  5 * (uint64_t(1) << 30); // 5 GiB
constexpr auto db_size_default_testnet4 = 20 * (uint64_t(1) << 30); //20 GiB
constexpr auto db_size_full_testnet4    = 50 * (uint64_t(1) << 30); //50 GiB

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
