// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_RESULT_CODE_HPP_
#define KTH_DATABASE_RESULT_CODE_HPP_

#include <array>
#include <cstddef>
#include <string_view>

namespace kth::database {

enum class result_code {
    success = 0,
    success_duplicate_coinbase = 1,
    duplicated_key = 2,
    key_not_found = 3,
    db_empty = 4,
    no_data_to_prune = 5,
    db_corrupt = 6,
    prune_error = 7,
    other = 8,

    /// The active versions cannot answer; the older ones were not consulted.
    ///
    /// NOT key_not_found, and the distinction is the point. UTXO-Z 0.10.0 named
    /// it not_resolved for the same reason: absence is a fact about the
    /// database, and this is a fact about which files were looked in. A caller
    /// that flattens the two reports an output as spent because it did not
    /// finish asking.
    not_resolved = 9,

    /// The store will answer nothing further until it is closed and rebuilt.
    ///
    /// It does NOT claim corruption. What it claims is narrower and is the only
    /// thing that can be known from inside: an operation that may have applied
    /// part of its work did not finish, so what is on disk is neither the state
    /// before it nor the state after. Whether that state is recoverable is a
    /// question for the restart, which consults the transition record; this code
    /// only says the restart has to happen.
    ///
    /// It is not absence and not a pending resolution — a caller that reads it
    /// as either reports an output as spent on the strength of a store that
    /// stopped answering. Every UTXO operation after it is refused with this
    /// same code, so a caller cannot make progress by retrying.
    ///
    /// UTXO-Z has a code of the same name and the same meaning. When it reports
    /// one, this is what it becomes: the category is preserved rather than
    /// folded into `other`, because `other` tells an operator nothing about what
    /// to do next.
    recovery_required = 10,

    /// One past the last valid code. Never returned, never stored, never
    /// compared against — it exists so the table below can be sized from the
    /// enum rather than from a number kept in step by hand.
    _count = 11
};

inline
bool succeed(result_code code) {
    return code == result_code::success || code == result_code::success_duplicate_coinbase;
}

inline
bool succeed_prune(result_code code) {
    return code == result_code::success || code == result_code::no_data_to_prune;
}

namespace detail {

/// Indexed by the enum's own value, so a code added without a name here shifts
/// nothing and silently takes another code's text — which is why the assertions
/// below exist rather than a comment asking for care.
inline constexpr std::array<char const*, static_cast<size_t>(result_code::_count)>
result_code_names{
    "success",
    "success_duplicate_coinbase",
    "duplicated_key",
    "key_not_found",
    "db_empty",
    "no_data_to_prune",
    "db_corrupt",
    "prune_error",
    "other",
    "not_resolved",
    "recovery_required",
};

static_assert(result_code_names.size() == static_cast<size_t>(result_code::_count),
    "every result_code needs a name of its own: add it to result_code_names, in "
    "the enum's order");

/// The table is positional, so being the right SIZE is not the same as being in
/// the right ORDER. These pin the codes whose meaning a caller must never see as
/// another's, against their WHOLE name: a first character would still accept
/// `recovery_failed` where `recovery_required` belongs, and those two ask an
/// operator for different things.
static_assert(std::string_view(result_code_names[static_cast<size_t>(result_code::success)])
    == "success");
static_assert(std::string_view(result_code_names[static_cast<size_t>(result_code::key_not_found)])
    == "key_not_found");
static_assert(std::string_view(result_code_names[static_cast<size_t>(result_code::not_resolved)])
    == "not_resolved");
static_assert(std::string_view(result_code_names[static_cast<size_t>(result_code::recovery_required)])
    == "recovery_required");

} // namespace detail

/// The one place a result_code becomes text.
///
/// A value outside the enum is reported as itself rather than as a word: it is
/// a defect somewhere upstream, and "unrecognised" hides which value arrived.
/// Nothing here casts an out-of-range integer back to the enum to ask it
/// questions — that is undefined, and it is how such a value would go unnoticed.
[[nodiscard]] inline
char const* result_code_name(result_code code) {
    auto const index = static_cast<size_t>(code);
    if (index >= detail::result_code_names.size()) {
        return "invalid result_code";
    }
    return detail::result_code_names[index];
}

/// Whether the store has latched and every further operation will be refused.
///
/// Asked rather than compared inline so the one place that decides "this needs a
/// restart, not a retry" is named.
[[nodiscard]] inline
bool needs_recovery(result_code code) {
    return code == result_code::recovery_required;
}

} // namespace kth::database

#endif // KTH_DATABASE_RESULT_CODE_HPP_
