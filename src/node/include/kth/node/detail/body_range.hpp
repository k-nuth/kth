// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed.
//
// `kth/node/detail/` is excluded from the install rules (src/node/CMakeLists.txt),
// so nothing here is part of the surface a consumer can reach. These are the sync
// coordinator's own decisions and steps, named so a control can drive the real
// path — the defect they exist for is a call that was never made, and a decision
// tested on its own does not show that.
//
// KND_API is on them anyway, and that is not a contradiction: the install rules
// decide what is SOURCE-visible to a consumer, and the export macro decides what
// is SYMBOL-visible across a binary boundary. These are defined in the node
// library and called from the test binary, which is such a boundary — on MSVC
// across a DLL edge, and on ELF as soon as this project builds shared or the
// visibility check in src/node/CMakeLists.txt is repaired. Exporting an internal
// symbol keeps it internal; omitting the macro only makes the link fragile.

#ifndef KTH_NODE_DETAIL_BODY_RANGE_HPP_
#define KTH_NODE_DETAIL_BODY_RANGE_HPP_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/define.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/orchestrator.hpp>

namespace kth::node::sync {

/// What made the coordinator look at the body range again.
///
/// Named after the events themselves, so a log line says which door was taken
/// rather than a category invented for the log.
enum class body_range_trigger {
    /// A batch of headers was validated and the tip moved. THE door this defect
    /// was about: the range's end is the header tip as it stood when the range
    /// was opened, and nothing else notices when that number changes.
    headers_advanced,
    /// The header task reported that every peer is at its tip.
    header_sync_complete,
    /// A post-checkpoint block finished validating.
    block_validated,
    /// A pre-checkpoint chunk finished validating.
    chunk_validated,
    /// The builder published a new height.
    utxo_build_advanced,
    /// A reorganization rewound the bodies and the branch above the fork has to
    /// be downloaded again. That path sends its range directly — the UTXO
    /// admission is about a set that describes the chain BELOW the range, and a
    /// switch has just moved what that means — but it names itself here so every
    /// range in the log speaks one vocabulary.
    reorg
};

/// `body_range_trigger` as text, for the one log line that reports it.
[[nodiscard]] KND_API
char const* to_string(body_range_trigger trigger);

/// The block range to ask the download supervisor for.
struct slow_sync_range {
    uint32_t start;
    uint32_t end;

    friend bool operator==(slow_sync_range const&, slow_sync_range const&) = default;
};

/// Why no range was opened. Reported when it CHANGES, never as a state.
enum class range_quiet : uint8_t {
    none = 0,
    /// The headers have not moved past the bodies.
    no_advance,
    /// The FAST range still owns these heights.
    below_checkpoint,
    /// The previous range is still in flight.
    in_flight
};

/// Why no range is owed, given the same four numbers.
///
/// THE statement of the rule. next_slow_sync_range answers from this rather than
/// repeating the conditions, and so does the log line that names the reason: two
/// hand-kept copies of the same three predicates would drift, and the one that
/// drifts silently is the log — it would report a cause that is no longer the
/// one that decided.
///
/// `none` means a range IS owed.
[[nodiscard]] KND_API
range_quiet why_no_range(
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    uint32_t checkpoint_height,
    std::optional<uint32_t> range_end);

/// Which body range is owed, given where headers and bodies actually are.
///
/// @par Why this is not a bool
/// It used to be one: `slow_sync_started`, true once the post-checkpoint range
/// had been sent. A bool records THAT a range was opened and cannot record
/// WHICH, and the range's end is the header tip as it stood at that instant. A
/// header tip that moves afterwards leaves the bool true, the coordinator
/// complete, every worker exiting with nothing to claim, and the remaining
/// bodies never requested. That is the whole defect; carrying the end height
/// instead of a flag is what closes it.
///
/// @par The range in flight is left alone
/// A range whose bodies have not all arrived is not replaced, because replacing
/// it stops its coordinator and abandons the chunks its workers hold. The
/// remainder is opened when that range has drained, from the event that drains
/// it — which is the same door, taken later, rather than an advance that is lost.
///
/// @param blocks_synced_to  Highest body validated.
/// @param headers_synced_to Highest header validated.
/// @param checkpoint_height Where full validation begins.
/// @param range_end         End of the range last sent, or nullopt if none was.
[[nodiscard]] KND_API
std::optional<slow_sync_range> next_slow_sync_range(
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    uint32_t checkpoint_height,
    std::optional<uint32_t> range_end);

/// What the recorded range end becomes when a reorganization rewinds the bodies.
///
/// An end above the height the bodies were rewound to describes a range over
/// blocks that are no longer on the chain. next_slow_sync_range reads it as a
/// range still in flight and opens nothing, so the branch above the fork is never
/// refilled — the stall this whole change is about, reached through the reorg
/// door instead of the header one.
///
/// Applied at the rewind itself rather than on the path that re-drives the
/// download, because the paths that do NOT re-drive it are the ones that leave
/// the stale value behind: a send that failed, a switch to a tip that is not
/// above the fork, and a reorg that was abandoned.
///
/// @param range_end  End of the range last sent, or nullopt.
/// @param rewound_to The height the bodies were rewound to.
[[nodiscard]] KND_API
std::optional<uint32_t> range_end_after_rewind(
    std::optional<uint32_t> range_end, uint32_t rewound_to);

/// What asking for a body range needs.
struct body_range_deps {
    blockchain::block_chain& chain;
    kth::node::p2p_node& network;
    block_download_input_channel& block_download_input;
    uint32_t checkpoint_height;
};


/// What the body-range decision remembers between events, purely so the logs can
/// report a CHANGE instead of a state.
///
/// Nothing here is consulted by a decision: the decision is a function of the
/// heights. Kept together so the caller carries one thing rather than three, and
/// so a control can watch what was reported.
struct body_range_log_memory {
    /// The last admission reported, so a hold is named once.
    slow_sync_admission last_hold{slow_sync_admission::start};
    /// The last reason nothing was opened.
    range_quiet last_quiet{range_quiet::none};
    /// The (headers, bodies) pair a standstill was last warned about.
    std::optional<std::pair<uint32_t, uint32_t>> last_stall;
};

/// Ask the download supervisor for a range, retrying until it is taken.
///
/// @return false only when the node is winding down. A dropped request would
///         leave the download coordinator uncreated with nothing to re-drive it,
///         so this waits rather than giving up. That it can hold the coordinator's
///         event loop while a channel is not drained is a pre-existing property of
///         this call, and closing it needs a debt with an owner — see the issue
///         raised for it, not a cap bolted on here.
[[nodiscard]] KND_API
::asio::awaitable<bool> send_block_range(body_range_deps deps, uint32_t start, uint32_t end);

/// Decide whether a body range is owed, and ask for it.
///
/// The whole path from "these are the heights" to "the supervisor was asked":
/// next_slow_sync_range decides the range, may_start_slow_sync decides whether
/// the UTXO set can validate it, and the request goes out on the real channel.
///
/// @return The new end of the range in flight, or nullopt if nothing was opened.
///         A range that could not be sent records no end, so the next event
///         re-derives it rather than holding behind one that was never sent.
[[nodiscard]] KND_API
::asio::awaitable<std::optional<uint32_t>> open_body_range_if_owed(
    body_range_deps deps,
    body_range_log_memory& memory,
    body_range_trigger trigger,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end);

/// What a validated batch of headers moved.
struct headers_progress {
    /// The validated header tip after this batch.
    uint32_t headers_synced_to;
    /// The new end of the body range in flight, when one was opened.
    std::optional<uint32_t> opened;
};

/// A validated batch of headers: move the tip, and look at what the bodies are
/// owed now.
///
/// @par Why the error is not consulted
/// `header_validation_task` publishes one message for both: `count` is how many
/// headers the organizer ACCEPTED and `result` is why it stopped. A batch that
/// added a valid prefix and then hit a bad header carries both — count > 0 AND an
/// error — and that prefix moved the tip exactly as a clean batch would. Reading
/// the error here would leave the bodies behind a tip that really did advance,
/// which is the defect this whole change is about, reached through the other
/// door.
///
/// What the error decides is what to do NEXT — ask this peer again or another one
/// — and that belongs to the caller, which is why asking for the next batch is
/// not done here.
///
/// A batch that added nothing moves nothing and opens nothing, whether or not it
/// carried an error.
///
/// Named rather than left inline because the defect is a CALL SITE, not a
/// decision. A control over the decision alone stays green with the defect fully
/// restored, since under it the decision is never reached.
[[nodiscard]] KND_API
::asio::awaitable<headers_progress> on_headers_advanced(
    body_range_deps deps,
    body_range_log_memory& memory,
    headers_validated const& result,
    uint32_t blocks_synced_to,
    uint32_t headers_synced_to,
    std::optional<uint32_t> range_end);

} // namespace kth::node::sync

#endif // KTH_NODE_DETAIL_BODY_RANGE_HPP_
