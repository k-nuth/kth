// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/settings.hpp>

#include <filesystem>

#include <kth/infrastructure/utility/units.hpp>

namespace kth::database {

using namespace std::filesystem;
using namespace kth; // for _GiB literals

// LMDB now only stores: headers, properties, reorg pool
// Blocks are stored in flat files (blk*.dat), UTXO in UTXOZ
constexpr auto db_size_mainnet  = 2_GiB;
constexpr auto db_size_testnet4 = 1_GiB;

settings::settings()
    : directory(u8"blockchain")
    , db_mode(db_mode_type::blocks)
    , reorg_pool_limit(100)      //TODO(fernando): look for a good default
    , db_max_size(db_size_mainnet)
    , safe_mode(true)
    , cache_capacity(0)
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
        case domain::config::network::testnet4:
        case domain::config::network::scalenet:
        case domain::config::network::chipnet: {
            db_max_size = db_size_testnet4;
            break;
        }
#endif // defined(KTH_CURRENCY_BCH)
    }
}

} // namespace kth::database
