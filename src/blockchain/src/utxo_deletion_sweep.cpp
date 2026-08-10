// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/utxo_deletion_sweep.hpp>

namespace kth::blockchain {

deletion_sweep_outcome run_deletion_sweep(
    std::vector<utxoz::deferred_deletion_entry> owed,
    absence_tolerance const& tolerated,
    deletion_applier const& apply,
    int max_attempts,
    std::function<void(int, utxoz::deletion_progress const&)> const& on_progress,
    utxoz::deferred_deletion_entry* offender) {

    if (owed.empty()) {
        return deletion_sweep_outcome::applied;
    }

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        auto progress = apply(owed);

        if (on_progress) {
            on_progress(attempt, progress);
        }

        // `erased` is retired here and never looked at again — not filtered out
        // of `owed`, not compared against it, not carried forward. The only way
        // a key that was applied can be sent again is if the next request is
        // built from something other than `unresolved`, which is why it is built
        // from `unresolved` alone below.

        // Proven absence. Entitlement is decided from the delta, per key, with a
        // strict obligation dominating a tolerable one.
        for (auto const& entry : progress.absent) {
            auto const it = tolerated.find(entry.key);
            if (it == tolerated.end() || ! it->second) {
                if (offender != nullptr) {
                    *offender = entry;
                }
                return deletion_sweep_outcome::absent_unaccounted;
            }
        }

        if (progress.unresolved.empty()) {
            // Nothing is owed. A fault reported anyway is not a gap in coverage,
            // but it is not a clean run either, and only a clean one may publish.
            if (progress.error) {
                return deletion_sweep_outcome::fault_reported;
            }
            return deletion_sweep_outcome::applied;
        }

        // ONLY unresolved, and rebuilt from the returned entries: the partition
        // is over distinct keys, so filtering the previous request by anything
        // positional would be wrong even when the sizes happen to line up.
        owed.assign(progress.unresolved.begin(), progress.unresolved.end());
    }

    if (offender != nullptr && ! owed.empty()) {
        *offender = owed.front();
    }
    return deletion_sweep_outcome::attempts_exhausted;
}

} // namespace kth::blockchain
