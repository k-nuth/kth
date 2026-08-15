// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/database/detail/` is excluded from the install rules, so nothing here is
// part of the surface a consumer can reach. It exists so a decision that is
// worth testing directly does not have to become a public symbol to be tested —
// the tests in this tree include it by path, a downstream consumer cannot.

#ifndef KTH_DATABASE_DETAIL_STORAGE_FAILURE_HPP_
#define KTH_DATABASE_DETAIL_STORAGE_FAILURE_HPP_

#include <utxoz/utxoz.hpp>

#include <kth/database/databases/result_code.hpp>

namespace kth::database::detail {

/// The one conversion from a UTXO-Z error to one of ours.
///
/// Folding these into `key_not_found` is the sentinel conversion this migration
/// exists to avoid: upstream reads `key_not_found` as absence, so a database
/// that could not be read, reported that way, answers "absent" — which the
/// validator reads as "spent". Nothing here ever returns it.
///
/// Three outcomes, and they are three different things to do:
///
///   * `not_resolved` — an ordinary miss. The active versions cannot say and
///     nothing was queued; the caller collects and resolves as a batch. Given a
///     code of its own rather than `key_not_found`, because a caller that reads
///     it as absence reports an output as spent having stopped asking halfway;
///   * `recovery_required` — the library has latched. It refuses every further
///     read and every further write until the database is closed and reopened,
///     and it says so in a category of its own. Kept as that category: folding
///     it into `other` loses the only part an operator can act on, and `other`
///     invites a retry that cannot succeed;
///   * everything else — a genuine failure of this node's storage, with no
///     single action attached to it. `other`, and the caller's log carries the
///     library's own name for it.
///
/// `recovery_failed` is deliberately NOT mapped. It means recovery ran and could
/// not act, which is a fact about a database that did not open — it never
/// reaches a read or a write, and telling an operator to close and reopen would
/// send them round a loop that has already been tried.
///
/// constexpr so the pairs can be pinned at compile time as well as swept at run
/// time: a mapping that changes has to fail a build, not only a test run.
[[nodiscard]] constexpr
result_code storage_failure_to_result_code(utxoz::error_code code) {
    switch (code) {
        case utxoz::error_code::not_resolved:
            return result_code::not_resolved;
        case utxoz::error_code::recovery_required:
            return result_code::recovery_required;
        default:
            return result_code::other;
    }
}

// The three that decide whether a fault is read as absence, as a retryable miss,
// or as a store that has stopped answering. Pinned here so the mapping cannot
// drift without a compiler saying so, independently of whether the test binary
// is built or run.
static_assert(storage_failure_to_result_code(utxoz::error_code::not_resolved)
    == result_code::not_resolved);
static_assert(storage_failure_to_result_code(utxoz::error_code::recovery_required)
    == result_code::recovery_required);
static_assert(storage_failure_to_result_code(utxoz::error_code::not_found)
    != result_code::key_not_found);

} // namespace kth::database::detail

#endif // KTH_DATABASE_DETAIL_STORAGE_FAILURE_HPP_
