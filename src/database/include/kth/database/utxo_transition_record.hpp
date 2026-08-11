// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_UTXO_TRANSITION_RECORD_HPP_
#define KTH_DATABASE_UTXO_TRANSITION_RECORD_HPP_

#include <cstdint>
#include <expected>
#include <span>

#include <kth/database/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::database {

// The durable statement that a chain transition started and has not been
// recorded as finished (issue #600).
//
// It is the authority on WHETHER the last transition finished, never on HOW to
// repair the stores. Finding one at startup means an explicit rebuild, not a
// resume: the delta mutates the UTXO maps in place, so there is nothing to roll
// back and reapplying would repeat mutations already in the set.
//
// ---------------------------------------------------------------------------
// The envelope is fixed forever, outside the format version
// ---------------------------------------------------------------------------
//
//   * the first two bytes are the format version, little-endian;
//   * the last four bytes are the checksum;
//   * the checksum is `bitcoin_checksum` over every byte before it.
//
// Nothing in a future version may move or redefine those three things. That is
// what makes the validation order — length, then checksum, then version, then
// the rest — sound: a field cannot be trusted before the checksum that covers
// it, and the checksum cannot be located by a version that has not been trusted
// yet. A format that versioned its own checksum would force a reader to act on
// an unverified version byte to decide how to verify it, which is the cycle
// this rule exists to break.
//
// Everything BETWEEN the version and the checksum is the payload, and a future
// version may lay it out however it likes, including a different length: the
// envelope rules are stated in terms of the two ends, not of any fixed size.
//
// Serialization is explicit and byte-wise. No struct is written raw: native
// layout, padding and endianness are not part of this format, and a record
// written by one build is read the same way by every other.

enum class transition_type : uint16_t {
    connect_batch = 1,
    reorg         = 2,
};

enum class transition_state : uint8_t {
    // The only state defined. The field exists so that a later one — should
    // recovery ever become something the node performs rather than demands —
    // does not need a format version bump.
    in_progress = 1,
};

struct utxo_transition_record {
    static constexpr uint16_t current_format_version = 1;

    // Version 1 is 25 bytes: 2 version + 2 type + 8 id + 4 first + 4 intended
    // + 1 state + 4 checksum. Named rather than spelled out at the call sites.
    static constexpr size_t version_1_size = 25;

    // The smallest byte string that could carry an envelope at all. A shorter
    // value cannot even be checked, let alone decoded.
    static constexpr size_t minimum_envelope_size = 2 + 4;

    uint16_t format_version{current_format_version};
    transition_type type{transition_type::connect_batch};

    // Correlates the log line written when a transition fails with the record
    // found at the next startup. Drawn from system entropy, never from a clock
    // or a pid: those collide exactly when several nodes are started together
    // by the same script, which is when a diagnosis needs them to differ.
    // Nothing branches on this value.
    uint64_t operation_id{0};

    // The lowest height whose UTXO state the transition rewrites. For a connect
    // batch that is the first block applied; for a reorganization, the first
    // block disconnected — one above the fork.
    uint32_t first_height{0};

    // The highest height in the same range: for a connect batch the last block
    // it means to apply, for a reorganization the tip it starts from.
    //
    // INTENDED. It is written before the work, so it says what was attempted
    // and never what was achieved. A reader that takes it for a completed
    // height reintroduces the whole problem.
    uint32_t intended_last_height{0};

    transition_state state{transition_state::in_progress};

    friend bool operator==(utxo_transition_record const&,
                           utxo_transition_record const&) = default;
};

// Why a record could not be read. Kept apart from "there is no record": absence
// is an answer, and a damaged record is not.
enum class transition_decode_error {
    too_short,          ///< Fewer bytes than an envelope needs.
    checksum_mismatch,  ///< The bytes are not the ones that were written.
    unknown_version,    ///< Written by a build that knows something this one does not.
    unknown_type,       ///< A transition kind this build cannot reason about.
    unknown_state,      ///< A state this build cannot reason about.
    wrong_size,         ///< Right version, wrong length for it.
};

/// Draw a fresh operation id from system entropy.
/// PRECONDITION: `pseudo_random::check_available()` succeeded in this process,
/// which the node establishes at startup.
[[nodiscard]]
KD_API uint64_t make_operation_id();

/// Serialize. The result is exactly what a reader will checksum.
[[nodiscard]]
KD_API data_chunk encode(utxo_transition_record const& record);

/// Parse and validate, in the only order the envelope permits.
[[nodiscard]]
KD_API std::expected<utxo_transition_record, transition_decode_error>
decode_transition_record(std::span<uint8_t const> bytes);

/// For logs and for the refusal message a node prints before it stops.
[[nodiscard]]
KD_API char const* to_string(transition_type type);

[[nodiscard]]
KD_API char const* to_string(transition_decode_error error);

} // namespace kth::database

#endif // KTH_DATABASE_UTXO_TRANSITION_RECORD_HPP_
