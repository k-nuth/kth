// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/reorg.hpp>

#include <algorithm>
#include <chrono>
#include <exception>

#include <asio/as_tuple.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth::node::sync {

bool persist_active_headers(
    blockchain::block_chain& chain,
    blockchain::header_index const& index,
    uint32_t from_height,
    uint32_t to_height) {

    if (from_height > to_height) {
        return true;
    }

    constexpr uint32_t batch_size = 1000;

    // The chain this run describes. A switch replaces what sits at these heights,
    // so a run that outlives one would write part of one branch and part of
    // another into the same table.
    auto const generation = chain.chain_generation();

    for (uint32_t height = from_height; height <= to_height; ) {
        auto const batch_end = std::min(height + batch_size - 1, to_height);

        domain::chain::header::list batch;
        batch.reserve(batch_end - height + 1);
        for (uint32_t h = height; h <= batch_end; ++h) {
            // By height through the active chain: the index numbers entries in
            // arrival order, so an entry's index stopped being its height the
            // moment the first side branch was stored.
            auto const idx = index.active_at(static_cast<int32_t>(h));
            if (idx == blockchain::header_index::null_index) {
                spdlog::warn("[header_persist] Height {} is not on the active chain; stopping", h);
                return false;
            }
            batch.push_back(index.get_header(idx));
        }

        if (chain.chain_generation() != generation) {
            spdlog::info("[header_persist] The chain moved while persisting {}-{}; stopping",
                from_height, to_height);
            return false;
        }

        if (auto const ec = chain.organize_headers_batch(batch, height); ec) {
            spdlog::warn("[header_persist] Failed at height {}: {}", height, ec.message());
            return false;
        }

        height = batch_end + 1;
    }

    return true;
}

namespace {

// Everything between closing entry and reopening it.
//
// Split out so the whole of it sits inside one try in the caller: from
// `begin_transition` on, an exception that unwinds past here leaves the gate
// shut with nobody told.
::asio::awaitable<reorg_outcome> switch_publish_and_reopen(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    blockchain::header_index::index_t branch_head,
    uint32_t fork_height,
    header_persister const& persist) {

    auto const outcome = co_await chain.switch_to_branch_async(branch_head, fork_height);

    // Set wherever the chain and its persisted description end up disagreeing.
    // Every such case is the same condition: the header table describes a branch
    // the node is no longer on, and nothing repairs it while it runs.
    bool fatal = false;

    if (outcome.ok) {
        organizer.adopt_tip(branch_head);

        // Make the by-height header table describe the new branch over the range
        // the switch replaced, and nothing above it — here rather than later,
        // because the writers are still parked, so this is the one moment nothing
        // else reads those heights or appends past them. The background persist
        // that runs at the end of a header sync covers new heights, not heights
        // whose block changed.
        //
        // The whole range goes in one call, and the database writes it in one
        // transaction: written halfway, the table would name the new branch over
        // part of the range and the abandoned one over the rest, and nothing
        // later would notice — every height would still hold a header that
        // parses. The range is bounded by the reorg depth, so it is one write.
        auto const& index = chain.headers();
        auto const new_tip_height = index.active_tip_height();

        // The branch head is above the fork — switch_to_branch refuses anything
        // else, and adopt_tip just published it — so anything else here means the
        // chain is not where the switch says it is. Say so and write nothing.
        if (new_tip_height <= int32_t(fork_height)) {
            spdlog::critical("[reorg] The active chain ends at {}, at or below the fork at {}, after "
                "a switch that reported success; the header table still describes the abandoned "
                "branch", new_tip_height, fork_height);
            fatal = true;
        } else {
            domain::chain::header::list branch;
            branch.reserve(static_cast<size_t>(new_tip_height - int32_t(fork_height)));
            for (int32_t h = int32_t(fork_height) + 1; h <= new_tip_height; ++h) {
                auto const idx = index.active_at(h);
                if (idx == blockchain::header_index::null_index) {
                    spdlog::critical("[reorg] Height {} is not on the active chain right after the "
                        "switch; the header table still describes the abandoned branch", h);
                    branch.clear();
                    fatal = true;
                    break;
                }
                branch.push_back(index.get_header(idx));
            }

            if ( ! branch.empty()) {
                if (auto const ec = persist(branch, fork_height + 1); ec) {
                    // The transaction aborted, so nothing was written: the
                    // persisted headers still describe the branch the node just
                    // left — whole, but wrong.
                    //
                    // That is not something to carry on from. The header index is
                    // rebuilt from this table at startup, so the node would run
                    // with its chain and its persisted view of it disagreeing, and
                    // a restart would come back up on the abandoned branch with
                    // the UTXO set already rewound below it. Nothing repairs the
                    // range on its own either: the background persist covers
                    // heights above where it started, not heights whose block
                    // changed underneath it.
                    //
                    // Report it as fatal and let the caller wind the node down.
                    // The next start loads the old branch, finds its blocks
                    // validated only to the fork, and re-syncs — the heavier
                    // branch arrives again and the switch is retried, from a state
                    // that is at least consistent with itself.
                    spdlog::critical("[reorg] Could not rewrite the header table over {}-{}: {}. "
                        "The persisted headers no longer describe the active chain",
                        fork_height + 1, new_tip_height, ec.message());
                    fatal = true;
                }
            }
        }

        // The switch rewound the validated tip to the fork. Tell the organizer
        // before the pause is lifted: deep-reorg parking measures depth against
        // that height, so a value left over from the abandoned branch would park
        // a fork sitting above where the chain now actually reaches — and the
        // next header batch can arrive as soon as the pause is gone.
        if (outcome.validated_tip) {
            organizer.note_block_validated(static_cast<int32_t>(*outcome.validated_tip));
        }
    }

    // Publish where the connected chain actually ended up, and only then reopen.
    //
    // The validated tip, not the branch head: the switch rewound the connected
    // chain to the fork, and the blocks above it are headers until the ordinary
    // path applies their deltas. Publishing the branch head would describe a
    // chain whose UTXO delta has not been applied — the height moving down here
    // while the generation moves up is the transition being honest (#605).
    //
    //
    // Both outcomes come through here. A switch that reports failure may still
    // have rewound part way — `validated_tip` is how far it got, not where it
    // started — so the published state has to describe that, and a node that
    // resyncs from there can serve work again. A switch that touched nothing
    // reports no tip, and there is nothing to republish.
    if ( ! fatal && ! outcome.mutated) {
        // Rejected before touching anything — a null head, a fork that is not
        // above the tip, an ancestry mismatch. Those report the current tip, so
        // publishing on them would move the generation and drop the template
        // cache for a switch that did nothing. Reopen and leave the view alone.
        spdlog::info("[reorg] The switch was rejected before touching the chain; "
            "the published state is unchanged");
        chain.end_transition();
        co_return reorg_outcome{outcome, fatal};
    }

    // Mutated, and no tip to describe it. The chain has been moved and nothing
    // says where to, so there is no height to publish and no state a capture
    // could be coherent with — reopening here would hand out templates built on
    // a view that describes the branch the node just left. Fatal, and the gate
    // stays shut.
    if ( ! fatal && ! outcome.validated_tip) {
        spdlog::critical("[reorg] The switch disconnected blocks and reported no tip. The chain "
            "has moved and nothing says where to; capture stays closed and the node winds down");
        fatal = true;
    }

    if ( ! fatal && outcome.validated_tip) {
        if (auto const ec = chain.publish_chain_view(*outcome.validated_tip); ec) {
            spdlog::critical("[reorg] Could not publish the chain state at height {}: {}. The "
                "node would keep validating against the branch it just left",
                *outcome.validated_tip, ec.message());
            fatal = true;
        }
    }

    if ( ! fatal) {
        // Earned. A failure above leaves capture closed while the node winds
        // down: there is no coherent state for a capture to be coherent with.
        chain.end_transition();
    }

    co_return reorg_outcome{outcome, fatal};
}

} // namespace

::asio::awaitable<reorg_outcome> execute_reorg(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    blockchain::header_index::index_t branch_head,
    uint32_t fork_height,
    std::function<bool()> abort,
    header_persister persist) {

    // Before anything is touched: without somewhere to write the replaced range,
    // a switch has no way to become survivable, and finding that out after the
    // chain has already moved is finding out too late.
    KTH_CONTRACT(persist);

    auto executor = co_await ::asio::this_coro::executor;

    // Quiesce the writers: the switch rewrites the UTXO set and the validated
    // height, so it must run alone. Lifting the pause is tied to the scope: a
    // pause left on stops every registered writer for the rest of the run, and
    // there are several ways out of here (an abort, a throw from the switch).
    struct pause_scope {
        blockchain::block_chain& chain;
        explicit pause_scope(blockchain::block_chain& c) : chain(c) { chain.request_reorg_pause(true); }
        ~pause_scope() { chain.request_reorg_pause(false); }
        pause_scope(pause_scope const&) = delete;
        pause_scope& operator=(pause_scope const&) = delete;
    } const pause(chain);

    // Bounded: a registered writer that never parks is a bug, and waiting on it
    // forever would hold the pause open and stall sync with nothing in the log.
    // Long enough that a legitimately slow batch — a UTXO build parks between
    // batches, and one batch runs full validation over a hundred blocks — is not
    // cut off.
    constexpr auto barrier_deadline = std::chrono::minutes(5);
    auto const wait_started = std::chrono::steady_clock::now();
    bool timed_out = false;

    while ( ! chain.reorg_barrier_reached() && ! abort()) {
        if (std::chrono::steady_clock::now() - wait_started > barrier_deadline) {
            timed_out = true;
            break;
        }
        ::asio::steady_timer wait_timer(executor);
        wait_timer.expires_after(std::chrono::milliseconds(50));
        co_await wait_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }

    if (timed_out) {
        auto const [parked, registered] = chain.reorg_barrier_state();
        spdlog::error("[reorg] Gave up waiting for the reorg barrier after {} minutes: {} of {} "
            "writers parked. The chain was not touched; the branch will be reconsidered when it is "
            "announced again", barrier_deadline.count(), parked, registered);
        co_return reorg_outcome{};
    }

    if (abort()) {
        spdlog::info("[reorg] Aborted while waiting for the barrier; the chain was not touched");
        co_return reorg_outcome{};
    }

    // Entry closes before the switch touches anything, and the captures already
    // admitted are drained first. A template that entered earlier finishes on
    // copies it holds; nothing new may capture a half-switched chain (#621).
    if ( ! chain.begin_transition()) {
        spdlog::critical("[reorg] A reorganization began while another transition was running");
        co_return reorg_outcome{{}, /*fatal*/ true};
    }

    // From here the gate is closed, and reopening it is the last thing a path
    // that ends well does. An exception unwinding past this point would leave it
    // closed with nobody told: `pause_scope` lifts the pause on the way out and
    // the sync loop carries on, so the node keeps working with every template
    // request refused for the rest of the process — a stop that never announces
    // itself. Convert it into the outcome the caller already knows how to act
    // on, and leave the gate shut on purpose: after a throw there is no coherent
    // state for a capture to be coherent with.
    try {
        co_return co_await switch_publish_and_reopen(
            chain, organizer, branch_head, fork_height, persist);
    } catch (std::exception const& e) {
        spdlog::critical("[reorg] The switch threw after the transition began: {}. The chain may "
            "be part way onto the branch; capture stays closed and the node winds down", e.what());
        co_return reorg_outcome{{}, /*fatal*/ true};
    } catch (...) {
        spdlog::critical("[reorg] The switch threw after the transition began. The chain may be "
            "part way onto the branch; capture stays closed and the node winds down");
        co_return reorg_outcome{{}, /*fatal*/ true};
    }
}

} // namespace kth::node::sync
