// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_tasks.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/network/protocols_coro.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;

namespace {

// What a single attempt at a header request left behind, and therefore what
// has to happen next. The task buffers an unfinished request in
// `pending_request`, and nothing outside this file can tell from that buffer
// alone whether the request is waiting on the network or waiting on the task
// itself -- which is precisely how #692 stayed invisible. Naming the three
// endings keeps that distinction in the type rather than in a comment.
enum class request_outcome {
    // Nothing is owed: headers were delivered, or the tip was reported.
    settled,

    // Buffered, and this task will not touch it again. The only thing that
    // re-reads pending_request is the peers_updated handler, so waking up means
    // an inbound message on `input` -- from the peer provider, or from the
    // coordinator. Which of those it is depends on the branch, and each branch
    // says so; the name promises only that a wake, if it comes, comes from
    // outside. It deliberately does not promise that one is coming.
    parked_for_external_event,

    // Buffered, an eligible peer has already been made current, and nothing
    // external is expected. Only the caller can move this forward.
    ask_next_peer,
};

} // namespace

// =============================================================================
// Header Download Task
// =============================================================================

::asio::awaitable<void> header_download_task(
    header_download_input_channel& input,
    header_download_output_channel& output,
    uint32_t max_header_height
) {
    spdlog::debug("[header_download] Task started (max_header_height={})",
        max_header_height == 0 ? "unlimited" : std::to_string(max_header_height));

    // Local copy of peers - updated by peer_provider (already filtered)
    std::vector<network::peer_session::ptr> available_peers;
    std::optional<header_request> pending_request;  // Buffered request waiting for peers

    // Sticky peer selection - use same peer until failure
    network::peer_session::ptr current_peer;

    // Track last failed peer nonce - don't reselect until we get peers_updated
    std::optional<uint64_t> last_failed_nonce;

    // Peers that have already answered this walk without moving the chain, by
    // either of the two ways there are to say it: no headers at all, or headers
    // the chain already holds. Both are the same answer, and asking again
    // reproduces it, so neither is eligible until the walk ends.
    //
    // Marking only the first of the two is what let one peer be asked eleven
    // times for the same height in 1.43 s (#705).
    boost::unordered_flat_set<uint64_t> peers_without_progress;

    // Track best known height from peer VERSION messages
    // This tells us the highest height any peer claims to be at
    uint32_t best_known_height = 0;

    // Connected is not eligible, and the two were counted with the same word.
    // The log said "9 peers available" and "No peers available" in the same
    // millisecond because one is available_peers and the other is what is left
    // after stopped, failed and spent peers come out of it.
    //
    // One predicate, so counting them and choosing one cannot drift apart --
    // two notions of eligible disagreeing is the shape of this whole defect.
    auto is_eligible = [&](network::peer_session::ptr const& p) {
        if (p->stopped()) return false;
        if (last_failed_nonce && p->nonce() == *last_failed_nonce) return false;
        if (peers_without_progress.contains(p->nonce())) return false;
        return true;
    };

    auto eligible_peers = [&]() -> size_t {
        return size_t(std::count_if(available_peers.begin(), available_peers.end(), is_eligible));
    };

    // Helper to compute best known height from available peers
    auto update_best_known_height = [&]() {
        best_known_height = 0;
        for (auto const& p : available_peers) {
            if (p->stopped()) continue;
            if (auto pv = p->peer_version(); pv) {
                best_known_height = std::max(best_known_height, pv->start_height());
            }
        }
    };

    // Helper to get peer for headers (sticky - same peer until failure)
    auto get_header_peer = [&]() -> network::peer_session::ptr {
        // If current peer is still valid and in available list, use it
        if (current_peer && !current_peer->stopped()) {
            // Don't use if it just failed (waiting for peers_updated)
            if (last_failed_nonce && current_peer->nonce() == *last_failed_nonce) {
                current_peer = nullptr;
            } else {
                // Check if still in available_peers (peer_provider may have removed it)
                for (auto const& p : available_peers) {
                    if (p->nonce() == current_peer->nonce()) {
                        return current_peer;
                    }
                }
                // Current peer was removed by peer_provider, clear it
                current_peer = nullptr;
            }
        }

        // Need to select a new peer - skip stopped peers, the last failed one, and peers at their tip
        // NOTE: Don't modify available_peers here - just skip invalid ones
        for (auto const& p : available_peers) {
            if ( ! is_eligible(p)) continue;
            current_peer = p;
            spdlog::info("[header_sync] Using peer: {}", current_peer->authority_with_agent());
            return current_peer;
        }

        // All peers are either stopped, failed, or at their tip - wait for peers_updated
        return nullptr;
    };

    // The one way this task says "header sync is finished", and therefore the
    // one thing that makes block sync start. Awaited rather than try_send:
    // dropped on a full channel it leaves the node at the tip with nothing
    // downloading bodies and nothing left to re-drive it, which is the stall
    // this whole change is about. Answers whether it was delivered; false means
    // the channel is closed, which happens on shutdown and nowhere else.
    auto deliver_completion = [&](header_request const& request,
                                  network::peer_session::ptr const& answered)
        -> ::asio::awaitable<bool> {
        auto [send_ec] = co_await output.async_send(std::error_code{}, downloaded_headers{
            .headers = {},
            .start_height = request.from_height,
            .source_peer = answered
        }, ::asio::as_tuple(::asio::use_awaitable));

        if (send_ec) {
            spdlog::debug("[header_download] Output channel closed while signalling sync complete");
            co_return false;
        }
        co_return true;
    };

    // No eligible peer remains for this request. Two different situations, and
    // the code used to have this decision in only one of them.
    //
    // At or past the highest height any peer claims, every peer having answered
    // without progress IS the answer: the chain is current, and block sync can
    // start on the local tip. Below it, the eligible set is merely exhausted and
    // the answer is not in.
    //
    // Either way it ends somewhere an event can reach: settled, or parked for a
    // peer set that only a real change can supply. What it never does again is
    // park at the tip, where the answer was already in.
    auto settle_without_peer = [&](header_request const& request,
                                   network::peer_session::ptr const& answered)
        -> ::asio::awaitable<request_outcome> {
        // Somebody has to have said it. Without this, a node with no peers at
        // all -- at startup, or after they have all gone -- reads its own empty
        // eligible set as "everyone agrees we are current": best_known_height is
        // 0 with nobody to claim otherwise, so every height is at or past it,
        // and the tip would be confirmed with nothing behind it and block sync
        // started over whatever the chain happened to hold.
        //
        // The tip is confirmed by evidence from peers, never by their absence.
        bool const anyone_answered = ! peers_without_progress.empty();

        // No max-height case here: process_request answers that before it ever
        // looks for a peer, so by the time this runs the only question left is
        // whether we are at the tip.
        bool const reached_tip = anyone_answered && request.from_height >= best_known_height;

        if (reached_tip) {
            spdlog::info("[header_download] No eligible peer left for height {} "
                "({} connected, {} spent) and it is at or past the best height any peer "
                "claims ({}): confirming the tip",
                request.from_height, available_peers.size(),
                peers_without_progress.size(), best_known_height);

            if ( ! co_await deliver_completion(request, answered)) {
                co_return request_outcome::settled;
            }

            // Cleared only now that the confirmation is delivered. The spent set
            // is deliberately NOT cleared here — it is cleared by the next
            // request that names no spent peer, which is what starts a fresh
            // walk — so a second confirmation cannot be manufactured out of the
            // same answers.
            current_peer = nullptr;
            pending_request.reset();
            co_return request_outcome::settled;
        }

        spdlog::warn("[header_download] No eligible peer left for height {} "
            "({} connected, {} spent, best_known={}): waiting",
            request.from_height, available_peers.size(),
            peers_without_progress.size(), best_known_height);

        // Parked, and correctly so: every peer has answered without progress
        // while claiming to be higher, so the headers exist and nobody here is
        // serving them. Nothing this task can do on its own changes that — only
        // a different peer set can — so `peers_updated` is the right and only
        // driver. Re-asking the same peers on a timer would not pay this debt,
        // it would only keep re-checking it.
        pending_request = request;
        co_return request_outcome::parked_for_external_event;
    };

    // Helper lambda to process a request
    auto process_request = [&](header_request const& request) -> ::asio::awaitable<request_outcome> {
        // Check if we've reached the max header height limit (if set)
        if (max_header_height > 0 && request.from_height >= max_header_height) {
            spdlog::info("[header_download] Reached max_header_height {} at from_height={}, signaling sync complete",
                max_header_height, request.from_height);

            // Same delivery as the tip confirmation, and for the same reason:
            // a completion signal dropped on a full channel is a sync that
            // never completes.
            if (co_await deliver_completion(request, nullptr)) {
                pending_request.reset();
            }
            co_return request_outcome::settled;
        }

        auto peer = get_header_peer();
        if (!peer) {
            // This is where the wedge lived: the request was parked here without
            // anyone asking whether being unable to find a peer, at this height,
            // was itself the answer.
            co_return co_await settle_without_peer(request, nullptr);
        }

        spdlog::debug("[header_download] Requesting headers from {} at height {}",
            peer->authority_with_agent(), request.from_height);

        // Download headers with timing
        auto start_time = std::chrono::steady_clock::now();
        // 2026-02-02: Reduced from 30s to 10s to minimize stalls from slow peers
        auto result = co_await network::request_headers_from(
            *peer, request.from_hash, std::chrono::seconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        if (!result) {
            // 2026-01-28: Added detailed logging to diagnose segfault during shutdown.
            // Symptom: After Ctrl-C, we see "Peer X failed: channel stopped" then immediate segfault.
            // Adding checkpoints to identify exactly which operation crashes.
            spdlog::warn("[header_sync] Peer {} failed: {}",
                peer->authority_with_agent(), result.error().message());
            spdlog::warn("[header_sync] checkpoint 1: after log");

            // Mark this peer as failed - don't reselect until peers_updated
            auto const peer_nonce = peer->nonce();
            spdlog::warn("[header_sync] checkpoint 2: got nonce {}", peer_nonce);
            last_failed_nonce = peer_nonce;
            current_peer = nullptr;
            spdlog::warn("[header_sync] checkpoint 3: cleared current_peer");

            // Report failure to output (coordinator will inform peer_provider)
            // Note: During shutdown, the channel may be closed. try_send returns false on closed channel.
            auto const error_to_report = result.error();
            spdlog::warn("[header_sync] checkpoint 4: got error code");
            bool send_ok = output.try_send(std::error_code{}, peer_failure_report{
                .peer = peer,
                .error = error_to_report
            });
            spdlog::warn("[header_sync] checkpoint 5: try_send returned {}", send_ok);
            if (!send_ok) {
                spdlog::warn("[header_download] Channel full or closed, peer failure report dropped");
            }
            // Driver: the coordinator, not peers_updated. The failure report
            // above travels output -> bridge -> header_validation_task ->
            // validated_headers -> coordinator, which answers with a fresh
            // header_request. Every hop is a bounded try_send that logs and
            // drops when full, so this is observed-reliable rather than
            // guaranteed; it fired on every failure across three mainnet runs.
            // Left as it is on purpose: a failure is not "ask the next one".
            pending_request = request;
            spdlog::warn("[header_sync] checkpoint 6: returning from error handler");
            co_return request_outcome::parked_for_external_event;
        }

        if (result->elements().empty()) {
            // Peer has no more headers - mark as "at their tip" and try another peer
            auto peer_start_height = peer->peer_version() ? peer->peer_version()->start_height() : 0;
            spdlog::info("[header_download] Peer {} (start_height={}) returned 0 headers at height {}, trying another peer ({} at tip so far)",
                peer->authority_with_agent(), peer_start_height, request.from_height, peers_without_progress.size() + 1);

            peers_without_progress.insert(peer->nonce());
            current_peer = nullptr;  // Force selection of new peer

            // Check if there's another peer available
            auto next_peer = get_header_peer();
            if (!next_peer) {
                co_return co_await settle_without_peer(request, peer);
            }

            // Another peer is eligible and get_header_peer() has already made it
            // current. Nothing will ask it on its own: the main loop blocks on
            // input.async_receive(), the only reader of pending_request is the
            // peers_updated handler, and this task has no timer. Returning
            // `ask_next_peer` is what makes the caller send it -- without that,
            // the request waits for an unrelated peer to connect or drop, which
            // on BCH mainnet measured 23m25s, 33m47s and 29m51s (#692).
            pending_request = request;
            co_return request_outcome::ask_next_peer;
        }

        auto headers_count = uint32_t(result->elements().size());
        spdlog::debug("[header_download] Received {} headers from {} in {}ms",
            headers_count, peer->authority_with_agent(), elapsed_ms);

        // Send to validation
        if (!output.try_send(std::error_code{}, downloaded_headers{
            .headers = result->elements(),
            .start_height = request.from_height + 1,
            .source_peer = peer
        })) {
            // No driver. Draining this output channel does not wake this task:
            // it reads from `input`, and nothing turns a drop here into an
            // inbound message. The request then waits on unrelated peer churn,
            // exactly the way #692 did. Tracked separately -- the trigger is
            // saturation, not an empty answer, and the fix is backpressure.
            spdlog::warn("[header_download] Channel full, headers dropped");
            pending_request = request;
            co_return request_outcome::parked_for_external_event;
        }

        // Report performance to peer_provider (for slow peer tracking)
        if (!output.try_send(std::error_code{}, header_performance{
            .peer_nonce = peer->nonce(),
            .headers_downloaded = headers_count,
            .download_time_ms = uint32_t(elapsed_ms)
        })) {
            spdlog::debug("[header_download] Channel full, performance report dropped");
            // Non-fatal, continue
        }

        // Clear pending since we processed it
        pending_request.reset();
        co_return request_outcome::settled;
    };

    // Ask, and keep asking while the answer was "not me, ask the next one".
    // Every answer that moved nothing puts its peer in peers_without_progress
    // and get_header_peer() skips those, so this visits each eligible peer at
    // most once and ends either at a peer with headers or at
    // settle_without_peer, which confirms the tip or records the debt.
    // available_peers is not touched inside the walk.
    auto drive_request = [&](header_request request) -> ::asio::awaitable<void> {
        while (co_await process_request(request) == request_outcome::ask_next_peer) {
            // Shutdown closes this channel first and stops the peers after, so
            // for a moment the peers still answer. A walk already under way
            // would spend that moment asking them, and would only notice once
            // it returned to the main loop -- which is where the closed input
            // is observed. Asking here is the same observation, one step
            // earlier, and it costs one flag read rather than a second notion
            // of "are we stopping".
            if ( ! input.is_open()) {
                co_return;
            }

            // process_request re-buffered the same request for the peer it just
            // made current; ask that one now rather than leaving it parked.
            request = *pending_request;
        }
    };

    // Single channel, FIFO processing - no priority issues
    while (true) {
        spdlog::debug("[header_download] Waiting for events ({} connected, {} eligible, pending={})",
            available_peers.size(), eligible_peers(), pending_request.has_value());

        auto [ec, event] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_download] Input channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(event)) {
            spdlog::debug("[header_download] Stop signal received");
            break;
        }

        if (auto* peers_msg = std::get_if<peers_updated>(&event)) {
            available_peers = peers_msg->peers;

            // 2026-01-28: Fix for header sync getting stuck retrying the same failed peer.
            // Symptom: After a peer times out, the log showed:
            //   [header_sync] Peer X failed: connection timed out
            //   [header_sync] Using peer: X   <-- same peer immediately reselected!
            // This repeated every 30 seconds indefinitely, ignoring 7-8 other available peers.
            //
            // Root cause: Race condition between peers_updated messages and peer failures.
            // 1. peer_provider broadcasts peers (peer X in list)
            // 2. header_download waits 30s for peer X response
            // 3. Meanwhile, new peers connect -> peer_provider broadcasts again (X still in list)
            // 4. Timeout fires, X fails -> last_failed_nonce = X
            // 5. header_download processes queued peers_updated from step 3
            // 6. OLD CODE: last_failed_nonce.reset() unconditionally <- BUG! Clears protection
            // 7. header_download selects X again (still in the stale list)
            //
            // Fix: Only clear last_failed_nonce if the failed peer is NOT in the new list.
            // If the failed peer is still present, this is a stale message from before the
            // failure, so we keep the protection active.
            if (last_failed_nonce) {
                bool failed_peer_still_present = false;
                for (auto const& p : available_peers) {
                    if (p->nonce() == *last_failed_nonce) {
                        failed_peer_still_present = true;
                        break;
                    }
                }
                if (!failed_peer_still_present) {
                    spdlog::debug("[header_download] Failed peer {} no longer in list, clearing protection",
                        *last_failed_nonce);
                    last_failed_nonce.reset();
                } else {
                    spdlog::debug("[header_download] Failed peer {} still in list, keeping protection",
                        *last_failed_nonce);
                }
            }

            // Update best known height from all peers
            update_best_known_height();

            spdlog::debug("[header_download] Peers updated: {} peers available, best_known_height={}",
                available_peers.size(), best_known_height);

            // 2026-02-02: Fix infinite loop in header sync completion detection.
            //
            // Bug: Previously we cleared ALL peers_without_progress on every peers_updated message.
            // Since peers_updated fires frequently (peer connects/disconnects), we never
            // reached the "all peers at tip" condition needed to complete header sync.
            // The loop was:
            //   1. BCHN peers return 0 headers → marked as at_tip
            //   2. peers_updated arrives → clears peers_without_progress
            //   3. Retry same BCHN peers → return 0 → marked as at_tip
            //   4. Another peers_updated → ... infinite loop
            //
            // Fix: Only remove disconnected peers from peers_without_progress. Keep connected peers
            // that already said they're at tip - they probably still are.
            // Now when all available peers are in peers_without_progress, we correctly detect
            // that header sync is complete and trigger block sync.
            if (!peers_without_progress.empty()) {
                size_t const before = peers_without_progress.size();
                // Build set of current peer nonces for O(1) lookup
                boost::unordered_flat_set<uint64_t> current_nonces;
                for (auto const& p : available_peers) {
                    current_nonces.insert(p->nonce());
                }
                // Remove peers that are no longer connected
                erase_if(peers_without_progress, [&](uint64_t nonce) {
                    return !current_nonces.contains(nonce);
                });
                if (peers_without_progress.size() != before) {
                    spdlog::debug("[header_download] Removed {} disconnected peers from peers_without_progress (now {})",
                        before - peers_without_progress.size(), peers_without_progress.size());
                }
            }

            // Connected is not what this needs; eligible is. Testing the wrong
            // one produced "Retrying pending request with 9 peers" immediately
            // followed by "No peers available", in the same millisecond, forever
            // (#705).
            if (pending_request && eligible_peers() > 0) {
                spdlog::debug("[header_download] Retrying pending request ({} connected, {} eligible)",
                    available_peers.size(), eligible_peers());
                co_await drive_request(*pending_request);
            }
            continue;
        }

        if (auto* request = std::get_if<header_request>(&event)) {
            // The coordinator's verdict on the answer this request follows: the
            // peer that gave it moved nothing, so it is spent for this walk just
            // as one that answered empty is. Only the coordinator knows this —
            // whether headers were new is the organizer's word, and this task
            // sees the wire, where "2000 headers, all of them already ours"
            // looks exactly like progress.
            if (request->spent_peer) {
                peers_without_progress.insert(*request->spent_peer);
                if (current_peer && current_peer->nonce() == *request->spent_peer) {
                    // Sticky selection would otherwise hand it straight back.
                    current_peer = nullptr;
                }
            } else {
                // No spent peer named: this request begins a walk rather than
                // continuing one, so nobody is spent yet. Spentness is a fact
                // about one walk — a peer with nothing new at this height may
                // have plenty at the next, and a peer with nothing a minute ago
                // may have the block that was just mined.
                //
                // This is also what makes a confirmed tip provisional: the
                // next request from the coordinator — or from anything that
                // learns the chain moved — finds every peer eligible again.
                peers_without_progress.clear();
                current_peer = nullptr;
            }

            spdlog::debug("[header_download] Request received: height={} ({} eligible)",
                request->from_height, eligible_peers());
            co_await drive_request(*request);
            continue;
        }
    }

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[header_download] Task ended");
}

// =============================================================================
// Header Validation Task
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    header_validation_input_channel& input,
    header_validated_channel& output
) {
    spdlog::debug("[header_validation] Task started");

    // Single channel, FIFO processing - no priority issues
    while (true) {
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_validation] Input channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[header_validation] Stop signal received");
            break;
        }

        if (auto* downloaded = std::get_if<downloaded_headers>(&msg)) {
            spdlog::debug("[header_validation] Validating {} headers from height {}",
                downloaded->headers.size(), downloaded->start_height);

            // Single writer to organizer - no lock needed
            auto result = organizer.add_headers(downloaded->headers);

            spdlog::debug("[header_validation] Added {} headers, total index size: {}",
                result.headers_added, result.index_size);

            if (!output.try_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = result.headers_added,
                .result = result.error,
                .source_peer = downloaded->source_peer,
                .reorg_candidate = result.reorg_candidate,
                .reorg_fork_height = result.reorg_fork_height,
                .reorg_branch_head = result.reorg_branch_head
            })) {
                spdlog::warn("[header_validation] Channel full, headers_validated dropped");
                break;
            }
        } else if (auto* failure = std::get_if<peer_failure_report>(&msg)) {
            // Peer failure - forward to coordinator so it can retry with another peer
            spdlog::debug("[header_validation] Received peer failure report for {}: {}",
                failure->peer->authority_with_agent(), failure->error.message());

            // Forward failure to coordinator - it will retry header sync
            if (!output.try_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = 0,
                .result = failure->error,
                .source_peer = failure->peer
            })) {
                spdlog::warn("[header_validation] Channel full, peer failure dropped");
            }
        }
    }

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[header_validation] Task ended");
}

} // namespace kth::node::sync
