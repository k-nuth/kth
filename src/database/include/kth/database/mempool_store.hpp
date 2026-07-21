// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_MEMPOOL_STORE_HPP
#define KTH_DATABASE_MEMPOOL_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <vector>

#include <kth/domain.hpp>

#include <kth/database/define.hpp>

namespace kth::database {

// One transaction as stored in mempool.dat, with the time it was first seen.
struct mempool_stored_tx {
    transaction_const_ptr tx;
    uint64_t time_seen;
};

// Serialize the transactions to `file`, written atomically (temp sibling +
// rename-over). Entries are ordered by time_seen ascending so parents
// (first-seen earlier) precede their children on reload. Returns false on any
// I/O error. Format: version + count + [tx, time_seen] per entry.
KD_API
bool store_mempool(std::filesystem::path const& file, std::vector<mempool_stored_tx> txs);

// Read a mempool.dat written by store_mempool, in file order (time_seen
// ascending). Empty on a missing / corrupt / wrong-version file (logged; the
// caller just starts without a persisted mempool).
KD_API
std::vector<mempool_stored_tx> load_mempool(std::filesystem::path const& file);

} // namespace kth::database

#endif // KTH_DATABASE_MEMPOOL_STORE_HPP
