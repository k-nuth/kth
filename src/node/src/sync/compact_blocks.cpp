// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/scope_action.hpp>
#include <kth/node/sync/compact_blocks.hpp>

#include <optional>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/channel_select.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/pools/mempool_hashers.hpp>
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

using blockchain::short_id_slots;

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
        if ( ! positions.emplace(id, next_free++).second) {
            return {compact_reconstruct::collision, {}, {}};
        }
    }
    auto filled = fill(positions, std::move(slots));
    if ( ! filled) {
        return {compact_reconstruct::collision, {}, {}};
    }
    slots = std::move(*filled);

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
    return reconstruct_with(message, [&](short_id_slots const& positions, domain::chain::transaction::list slots)
            -> std::optional<domain::chain::transaction::list> {
        std::vector<bool> filled(slots.size());
        for (auto const& tx : available) {
            auto const found = positions.find(domain::message::compact_short_id(key, tx.hash()));
            if (found == positions.end()) {
                continue;
            }
            if (filled[found->second]) {
                return std::nullopt;
            }
            filled[found->second] = true;
            slots[found->second] = tx;
        }
        return slots;
    });
}

compact_reconstruction reconstruct_compact_block(domain::message::compact_block const& message,
    blockchain::block_chain const& chain) {

    return reconstruct_with(message, [&](short_id_slots const& positions, domain::chain::transaction::list slots)
            -> std::optional<domain::chain::transaction::list> {
        auto filled = chain.fill_tx_list_from_mempool(message, std::move(slots), positions);
        if (filled.collided) {
            return std::nullopt;
        }
        return std::move(filled.transactions);
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
    , offers_(2 * max_reconstructions)
    , answers_(max_reconstructions)
    , finished_(max_reconstructions)
    , questions_(max_reconstructions)
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
    // Refused when run() has no room for it, or has ended and closed this.
    return offers_.try_send(offered{peer, std::move(message)});
}

bool compact_block_receiver::deliver(network::peer_session::ptr const& peer,
    domain::message::block_transactions message) {

    if ( ! peer) {
        return false;
    }
    return answers_.try_send(answered{peer, std::move(message)});
}

::asio::awaitable<size_t> compact_block_receiver::pending() {
    auto executor = co_await ::asio::this_coro::executor;
    auto const answer = std::make_shared<count_channel>(1);
    // Waits for room, and for the answer: run() answers every question it
    // takes, and closes this when it ends.
    auto asked = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(questions_, answer)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    if (std::get<0>(asked)) {
        co_return 0;
    }
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(*answer)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) {
        co_return 0;
    }
    co_return *selected.value<0>();
}

void compact_block_receiver::take(reconstructions& pending, task_group& tasks, offered offer) {
    auto const hash = domain::chain::hash(offer.message.header());
    // Another peer's cmpctblock for a block already being rebuilt is taken
    // and left: downloading it beside the reconstruction would fetch it twice.
    for (auto const& entry : pending) {
        if (entry.second.hash == hash) {
            return;
        }
    }
    // One per peer, three in all: past that the block is downloaded.
    if (pending.contains(offer.peer.get()) || pending.size() >= max_reconstructions) {
        fall_back(offer.peer, offer.message.header());
        return;
    }
    auto reply = std::make_shared<reply_channel>(1);
    pending.emplace(offer.peer.get(), reconstruction{hash, reply});
    spdlog::debug("[compact_blocks] Rebuilding block {} from [{}]", encode_hash(hash), offer.peer->authority());
    tasks.spawn("compact_block", reconstruct(std::move(offer.peer), std::move(offer.message), std::move(reply)));
}

::asio::awaitable<void> compact_block_receiver::run() {
    auto executor = co_await ::asio::this_coro::executor;
    // Owned here, and by nothing else.
    reconstructions pending;
    task_group tasks("compact_blocks", network_.thread_pool().get_executor());

    while ( ! network_.stopped()) {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(finished_),
            ::asio::experimental::receive(answers_),
            ::asio::experimental::receive(offers_),
            ::asio::experimental::receive(questions_)
        ).async_wait(executor, {}, network_.stop_token(), ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) {
            // The network stopped.
            break;
        }
        if (selected.index == 0) {
            pending.erase(*selected.value<0>());
            continue;
        }
        if (selected.index == 1) {
            auto answer = std::move(*selected.value<1>());
            auto const found = pending.find(answer.peer.get());
            // One slot: the answer to the request, which names its block. One
            // for another block, a late answer to a request given up on among
            // them, takes no slot, and whatever follows the answer is not it.
            if (found == pending.end() || answer.message.block_hash() != found->second.hash ||
                    ! found->second.reply->try_send(std::move(answer.message))) {
                spdlog::debug("[compact_blocks] blocktxn from [{}] answers no request; ignored",
                    answer.peer->authority());
            }
            continue;
        }
        if (selected.index == 2) {
            take(pending, tasks, std::move(*selected.value<2>()));
            continue;
        }
        (void)(*selected.value<3>())->try_send(pending.size());
    }

    // Nothing more is taken: the read loops are refused from here on, and
    // whoever is asking is answered. A cmpctblock still queued is not
    // rebuilt; the node is stopping.
    offers_.close();
    answers_.close();
    questions_.close();
    // A question sent before the close and not yet taken is answered here:
    // whoever sent it is waiting for the answer, and nothing else would give
    // it one.
    std::error_code drained;
    while (auto const left = questions_.try_receive(drained)) {
        (void)(*left)->try_send(size_t{0});
    }
    // What was queued and not taken holds its peer's session: let go of here,
    // not when the receiver is destroyed.
    while (offers_.try_receive(drained)) {}
    while (answers_.try_receive(drained)) {}
    // The reconstructions' waits end with the same stop; a submission still
    // owed is answered by the coordinator as it ends.
    co_await tasks.join();
    finished_.close();
}

::asio::awaitable<void> compact_block_receiver::reconstruct(network::peer_session::ptr peer,
    domain::message::compact_block message, std::shared_ptr<reply_channel> reply) {

    // run() is told when this ends, however it ends. There is room: the
    // channel holds one for each reconstruction there can be.
    detail::scope_action const finished([this, key = peer.get()]() noexcept {
        network::peer_session const* who = key;
        (void)finished_.try_send(std::move(who));
    }, true);

    // Off the task that spawned it: co_spawn runs a task inline up to its
    // first wait when called from a thread of the task's executor.
    auto executor = co_await ::asio::this_coro::executor;
    co_await ::asio::post(executor, ::asio::use_awaitable);

    auto const taken = co_await rebuild(peer, message, *reply);
    if ( ! taken && ! network_.stopped()) {
        fall_back(peer, message.header());
    }
}

::asio::awaitable<bool> compact_block_receiver::rebuild(network::peer_session::ptr const& peer,
    domain::message::compact_block const& message, reply_channel& reply) {

    auto const hash = domain::chain::hash(message.header());
    auto rebuilt = reconstruct_compact_block(message, chain_);
    if (rebuilt.status == compact_reconstruct::missing) {
        auto const request = domain::message::get_block_transactions::create(hash, rebuilt.missing);
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
            ::asio::experimental::receive(reply),
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
        // For this block: run() hands over no other.
        auto const response = std::move(*selected.value<0>());
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
