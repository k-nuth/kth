// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <control.hpp>
#include <expected>
#include <optional>
#include <tuple>

namespace kth::blockchain {
template <class... Args>
struct channel {
    using tuple = std::tuple<std::error_code, Args...>;
    asio::experimental::concurrent_channel<void(std::error_code, Args...)> wire;
    int readers = 0;
    explicit channel(asio::io_context& io) : wire(io, 8) {}
    bool is_open() const {
        return wire.is_open();
    }
    void close() {
        wire.close();
    }
    void cancel() {
        wire.cancel();
    }
    template <class Token>
    asio::awaitable<tuple> async_receive(Token token) {
        struct reader {
            int& count;
            explicit reader(int& n) : count(n) {
                ++count;
            }
            ~reader() {
                --count;
            }
        } guard(readers);
        if (control::mode == control::fault::receive_allocation)
            throw std::bad_alloc{};
        if (control::mode == control::fault::receive_exception)
            throw std::runtime_error("receive failure");
        co_return co_await wire.async_receive(token);
    }
};
struct block_chain {
    asio::io_context context;
    using blocks = std::shared_ptr<std::vector<domain::message::block::const_ptr>>;
    using block_channel_ptr = std::shared_ptr<channel<size_t, blocks, blocks>>;
    using transaction_channel_ptr = std::shared_ptr<channel<domain::message::transaction::const_ptr>>;
    using ds_proof_channel_ptr = std::shared_ptr<channel<std::shared_ptr<domain::chain::double_spend_proof>>>;
    block_channel_ptr block_events;
    transaction_channel_ptr tx_events;
    ds_proof_channel_ptr proof_events;
    int active = 0, unsubscribed = 0;
    bool stopped = false, stale = false;
    auto executor() {
        if (control::mode == control::fault::startup_allocation)
            throw std::bad_alloc{};
        if (control::mode == control::fault::startup_exception)
            throw std::runtime_error("startup failure");
        if (control::mode == control::fault::schedule_allocation)
            control::fail_next_allocation = true;
        return context.get_executor();
    }
    template <class T>
    asio::awaitable<std::expected<T, std::error_code>> query(T value) {
        co_await asio::post(context, asio::use_awaitable);
        control::throw_query();
        if (control::mode == control::fault::domain)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        co_return std::move(value);
    }
    // Query parameters are references in the native API. Read them after the
    // suspension to require the co_spawn factory to retain its owned captures.
    template <class T, class Key>
    asio::awaitable<std::expected<T, std::error_code>> query_with_key(T value, Key const& key) {
        auto result = co_await query(std::move(value));
        if constexpr (std::is_same_v<Key, hash_digest>) {
            if (key[0] != 7)
                throw std::runtime_error("query lost its captured hash");
        }
        co_return std::move(result);
    }
    struct height {
        size_t block = 42;
    };
    auto fetch_last_height() {
        return query(height{});
    }
    auto fetch_mining_template(uint64_t) {
        mining_template value;
        value.selection.txs.push_back(std::make_shared<domain::message::transaction>());
        return query(std::move(value));
    }
    template <class Key>
    auto fetch_block_header(Key const& key) {
        return query_with_key(std::tuple{ std::make_shared<domain::chain::header>(), size_t{ 42 } }, key);
    }
    template <class Key>
    auto fetch_block(Key const& key) {
        return query_with_key(std::tuple{ std::make_shared<domain::message::block>(), size_t{ 42 } }, key);
    }
    template <class Key>
    auto fetch_merkle_block(Key const& key) {
        return query_with_key(std::tuple{ std::make_shared<domain::message::merkle_block>(), size_t{ 42 } }, key);
    }
    template <class Key>
    auto fetch_compact_block(Key const& key) {
        return query_with_key(std::tuple{ std::make_shared<domain::message::compact_block>(), size_t{ 42 } }, key);
    }
    auto fetch_block_header_txs_size(hash_digest const& key) {
        return query_with_key(std::tuple{ std::make_shared<domain::chain::header>(), size_t{ 42 }, hashes{}, size_t{ 80 } }, key);
    }
    auto fetch_block_hash_timestamp(size_t) {
        return query(std::tuple{ hash_digest{}, uint32_t{ 100 }, size_t{ 42 } });
    }
    auto fetch_transaction(hash_digest const& key, bool) {
        return query_with_key(std::tuple{ std::make_shared<domain::message::transaction>(), size_t{ 2 }, size_t{ 42 } }, key);
    }
    auto fetch_transaction_position(hash_digest const& key, bool) {
        return query_with_key(std::tuple{ size_t{ 2 }, size_t{ 42 } }, key);
    }
    struct submission {
        std::error_code error;
        // Match the native reply's user-provided constructor contract.
        submission(std::error_code ec = {}) : error(ec) {}
    };
    static asio::awaitable<submission> admitted(domain::message::block::const_ptr block, control::fault mode) {
        co_await asio::post(asio::use_awaitable);
        control::throw_query();
        if (block->value != 42)
            throw std::runtime_error("submission lost owned input");
        co_return submission{ mode == control::fault::domain ? std::make_error_code(std::errc::invalid_argument) : std::error_code{} };
    }
    auto submit_block(domain::message::block::const_ptr block) {
        return admitted(std::move(block), control::mode);
    }
    asio::awaitable<std::error_code> submit_transaction(domain::message::transaction::const_ptr tx) {
        co_await asio::post(context, asio::use_awaitable);
        control::throw_query();
        if (tx->value != 42)
            throw std::runtime_error("transaction lost copied input");
        co_return control::mode == control::fault::domain ? std::make_error_code(std::errc::invalid_argument) : std::error_code{};
    }
    auto transaction_validate(domain::message::transaction::const_ptr tx) {
        return submit_transaction(std::move(tx));
    }
    template <class Pointer>
    Pointer subscribe(Pointer& slot) {
        if (control::mode == control::fault::subscribe_allocation)
            throw std::bad_alloc{};
        if (stopped)
            return nullptr;
        slot = std::make_shared<typename Pointer::element_type>(context);
        ++active;
        return slot;
    }
    auto subscribe_blockchain() {
        return subscribe(block_events);
    }
    auto subscribe_transaction() {
        return subscribe(tx_events);
    }
    auto subscribe_ds_proof() {
        return subscribe(proof_events);
    }
    template <class Pointer>
    void unsubscribe(Pointer const& value) {
        if (! value)
            return;
        value->close();
        --active;
        ++unsubscribed;
    }
    void unsubscribe_blockchain(block_channel_ptr const& value) {
        unsubscribe(value);
    }
    void unsubscribe_transaction(transaction_channel_ptr const& value) {
        unsubscribe(value);
    }
    void unsubscribe_ds_proof(ds_proof_channel_ptr const& value) {
        unsubscribe(value);
    }
    bool is_stale() const {
        return stale;
    }
    void drain() {
        context.restart();
        context.run();
    }
    void poll() {
        context.restart();
        context.poll();
    }
    void cleanup() {
        if (block_events)
            block_events->close();
        if (tx_events)
            tx_events->close();
        if (proof_events)
            proof_events->close();
        drain();
        block_events.reset();
        tx_events.reset();
        proof_events.reset();
    }
    ~block_chain() {
        cleanup();
    }
};
}  // namespace kth::blockchain
