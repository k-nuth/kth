// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_BROADCASTER_HPP
#define KTH_INFRASTRUCTURE_BROADCASTER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace kth {

/// Thread-safe broadcaster for pub/sub pattern using Asio channels.
/// Replaces the deprecated resubscriber class.
///
/// Usage:
///   broadcaster<code, size_t, block_ptr> bc(executor);
///
///   // Subscribe (returns a channel to receive from)
///   auto channel = bc.subscribe();
///
///   // In a coroutine, receive events:
///   while (auto event = co_await channel->async_receive(asio::use_awaitable)) {
///       auto [ec, height, block] = *event;
///       // process...
///   }
///
///   // Publish to all subscribers:
///   bc.publish(error::success, height, block);
///
template <typename... Args>
class broadcaster {
public:
    using executor_type = ::asio::any_io_executor;
    using channel_type = ::asio::experimental::concurrent_channel<void(std::error_code, Args...)>;
    using channel_ptr = std::shared_ptr<channel_type>;

    static constexpr size_t default_buffer_size = 64;

    explicit broadcaster(executor_type executor, size_t buffer_size = default_buffer_size)
        : executor_(std::move(executor))
        , buffer_size_(buffer_size)
        , stopped_(false)
    {}

    ~broadcaster() {
        close();
    }

    // Non-copyable, non-movable
    broadcaster(broadcaster const&) = delete;
    broadcaster& operator=(broadcaster const&) = delete;
    broadcaster(broadcaster&&) = delete;
    broadcaster& operator=(broadcaster&&) = delete;

    /// Start accepting subscriptions.
    void start() {
        stopped_ = false;
    }

    /// Stop and close all channels.
    void stop() {
        close();
    }

    /// Subscribe and get a channel to receive events.
    /// Returns nullptr if the broadcaster is stopped.
    [[nodiscard]]
    channel_ptr subscribe() {
        if (stopped_) {
            return nullptr;
        }

        auto channel = std::make_shared<channel_type>(executor_, buffer_size_);

        {
            std::lock_guard lock(mutex_);
            subscribers_.push_back(channel);
        }

        return channel;
    }

    /// Unsubscribe a specific channel.
    void unsubscribe(channel_ptr const& channel) {
        if ( ! channel) {
            return;
        }

        std::lock_guard lock(mutex_);

        // Close the channel
        channel->close();

        // Remove from subscribers list
        subscribers_.erase(
            std::remove_if(subscribers_.begin(), subscribers_.end(),
                [&channel](channel_ptr const& ch) {
                    return ch == channel || ch->is_open() == false;
                }),
            subscribers_.end()
        );
    }

    /// Publish an event to all subscribers.
    /// Subscribers that are full or closed are removed.
    void publish(Args... args) {
        if (stopped_) {
            return;
        }

        std::lock_guard lock(mutex_);

        // Remove closed channels and try to send to open ones
        subscribers_.erase(
            std::remove_if(subscribers_.begin(), subscribers_.end(),
                [&](channel_ptr& channel) {
                    if ( ! channel || ! channel->is_open()) {
                        return true;  // Remove closed channels
                    }

                    // try_send is non-blocking, returns false if channel is full
                    // We use it to avoid blocking the publisher
                    channel->try_send(std::error_code{}, args...);
                    return false;
                }),
            subscribers_.end()
        );
    }

    /// Close all subscriber channels.
    void close() {
        stopped_ = true;

        std::lock_guard lock(mutex_);

        for (auto& channel : subscribers_) {
            if (channel && channel->is_open()) {
                channel->close();
            }
        }
        subscribers_.clear();
    }

    /// Number of active subscribers.
    [[nodiscard]]
    size_t subscriber_count() const {
        std::lock_guard lock(mutex_);
        return subscribers_.size();
    }

    /// Check if stopped.
    [[nodiscard]]
    bool stopped() const {
        return stopped_;
    }

private:
    executor_type executor_;
    size_t buffer_size_;
    std::atomic<bool> stopped_;
    mutable std::mutex mutex_;
    std::vector<channel_ptr> subscribers_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_BROADCASTER_HPP
