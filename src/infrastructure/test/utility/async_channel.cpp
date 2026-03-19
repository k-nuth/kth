// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

#if defined(KTH_ASIO_STANDALONE)
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/as_tuple.hpp>
#else
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/system/error_code.hpp>
#endif

using namespace kth;
using namespace std::chrono_literals;

// Use the appropriate error_code type
#if defined(KTH_ASIO_STANDALONE)
using error_code_t = std::error_code;
#else
using error_code_t = boost::system::error_code;
#endif

// =============================================================================
// async_channel<T> tests
// =============================================================================

TEST_CASE("async_channel basic send and receive", "[async_channel]") {
    ::asio::io_context ctx;

    SECTION("send and receive single value") {
        async_channel<int> channel(ctx.get_executor(), 1);

        bool received = false;
        int received_value = 0;

        // Producer coroutine
        auto producer = [&]() -> ::asio::awaitable<void> {
            co_await channel.async_send(error_code_t{}, 42, ::asio::use_awaitable);
        };

        // Consumer coroutine
        auto consumer = [&]() -> ::asio::awaitable<void> {
            auto [ec, value] = co_await channel.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                received = true;
                received_value = value;
            }
        };

        ::asio::co_spawn(ctx, producer(), ::asio::detached);
        ::asio::co_spawn(ctx, consumer(), ::asio::detached);

        ctx.run();

        CHECK(received);
        CHECK(received_value == 42);
    }

    SECTION("send and receive string") {
        async_channel<std::string> channel(ctx.get_executor(), 1);

        std::string received_value;

        auto producer = [&]() -> ::asio::awaitable<void> {
            co_await channel.async_send(error_code_t{}, std::string("hello"), ::asio::use_awaitable);
        };

        auto consumer = [&]() -> ::asio::awaitable<void> {
            auto [ec, value] = co_await channel.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                received_value = std::move(value);
            }
        };

        ::asio::co_spawn(ctx, producer(), ::asio::detached);
        ::asio::co_spawn(ctx, consumer(), ::asio::detached);

        ctx.run();

        CHECK(received_value == "hello");
    }
}

TEST_CASE("async_channel buffering", "[async_channel]") {
    ::asio::io_context ctx;

    SECTION("buffered channel allows multiple sends before receive") {
        async_channel<int> channel(ctx.get_executor(), 3);

        std::vector<int> received_values;

        auto producer = [&]() -> ::asio::awaitable<void> {
            co_await channel.async_send(error_code_t{}, 1, ::asio::use_awaitable);
            co_await channel.async_send(error_code_t{}, 2, ::asio::use_awaitable);
            co_await channel.async_send(error_code_t{}, 3, ::asio::use_awaitable);
        };

        auto consumer = [&]() -> ::asio::awaitable<void> {
            for (int i = 0; i < 3; ++i) {
                auto [ec, value] = co_await channel.async_receive(::asio::as_tuple(::asio::use_awaitable));
                if (!ec) {
                    received_values.push_back(value);
                }
            }
        };

        ::asio::co_spawn(ctx, producer(), ::asio::detached);
        ::asio::co_spawn(ctx, consumer(), ::asio::detached);

        ctx.run();

        REQUIRE(received_values.size() == 3);
        CHECK(received_values[0] == 1);
        CHECK(received_values[1] == 2);
        CHECK(received_values[2] == 3);
    }
}

// =============================================================================
// event_channel tests
// =============================================================================

TEST_CASE("event_channel signaling", "[async_channel][event]") {
    ::asio::io_context ctx;

    SECTION("signal and wait") {
        event_channel channel(ctx.get_executor(), 1);

        bool signaled = false;

        auto signaler = [&]() -> ::asio::awaitable<void> {
            co_await channel.async_send(error_code_t{}, ::asio::use_awaitable);
        };

        auto waiter = [&]() -> ::asio::awaitable<void> {
            auto [ec] = co_await channel.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                signaled = true;
            }
        };

        ::asio::co_spawn(ctx, signaler(), ::asio::detached);
        ::asio::co_spawn(ctx, waiter(), ::asio::detached);

        ctx.run();

        CHECK(signaled);
    }
}

// =============================================================================
// multi_channel tests
// =============================================================================

TEST_CASE("multi_channel multiple values", "[async_channel][multi]") {
    ::asio::io_context ctx;

    SECTION("send and receive tuple of values") {
        multi_channel<int, std::string> channel(ctx.get_executor(), 1);

        int received_int = 0;
        std::string received_str;

        auto producer = [&]() -> ::asio::awaitable<void> {
            co_await channel.async_send(error_code_t{}, 42, std::string("test"), ::asio::use_awaitable);
        };

        auto consumer = [&]() -> ::asio::awaitable<void> {
            auto [ec, i, s] = co_await channel.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (!ec) {
                received_int = i;
                received_str = std::move(s);
            }
        };

        ::asio::co_spawn(ctx, producer(), ::asio::detached);
        ::asio::co_spawn(ctx, consumer(), ::asio::detached);

        ctx.run();

        CHECK(received_int == 42);
        CHECK(received_str == "test");
    }
}

// =============================================================================
// async_channel with strand (NOT thread-safe, requires strand serialization)
// =============================================================================
//
// IMPORTANT: async_channel is NOT thread-safe for concurrent access.
// Documentation: "Distinct objects: Safe. Shared objects: Unsafe."
// When using async_channel from multiple threads, ALL access must be
// serialized through a strand.
//
// =============================================================================

TEST_CASE("async_channel with strand serialization", "[async_channel][threadpool][strand]") {
    SECTION("multiple producers with strand serialization") {
        threadpool pool("test", 4);

        // Create strand to serialize all channel access
        ::asio::strand<::asio::any_io_executor> strand(pool.get_executor());

        // Channel bound to strand - all operations will be serialized
        async_channel<int> channel(strand, 10);

        std::atomic<int> sum{0};
        std::atomic<int> count{0};
        std::atomic<int> send_count{0};
        std::atomic<bool> consumer_done{false};

        // 5 producer coroutines - spawned on strand for serialized access
        for (int i = 1; i <= 5; ++i) {
            ::asio::co_spawn(strand,
                [&channel, &send_count, val = i]() -> ::asio::awaitable<void> {
                    auto [ec] = co_await channel.async_send(
                        error_code_t{}, val,
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        ++send_count;
                    }
                }, ::asio::detached);
        }

        // Consumer coroutine - also on strand
        ::asio::co_spawn(strand,
            [&channel, &sum, &count, &consumer_done]() -> ::asio::awaitable<void> {
                for (int i = 0; i < 5; ++i) {
                    auto [ec, value] = co_await channel.async_receive(
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        sum += value;
                        ++count;
                    }
                }
                consumer_done = true;
            }, ::asio::detached);

        // Wait for consumer to complete
        while (!consumer_done.load()) {
            std::this_thread::sleep_for(1ms);
        }

        pool.stop();
        pool.join();

        INFO("send_count = " << send_count.load());
        INFO("count = " << count.load());
        INFO("sum = " << sum.load());

        CHECK(send_count.load() == 5);
        CHECK(count.load() == 5);
        CHECK(sum.load() == 15);  // 1+2+3+4+5
    }
}

// =============================================================================
// concurrent_channel (thread-safe, no strand needed)
// =============================================================================
//
// concurrent_channel is thread-safe for concurrent access from multiple threads.
// Use this when producers/consumers run on different threads of the pool.
//
// =============================================================================

TEST_CASE("concurrent_channel without strand", "[concurrent_channel][threadpool]") {
    SECTION("multiple producers from different threads - thread safe") {
        threadpool pool("test", 4);

        // concurrent_channel is thread-safe, no strand needed
        concurrent_channel<int> channel(pool.get_executor(), 10);

        std::atomic<int> sum{0};
        std::atomic<int> count{0};
        std::atomic<int> send_count{0};
        std::atomic<bool> consumer_done{false};

        // 5 producer coroutines - can run on any thread safely
        for (int i = 1; i <= 5; ++i) {
            ::asio::co_spawn(pool.get_executor(),
                [&channel, &send_count, val = i]() -> ::asio::awaitable<void> {
                    auto [ec] = co_await channel.async_send(
                        error_code_t{}, val,
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        ++send_count;
                    }
                }, ::asio::detached);
        }

        // Consumer coroutine - can run on any thread
        ::asio::co_spawn(pool.get_executor(),
            [&channel, &sum, &count, &consumer_done]() -> ::asio::awaitable<void> {
                for (int i = 0; i < 5; ++i) {
                    auto [ec, value] = co_await channel.async_receive(
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        sum += value;
                        ++count;
                    }
                }
                consumer_done = true;
            }, ::asio::detached);

        // Wait for consumer to complete
        while (!consumer_done.load()) {
            std::this_thread::sleep_for(1ms);
        }

        pool.stop();
        pool.join();

        INFO("send_count = " << send_count.load());
        INFO("count = " << count.load());
        INFO("sum = " << sum.load());

        CHECK(send_count.load() == 5);
        CHECK(count.load() == 5);
        CHECK(sum.load() == 15);  // 1+2+3+4+5
    }

    SECTION("high contention stress test") {
        threadpool pool("test", 8);
        concurrent_channel<int> channel(pool.get_executor(), 100);

        constexpr int num_producers = 20;
        constexpr int items_per_producer = 10;
        constexpr int total_items = num_producers * items_per_producer;

        std::atomic<int> sum{0};
        std::atomic<int> send_count{0};
        std::atomic<int> receive_count{0};
        std::atomic<bool> done{false};

        // Many producers sending concurrently
        for (int p = 0; p < num_producers; ++p) {
            ::asio::co_spawn(pool.get_executor(),
                [&channel, &send_count, p]() -> ::asio::awaitable<void> {
                    for (int i = 0; i < items_per_producer; ++i) {
                        int value = p * items_per_producer + i + 1;
                        auto [ec] = co_await channel.async_send(
                            error_code_t{}, value,
                            ::asio::as_tuple(::asio::use_awaitable));
                        if (!ec) {
                            ++send_count;
                        }
                    }
                }, ::asio::detached);
        }

        // Single consumer receiving all items
        ::asio::co_spawn(pool.get_executor(),
            [&channel, &sum, &receive_count, &done]() -> ::asio::awaitable<void> {
                for (int i = 0; i < total_items; ++i) {
                    auto [ec, value] = co_await channel.async_receive(
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        sum += value;
                        ++receive_count;
                    }
                }
                done = true;
            }, ::asio::detached);

        // Wait for completion
        while (!done.load()) {
            std::this_thread::sleep_for(1ms);
        }

        pool.stop();
        pool.join();

        // Expected sum: 1+2+3+...+200 = 200*201/2 = 20100
        int expected_sum = total_items * (total_items + 1) / 2;

        INFO("send_count = " << send_count.load());
        INFO("receive_count = " << receive_count.load());
        INFO("sum = " << sum.load());
        INFO("expected_sum = " << expected_sum);

        CHECK(send_count.load() == total_items);
        CHECK(receive_count.load() == total_items);
        CHECK(sum.load() == expected_sum);
    }
}
