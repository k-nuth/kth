// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/store.hpp>
#include <kth/domain.hpp>

#include <cstddef>
#include <memory>

namespace kth::database {

using namespace kth::domain::chain;
using namespace kth::database;

#define INTERNAL_DB_DIR "internal_db"

// Construct.
// ------------------------------------------------------------------------

store::store(path const& prefix)
    : internal_db_dir(prefix / INTERNAL_DB_DIR) {}

}  // namespace kth::database
