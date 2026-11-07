// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/compact_blocks.hpp>

#include <exception>
#include <unordered_map>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/constants/functions.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/get_block_transactions.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/node/detail/header_ingest.hpp>
#include <kth/node/p2p_node.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::sync {

namespace {

using short_id_slots = std::unordered_map<uint64_t, size_t>;

// Peers choose the short IDs, and std::hash leaves an integer as it is, so a
// peer can put them all in one bucket and make every lookup walk it. A bucket
// this crowded is taken for a collision, and the block is downloaded instead.
constexpr size_t max_short_id_bucket = 12;

// Whether `header` commits to `transactions`. They are lent to a block for the
// check and handed back, not copied.
bool commits(domain::chain::header const& header, domain::chain::transaction::list& transactions) {
    domain::chain::block block{header, std::move(transactions)};
    bool const valid = block.is_valid_merkle_root();
    transactions = std::move(block.transactions());
    return valid;
}

// The most transactions the block after the connected tip can hold: each
// takes at least the smallest transaction's bytes.
size_t max_transactions(domain::chain::chain_state const& state) {
#if defined(KTH_CURRENCY_BCH)
    return size_t(state.dynamic_max_block_size() / min_transaction_size_descartes);
#else
    (void)state;
    return max_compact_transactions();
#endif
}

template <typename Fill>
compact_reconstruction reconstruct_with(domain::message::compact_block const& message, Fill fill) {
    auto const& short_ids = message.short_ids();
    auto const& prefilled = message.transactions();
    if (prefilled.size() > max_compact_transactions() ||
            short_ids.size() > max_compact_transactions() - prefilled.size()) {
        return {};
    }
    auto const total = short_ids.size() + prefilled.size();
    if (total == 0) {
        return {};
    }

    // Prefilled transactions at their positions, which strictly increase.
    domain::chain::transaction::list slots(total);
    uint64_t previous = 0;
    bool started = false;
    for (auto const& item : prefilled) {
        if (item.index() >= total || (started && item.index() <= previous) ||
                item.transaction().is_null()) {
            return {};
        }
        slots[size_t(item.index())] = item.transaction();
        previous = item.index();
        started = true;
    }

    // Each short ID, in order, names the next free position.
    short_id_slots positions;
    positions.reserve(short_ids.size());
    size_t next_free = 0;
    for (auto const id : short_ids) {
        while ( ! slots[next_free].is_null()) {
            ++next_free;
        }
        // A repeated short ID cannot tell its two positions apart, even when
        // the mempool holds only one match.
        if ( ! positions.emplace(id, next_free++).second ||
                positions.bucket_size(positions.bucket(id)) > max_short_id_bucket) {
            return {compact_reconstruct::collision, {}, {}};
        }
    }
    if (fill(positions, slots)) {
        return {compact_reconstruct::collision, {}, {}};
    }

    std::vector<uint64_t> missing;
    for (size_t at = 0; at < slots.size(); ++at) {
        if (slots[at].is_null()) {
            missing.push_back(at);
        }
    }
    if ( ! missing.empty()) {
        return {compact_reconstruct::missing, std::move(slots), std::move(missing)};
    }
    if ( ! commits(message.header(), slots)) {
        return {};
    }
    return {compact_reconstruct::complete, std::move(slots), {}};
}

} // namespace

compact_reconstruction reconstruct_compact_block(domain::message::compact_block const& message,
    domain::chain::transaction::list const& available) {

    auto const key = domain::message::hash(message);
    return reconstruct_with(message, [&](short_id_slots const& positions, domain::chain::transaction::list& slots) {
        std::vector<bool> filled(slots.size());
        for (auto const& tx : available) {
            auto const found = positions.find(domain::message::compact_short_id(key, tx.hash()));
            if (found == positions.end()) {
                continue;
            }
            if (filled[found->second]) {
                return true;
            }
            filled[found->second] = true;
            slots[found->second] = tx;
        }
        return false;
    });
}

compact_reconstruction reconstruct_compact_block(domain::message::compact_block const& message,
    blockchain::block_chain const& chain) {

    return reconstruct_with(message, [&](short_id_slots const& positions, domain::chain::transaction::list& slots) {
        size_t mempool_count = 0;
        return chain.fill_tx_list_from_mempool(message, mempool_count, slots, positions);
    });
}

compact_reconstruction accept_block_transactions(domain::message::compact_block const& message,
    compact_reconstruction const& partial, domain::chain::transaction::list const& provided) {

    auto const& prefilled = message.transactions();
    auto const& short_ids = message.short_ids();
    if (partial.status != compact_reconstruct::missing ||
            provided.size() != partial.missing.size() ||
            partial.transactions.empty() ||
            partial.transactions.size() != prefilled.size() + short_ids.size()) {
        return {};
    }

    // Walk the block: each position that is not prefilled is the next short
    // ID's, and a missing one takes the next provided transaction, which must
    // have that short ID.
    auto slots = partial.transactions;
    auto const key = domain::message::hash(message);
    size_t next_short_id = 0;
    size_t next_provided = 0;
    size_t next_prefilled = 0;
    for (size_t at = 0; at < slots.size(); ++at) {
        if (next_prefilled < prefilled.size() && prefilled[next_prefilled].index() == at) {
            ++next_prefilled;
            continue;
        }
        if (next_short_id >= short_ids.size()) {
            return {};
        }
        if (next_provided < partial.missing.size() && partial.missing[next_provided] == at) {
            auto const& tx = provided[next_provided++];
            if (domain::message::compact_short_id(key, tx.hash()) != short_ids[next_short_id]) {
                return {};
            }
            slots[at] = tx;
        }
        ++next_short_id;
    }
    if (next_provided != provided.size() || ! commits(message.header(), slots)) {
        return {};
    }
    return {compact_reconstruct::complete, std::move(slots), {}};
}

// =============================================================================
// compact_block_receiver
// =============================================================================

compact_block_receiver::compact_block_receiver(blockchain::block_chain& chain, p2p_node& network,
    std::chrono::milliseconds blocktxn_timeout)
    : chain_(chain)
    , network_(network)
    , blocktxn_timeout_(blocktxn_timeout)
    , tasks_("compact_blocks", network.thread_pool().get_executor())
{}

bool compact_block_receiver::offer(network::peer_session::ptr const& peer,
    domain::message::compact_block message) {

    if ( ! peer || network_.stopped()) {
        return false;
    }
    // Only the block after the connected tip, and only when the node holds no
    // header past it but, at most, that block's own: otherwise this is initial
    // sync, or a branch, and the header ingest leads.
    auto const view = chain_.chain_view();
    if ( ! view || ! view->state) {
        return false;
    }
    auto const header_tip = chain_.headers().active_tip_height();
    if (header_tip < 0 || size_t(header_tip) > view->connected_tip_height + 1 ||
            ! chain_.synchronization(*view, header_tip).fresh ||
            message.header().previous_block_hash() != view->tip_hash) {
        return false;
    }
    // BIP152 has a peer validate a block's header, its proof of work
    // included, before it sends the block's cmpctblock. One that fails is not
    // worth a pass over the mempool; refused here, the header goes to the
    // header ingest, which answers it as it answers a header sent alone.
    auto const hash = domain::chain::hash(message.header());
    auto const retarget = view->state->network() != domain::config::network::regtest;
    if (message.header().check(hash, retarget) || message.header().accept(*view->state, hash)) {
        return false;
    }
    // More transactions than that block can hold describe no block the chain
    // would take, and are not worth a pass over the mempool.
    if (message.short_ids().size() + message.transactions().size() > max_transactions(*view->state)) {
        return false;
    }

    std::lock_guard const lock(mutex_);
    if (stopping_) {
        return false;
    }
    // Another peer's cmpctblock for a block already being rebuilt is taken
    // and left: downloading it beside the reconstruction would fetch it twice.
    for (auto const& entry : pending_) {
        if (entry.second.hash == hash) {
            return true;
        }
    }
    if (pending_.contains(peer.get()) || pending_.size() >= max_reconstructions) {
        return false;
    }
    pending_.emplace(peer.get(), reconstruction{hash, nullptr});
    // Spawned under the lock, so run() cannot stop waiting before this task
    // is counted. The task leaves the read loop before it does anything.
    tasks_.spawn("compact_block", reconstruct(peer, std::move(message)));
    spdlog::debug("[compact_blocks] Rebuilding block {} from [{}]", encode_hash(hash), peer->authority());
    return true;
}

bool compact_block_receiver::deliver(network::peer_session::ptr const& peer,
    domain::message::block_transactions message) {

    std::shared_ptr<reply_channel> reply;
    {
        std::lock_guard const lock(mutex_);
        auto const found = pending_.find(peer.get());
        if (found == pending_.end()) {
            return false;
        }
        reply = found->second.reply;
    }
    // One slot: the answer to the request. Whatever follows it is not. Sent
    // outside the lock, which the reconstruction it wakes takes at its end.
    return reply && reply->try_send(std::move(message));
}

size_t compact_block_receiver::pending() const {
    std::lock_guard const lock(mutex_);
    return pending_.size();
}

::asio::awaitable<void> compact_block_receiver::run() {
    auto executor = co_await ::asio::this_coro::executor;
    // The network's stop, as an event: a selection with nothing but a
    // never-due `after` case ends with operation_aborted when it stops.
    if ( ! network_.stopped()) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::after(::asio::steady_timer::time_point::max())
        ).async_wait(executor, {}, network_.stop_token(), ::asio::as_tuple(::asio::deferred));
        (void)completion;
    }
    {
        std::lock_guard const lock(mutex_);
        stopping_ = true;
    }
    // Their waits for blocktxn end with the same stop; a submission still
    // owed is answered by the coordinator as it ends.
    co_await tasks_.join();
}

::asio::awaitable<void> compact_block_receiver::reconstruct(network::peer_session::ptr peer,
    domain::message::compact_block message) {

    // Off the read loop that offered it: co_spawn runs a task inline up to
    // its first wait when called from a thread of the task's executor, and the
    // read loops run on the same pool.
    auto executor = co_await ::asio::this_coro::executor;
    co_await ::asio::post(executor, ::asio::use_awaitable);

    bool taken = false;
    try {
        taken = co_await rebuild(peer, message);
    } catch (std::exception const& e) {
        spdlog::debug("[compact_blocks] Rebuilding a block from [{}] failed: {}", peer->authority(), e.what());
    }
    if ( ! taken && ! network_.stopped()) {
        fall_back(peer, message.header());
    }
    std::lock_guard const lock(mutex_);
    pending_.erase(peer.get());
}

::asio::awaitable<bool> compact_block_receiver::rebuild(network::peer_session::ptr const& peer,
    domain::message::compact_block const& message) {

    auto const hash = domain::chain::hash(message.header());
    auto rebuilt = reconstruct_compact_block(message, chain_);
    if (rebuilt.status == compact_reconstruct::missing) {
        auto const request = domain::message::get_block_transactions::create(hash, rebuilt.missing);
        auto const reply = std::make_shared<reply_channel>(1);
        {
            std::lock_guard const lock(mutex_);
            auto const found = pending_.find(peer.get());
            if (found == pending_.end()) {
                co_return false;
            }
            found->second.reply = reply;
        }
        // Never waits for room: a peer that is not reading holds no
        // reconstruction while its queue drains.
        if ( ! request || ! peer->try_send(*request)) {
            spdlog::debug("[compact_blocks] getblocktxn for {} not sent to [{}]",
                encode_hash(hash), peer->authority());
            co_return false;
        }
        spdlog::debug("[compact_blocks] Asked [{}] for {} transaction(s) of {}",
            peer->authority(), rebuilt.missing.size(), encode_hash(hash));

        // The answer, the deadline or the network's stop: one selection, and
        // the read loop that delivers the answer never waits on it.
        auto executor = co_await ::asio::this_coro::executor;
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(*reply),
            ::asio::experimental::after(std::chrono::steady_clock::now() + blocktxn_timeout_)
        ).async_wait(executor, {}, network_.stop_token(), ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            co_return false;
        }
        if (selected.index == 1) {
            spdlog::debug("[compact_blocks] No blocktxn for {} from [{}] in time",
                encode_hash(hash), peer->authority());
            co_return false;
        }
        auto const response = std::move(*selected.value<0>());
        if (response.block_hash() != hash) {
            spdlog::debug("[compact_blocks] blocktxn from [{}] is for {}, not {}",
                peer->authority(), encode_hash(response.block_hash()), encode_hash(hash));
            co_return false;
        }
        rebuilt = accept_block_transactions(message, rebuilt, response.transactions());
    }
    if (rebuilt.status != compact_reconstruct::complete) {
        spdlog::debug("[compact_blocks] Block {} from [{}] could not be rebuilt",
            encode_hash(hash), peer->authority());
        co_return false;
    }

    // What a block found locally goes through: success is connection.
    auto const block = std::make_shared<domain::message::block const>(
        message.header(), std::move(rebuilt.transactions));
    auto const result = co_await chain_.submit_block(block);
    if (result.error && result.error != error::duplicate_block) {
        spdlog::debug("[compact_blocks] Block {} from [{}] was not connected: {}",
            encode_hash(hash), peer->authority(), result.error.message());
        co_return false;
    }
    spdlog::debug("[compact_blocks] Block {} from [{}] rebuilt and taken", encode_hash(hash), peer->authority());
    co_return true;
}

void compact_block_receiver::fall_back(network::peer_session::ptr const& peer,
    domain::chain::header const& header) {

    // As if the peer had sent the header alone: the block is downloaded the
    // ordinary way. A refusal rings the ingest's own debt, as for any header.
    domain::message::header::list headers;
    headers.emplace_back(header);
    if ( ! network_.header_ingest().submit(peer, std::move(headers))) {
        spdlog::debug("[compact_blocks] Header from [{}] not queued; a follow-up is owed", peer->authority());
    }
}

} // namespace kth::node::sync
