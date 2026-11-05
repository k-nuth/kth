// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_reader.hpp>

#include <list>
#include <exception>
#include <spdlog/spdlog.h>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::blockchain {

struct block_reader::state {
    using response = select_channel<result>;
    struct request {
        hash_digest hash;
        size_t height;
        database::flat_file_pos pos;
        std::shared_ptr<response> reply;
    };
    struct cached {
        hash_digest hash;
        block_const_ptr block;
        size_t bytes;
    };
    state(std::filesystem::path path, database::block_store::magic_t magic, limits budget)
        : requests(budget.requests, budget.requests == 0 ? 1 : budget.requests), store(std::move(path), magic), budget(budget) {}

    select_channel<request> requests;
    select_channel<int> stopping{1};
    database::block_store store; // read-only instance, owned exclusively by run()
    limits budget;
    std::list<cached> recent;
    boost::unordered_flat_map<hash_digest, std::list<cached>::iterator> cache;
    size_t bytes = 0;

    void finish() {
        stopping.close();
        requests.close();
        std::error_code ec;
        while (auto item = requests.try_receive(ec)) {
            item->reply->close();
        }
    }

    result read(request const& item) {
        if (auto hit = cache.find(item.hash); hit != cache.end()) {
            recent.splice(recent.begin(), recent, hit->second);
            return std::pair{hit->second->block, item.height};
        }
        auto raw = store.read_block_raw(item.pos);
        if (!raw) {
            return kth::fail(error::operation_failed);
        }
        byte_reader reader(*raw);
        auto parsed = domain::message::block::from_data(reader, 0u);
        if (!parsed || !reader.is_exhausted() || parsed->hash() != item.hash ||
            !parsed->is_valid_merkle_root()) {
            return kth::fail(error::operation_failed);
        }
        block_const_ptr block = std::make_shared<domain::message::block const>(std::move(*parsed));
        if (budget.cache_entries != 0 && raw->size() <= budget.cache_bytes) {
            while (!recent.empty() && (recent.size() >= budget.cache_entries ||
                    raw->size() > budget.cache_bytes - bytes)) {
                bytes -= recent.back().bytes;
                cache.erase(recent.back().hash);
                recent.pop_back();
            }
            recent.push_front({item.hash, block, raw->size()});
            cache.emplace(item.hash, recent.begin());
            bytes += raw->size();
        }
        return std::pair{std::move(block), item.height};
    }
};

block_reader::block_reader(std::filesystem::path path, database::block_store::magic_t magic, limits budget)
    : state_(std::make_shared<state>(std::move(path), magic, budget)) {}

void block_reader::start() {
    if (started_) {
        return;
    }
    started_ = true;
    // An owned root task. Completion closes all endpoints, including on an
    // exceptional exit. Destruction closes admission and joins its executor.
    ::asio::co_spawn(worker_, run(state_), [owned = state_](std::exception_ptr failure) {
        if (failure) {
            try { std::rethrow_exception(failure); }
            catch (std::exception const& error) { spdlog::error("[block_reader] owner failed: {}", error.what()); }
        }
        owned->finish();
    });
}

block_reader::~block_reader() {
    stop();
    worker_.join(); // do not stop the executor and abandon admitted requests
}

void block_reader::stop() {
    state_->stopping.close();
    state_->requests.close();
}

::asio::awaitable<void> block_reader::run(std::shared_ptr<state> owned) {
    auto executor = co_await ::asio::this_coro::executor;
    for (;;) {
        std::error_code ec;
        (void)owned->stopping.try_receive(ec);
        if (ec == ::asio::error::eof) {
            break;
        }
        auto [receive_error, selected] = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(owned->stopping),
            ::asio::experimental::receive(owned->requests)
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        if (receive_error || selected.index == 0) {
            break;
        }
        auto item = std::move(*selected.value<1>());
        struct reply_scope {
            std::shared_ptr<state::response> reply;
            ~reply_scope() { reply->close(); }
        } scope{item.reply};
        (void)owned->stopping.try_receive(ec);
        if (ec == ::asio::error::eof) {
            break;
        }
        // An empty reply channel is reserved until publication; closure means
        // the requester has cancelled, so queued work can be skipped.
        (void)item.reply->try_receive(ec);
        if (ec == ::asio::error::eof) {
            continue;
        }
        try {
            auto answer = owned->read(item);
            item.reply->try_send(std::move(answer));
        } catch (std::exception const& failure) {
            spdlog::error("[block_reader] read failed: {}", failure.what());
            owned->cache.clear(); owned->recent.clear(); owned->bytes = 0;
            result answer = kth::fail(error::operation_failed);
            item.reply->try_send(std::move(answer));
        }
    }
    owned->finish();
}

::asio::awaitable<block_reader::result> block_reader::fetch_block(
    hash_digest hash, size_t height, database::flat_file_pos pos) const {
    return fetch_owned(state_, hash, height, pos);
}

::asio::awaitable<block_reader::result> block_reader::fetch_owned(std::shared_ptr<state> owned,
    hash_digest hash, size_t height, database::flat_file_pos pos) {
    co_await ::asio::this_coro::throw_if_cancelled(false);
    auto executor = co_await ::asio::this_coro::executor;
    auto cancellation = co_await ::asio::this_coro::cancellation_state;
    if (cancellation.cancelled() != ::asio::cancellation_type::none) {
        co_return kth::fail(error::channel_stopped);
    }
    auto reply = std::make_shared<state::response>(1);
    struct reply_scope {
        std::shared_ptr<state::response> reply;
        ~reply_scope() { reply->close(); }
    } scope{reply};
    state::request request{hash, height, pos, reply};
    auto [send_error, sent] = co_await ::asio::experimental::make_select(
        ::asio::experimental::send(owned->requests, std::move(request)),
        ::asio::experimental::receive(owned->stopping)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    if (send_error == ::asio::error::no_buffer_space) {
        co_return kth::fail(error::oversubscribed);
    }
    if (send_error || sent.index != 0) {
        co_return kth::fail(cancellation.cancelled() != ::asio::cancellation_type::none ?
            error::channel_stopped : error::service_stopped);
    }
    auto [receive_error, received] = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(*reply),
        ::asio::experimental::receive(owned->stopping)
    ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    if (receive_error || received.index != 0) {
        co_return kth::fail(cancellation.cancelled() != ::asio::cancellation_type::none ?
            error::channel_stopped : error::service_stopped);
    }
    co_return std::move(*received.value<0>());
}

} // namespace kth::blockchain
