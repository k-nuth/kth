// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/compact_block_relay.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/domain/constants/functions.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/block_transactions.hpp>
#include <kth/domain/message/get_block_transactions.hpp>
#include <kth/node/compact_blocks.hpp>
#include <kth/node/detail/header_ingest.hpp>

namespace kth::node {

struct compact_block_relay::state {
    struct pending_block {
        hash_digest hash;
        bool waiting{false};
        std::optional<domain::message::block_transactions> response;
    };

    state(blockchain::block_chain& chain_in, p2p_node& network_in,
        std::chrono::milliseconds timeout)
        : chain(chain_in)
        , network(network_in)
        , blocktxn_timeout(timeout) {}

    blockchain::block_chain& chain;
    p2p_node& network;
    std::chrono::milliseconds const blocktxn_timeout;
    std::mutex mutex;
    std::atomic<bool> stopping{false};
    // One request per peer, at most three in total. A second peer announcing
    // the same block must not start a full download beside its reconstruction.
    std::map<network::peer_session const*, pending_block> pending;

    void download(network::peer_session::ptr const& peer, domain::message::compact_block const& message) {
        if (stopping.load() || network.stopped()) {
            return;
        }
        domain::message::header::list headers;
        headers.emplace_back(message.header());
        (void)network.header_ingest().submit(peer, std::move(headers));
    }

    ::asio::awaitable<std::optional<domain::message::block_transactions>> receive(
        network::peer_session::ptr const& peer) {
        auto const deadline = std::chrono::steady_clock::now() + blocktxn_timeout;
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor);
        while ( ! stopping.load() && ! network.stopped() && ! peer->stopped()) {
            {
                std::lock_guard lock(mutex);
                auto& response = pending.at(peer.get()).response;
                if (response) {
                    co_return std::move(response);
                }
            }
            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            // The timer stays on this coroutine's executor. Cross-thread
            // offers only write the response under the mutex.
            timer.expires_at(std::min(deadline, now + std::chrono::milliseconds(50)));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                break;
            }
        }
        co_return std::nullopt;
    }

    ::asio::awaitable<void> reconstruct(network::peer_session::ptr peer,
        domain::message::compact_block message) {
        auto rebuilt = reconstruct_compact_block(message, chain);
        if (rebuilt.status == compact_reconstruct::missing) {
            auto const request = domain::message::get_block_transactions::create(
                domain::chain::hash(message.header()), rebuilt.missing);
            // Missing indexes came from the bounded reconstruction, so create
            // succeeds. Keep the wait off both the read loop and coordinator.
            {
                std::lock_guard lock(mutex);
                pending.at(peer.get()).waiting = true;
            }
            if ( ! request || ! peer->try_send(*request)) {
                download(peer, message);
                co_return;
            }
            auto const response = co_await receive(peer);
            if ( ! response || response->block_hash() != domain::chain::hash(message.header())) {
                download(peer, message);
                co_return;
            }
            rebuilt = accept_block_transactions(message, rebuilt, response->transactions());
        }
        if (rebuilt.status != compact_reconstruct::complete) {
            download(peer, message);
            co_return;
        }
        if (stopping.load() || network.stopped()) {
            co_return;
        }
        auto const block = std::make_shared<domain::message::block>(
            domain::chain::header{message.header()}, std::move(rebuilt.transactions));
        auto const result = co_await chain.organize(block);
        if (result != error::success && result != error::duplicate_block) {
            download(peer, message);
        }
    }

    static ::asio::awaitable<void> run(std::shared_ptr<state> held,
        network::peer_session::ptr peer, domain::message::compact_block message) {
        try {
            co_await held->reconstruct(peer, message);
        } catch (std::exception const&) {
            held->download(peer, message);
        }
        std::lock_guard lock(held->mutex);
        held->pending.erase(peer.get());
    }

    bool offer(std::shared_ptr<state> const& held, network::peer_session::ptr const& peer,
        domain::message::compact_block const& message) {
        if ( ! peer || stopping.load() || network.stopped()) {
            return false;
        }
        auto const view = chain.chain_view();
        auto const header_tip = chain.headers().active_tip_height();
        // organize accepts only a child of the connected tip. Also permit its
        // header to have arrived first; during IBD the normal download stays.
        if ( ! view || ! view->state || ! chain.synchronization().fresh
            || header_tip < 0 || size_t(header_tip) > view->connected_tip_height + 1
            || message.header().previous_block_hash() != view->tip_hash) {
            return false;
        }
        auto const count = message.transactions().size() + message.short_ids().size();
        auto const minimum = static_absolute_max_block_size() / max_compact_transactions();
        auto const limit = view->state->dynamic_max_block_size() / minimum;
        if (count > limit) {
            return false;
        }
        auto const hash = domain::chain::hash(message.header());
        {
            std::lock_guard lock(mutex);
            for (auto const& entry : pending) {
                if (entry.second.hash == hash) {
                    return true;
                }
            }
            if (pending.contains(peer.get()) || pending.size() >= 3 || stopping.load()) {
                return false;
            }
            pending.emplace(peer.get(), pending_block{hash});
        }
        ::asio::co_spawn(network.thread_pool().get_executor(), run(held, peer, message), ::asio::detached);
        return true;
    }

    bool offer(network::peer_session::ptr const& peer, domain::message::block_transactions const& message) {
        std::lock_guard lock(mutex);
        auto const found = pending.find(peer.get());
        if (found == pending.end() || ! found->second.waiting || found->second.response) {
            return false;
        }
        // One request per peer makes even a response naming the wrong block
        // unambiguous: it fails reconstruction immediately, without a ban.
        found->second.response = message;
        return true;
    }
};

compact_block_relay::compact_block_relay(blockchain::block_chain& chain, p2p_node& network,
    std::chrono::milliseconds blocktxn_timeout)
    : state_(std::make_shared<state>(chain, network, blocktxn_timeout))
    , network_(network) {}

compact_block_relay::~compact_block_relay() {
    state_->stopping.store(true);
}

void compact_block_relay::attach() {
    auto const held = state_;
    network_.set_compact_block_offer(
        [held](network::peer_session::ptr const& peer, domain::message::compact_block const& message) {
            return held->offer(held, peer, message);
        });
    network_.set_block_transactions_offer(
        [held](network::peer_session::ptr const& peer, domain::message::block_transactions const& message) {
            return held->offer(peer, message);
        });
}

} // namespace kth::node
