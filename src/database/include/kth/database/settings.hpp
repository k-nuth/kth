// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_SETTINGS_HPP
#define KTH_DATABASE_SETTINGS_HPP

#include <kth/database/databases/property_code.hpp>
#include <kth/database/define.hpp>

#include <cstdint>
#include <filesystem>

namespace kth::database {

/// Common database configuration settings, properties not thread safe.
class KD_API settings {
  public:
    settings();
    settings(domain::config::network context);

    /// Properties.
    kth::path directory;
    db_mode_type db_mode;
    uint32_t reorg_pool_limit;
    uint64_t db_max_size;
    bool safe_mode;
    uint32_t cache_capacity;
};

}  // namespace kth::database

#endif
