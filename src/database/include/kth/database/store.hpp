// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_STORE_HPP
#define KTH_DATABASE_STORE_HPP

#include <filesystem>
#include <memory>

#include <kth/domain.hpp>
#include <kth/database/define.hpp>

#include <kth/infrastructure/utility/sequential_lock.hpp>


namespace kth::database {

struct KD_API store {
    using path = kth::path;
    using handle = sequential_lock::handle;

    // Construct.
    // ------------------------------------------------------------------------
    store(path const& prefix);

    /// Content store.
    path const internal_db_dir;
};

} // namespace kth::database

#endif
