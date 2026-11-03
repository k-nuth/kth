// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/browser/runtime.hpp>
#include <asio/experimental/channel.hpp>
#include <cstdio>
#include <stdexcept>

namespace {
kth::browser::runtime owner;
void require(bool ok, char const* reason) {
    if (!ok) throw std::runtime_error(reason);
}
asio::awaitable<void> control() {
    using namespace std::chrono_literals;
    auto executor = co_await asio::this_coro::executor;
    for (bool infinite : {false, true}) {
        kth::browser::timer timer(executor), witness(executor, 25ms);
        if (infinite) timer.expires_at(kth::browser::timer::time_point::max());
        else timer.expires_after(std::chrono::hours(24 * 30));
        unsigned completed = 0;
        bool expired_early = false;
        timer.async_wait([&](std::error_code ec) {
            expired_early = ec != asio::error::operation_aborted;
            ++completed;
        });
        co_await witness.async_wait(asio::use_awaitable);
        require(!expired_early, "long timer expired early");
        require(completed == 0, "long timer completed before cancellation");
        require(timer.cancel() == 1, "long timer lost pending waiter");
        co_await asio::post(executor, asio::use_awaitable);
        require(completed == 1, "long timer cancellation did not complete");
    }
    kth::browser::timer timer(executor, 10ms);
    asio::experimental::channel<void(std::error_code, int)> done(executor, 2);
    for (int i = 0; i != 2; ++i) timer.async_wait([&, i](std::error_code ec) {
        require(!ec, "concurrent waiter failed");
        require(done.try_send(std::error_code{}, i), "duplicate timer completion");
    });
    auto first = co_await done.async_receive(asio::use_awaitable);
    auto second = co_await done.async_receive(asio::use_awaitable);
    require(first != second, "concurrent waiter was lost");
    timer.expires_after(1h);
    unsigned cancelled = 0;
    for (int i = 0; i != 2; ++i) timer.async_wait([&](std::error_code ec) {
        require(ec == asio::error::operation_aborted, "waiter not cancelled");
        ++cancelled;
    });
    require(timer.expires_after(1ms) == 2, "expiry change did not cancel both waiters");
    co_await timer.async_wait(asio::use_awaitable);
    require(cancelled == 2, "expiry change lost a waiter");
}
}
int main() {
    asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("TIMER FAILED %s\n", e.what()); }
        } else std::puts("timer control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
