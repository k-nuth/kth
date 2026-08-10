// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_UTXO_DELETION_SWEEP_HPP
#define KTH_BLOCKCHAIN_UTXO_DELETION_SWEEP_HPP

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <utxoz/utxoz.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

/// Per key, whether a PROVEN absence may be tolerated.
///
/// Built per block from that block's own inverse delta and folded with AND, so
/// one appearance that requires the key to exist and be deleted dominates every
/// appearance that would excuse it. See disconnect_block.
using absence_tolerance = boost::unordered_flat_map<utxoz::raw_outpoint, bool>;

/// The connect path's tolerance: none.
///
/// A connect batch nets out anything created and spent inside itself before the
/// delta is applied, so every key it asks to delete WAS in the set. A proven
/// absence there is not a legitimate no-op — it is a UTXO set that does not
/// match the blocks — and there is no per-block classification to consult.
///
/// Named rather than passing an empty map at the call site, so the strictness is
/// a stated policy instead of a property of a literal someone might "fix".
[[nodiscard]]
inline absence_tolerance const& strict_absence() {
    static absence_tolerance const none;
    return none;
}

/// Fold one block's verdict about one key into the rewind's obligation.
///
/// CONSERVATIVE, and that is the whole content: a key is tolerated absent only
/// while EVERY block that asked for its deletion created and spent it itself. A
/// single appearance that requires the key to exist and be deleted dominates
/// every appearance that would excuse it, so the values AND together.
///
/// Outpoints are unique, so today each key is contributed by exactly one block
/// and the fold never combines two verdicts. It is written to combine anyway:
/// the batch is applied once at the end and UTXO-Z answers once per DISTINCT
/// key, so the moment anything makes a key reachable from two blocks — a
/// duplicate coinbase under pre-BIP30 rules, or any future path that widens the
/// obligation — a union would let one block's legitimate no-op excuse another
/// block's real deletion, silently.
inline void fold_absence_tolerance(absence_tolerance& tolerated,
                                   utxoz::raw_outpoint const& key,
                                   bool tolerable_here) {
    auto const it = tolerated.find(key);
    if (it == tolerated.end()) {
        tolerated.emplace(key, tolerable_here);
        return;
    }
    it->second = it->second && tolerable_here;
}

/// How a batch of deletions is applied. Injected so the policy below can be
/// exercised against outcomes real storage cannot be made to produce on demand —
/// an unreadable version file is not something a test can conjure, and a policy
/// that is only ever run against the happy path is not one that has been tested.
using deletion_applier =
    std::function<utxoz::deletion_progress(std::span<utxoz::deferred_deletion_entry const>)>;

/// Why a sweep stopped, for the caller's log and for tests to discriminate on.
enum class deletion_sweep_outcome {
    applied,                ///< Every obligation is accounted for
    absent_unaccounted,     ///< A proven absence no block of this rewind is entitled to
    fault_reported,         ///< The walk reported an error, even with nothing left owed
    attempts_exhausted      ///< Still owed after the last attempt
};

/// Apply an obligation, retrying ONLY what is still owed.
///
/// The whole policy of #602's deletion half, in one place and free of storage so
/// it can be tested against every outcome:
///
///  - `erased` is retired permanently, INCLUDING when `error` is set. A deletion
///    writes as it walks, so those keys are gone; resending one would ask about
///    a key that is now genuinely absent, get `absent` back, and turn this
///    operation's own success into a refusal.
///  - `absent` is proven absence — 0.10.0 puts storage faults in `unresolved` —
///    so the only question is whether this operation is entitled to it, and that
///    is answered from the delta it applied rather than from the store.
///  - `unresolved` is the ONLY category resent, rebuilt from what came back
///    rather than filtered out of what was sent, because the partition is over
///    distinct keys and matches by key, never by position.
///  - `error` with nothing left owed still fails: everything is classified, but
///    a transition publishes only over a clean run.
///
/// Retries live inside ONE operation and one process. An interrupted transition
/// is refused at the next start and rebuilt, never resumed — see
/// check_last_transition.
///
/// @param owed The obligation, by value: this consumes it as it shrinks.
/// @param on_progress Optional observer, called once per attempt with that
///        attempt's progress. For logging and for tests; it changes nothing.
[[nodiscard]]
KB_API deletion_sweep_outcome run_deletion_sweep(
    std::vector<utxoz::deferred_deletion_entry> owed,
    absence_tolerance const& tolerated,
    deletion_applier const& apply,
    int max_attempts,
    std::function<void(int, utxoz::deletion_progress const&)> const& on_progress,
    utxoz::deferred_deletion_entry* offender);

} // namespace kth::blockchain

#endif
