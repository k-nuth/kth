// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/stats.hpp>

#ifdef KTH_WITH_STATS

namespace kth {

sync_stats& global_sync_stats() {
    static sync_stats instance;
    return instance;
}

} // namespace kth

#endif // KTH_WITH_STATS
