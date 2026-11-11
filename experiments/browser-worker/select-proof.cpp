#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <asio/experimental/channel_select.hpp>
#include <cstdio>
#include <memory>
#include <stdexcept>
kth::browser::runtime owner;
asio::awaitable<void> control() {
    auto executor = co_await asio::this_coro::executor;
    asio::experimental::select_channel<int> input(1);
    auto result = co_await asio::experimental::make_select(
        asio::experimental::receive(input),
        asio::experimental::after(std::chrono::steady_clock::now()+std::chrono::milliseconds(25))
    ).async_wait(executor, {}, std::stop_token{}, asio::use_awaitable);
    std::printf("select deadline fired index=%zu\n", result.index);
    if (result.index!=1) throw std::runtime_error("deadline not selected");

    // Deadlines past setTimeout's range (a signed 32-bit count of
    // milliseconds, about 24.8 days), and no deadline at all, are not selected
    // early: the message, sent later, is.
    for (auto const deadline : {std::chrono::steady_clock::now() + std::chrono::hours(25 * 24),
                                std::chrono::steady_clock::time_point::max()}) {
        kth::browser::timer later(executor, std::chrono::milliseconds(100));
        later.async_wait([&input](std::error_code) { (void)input.try_send(7); });
        auto waited = co_await asio::experimental::make_select(
            asio::experimental::receive(input),
            asio::experimental::after(deadline)
        ).async_wait(executor, {}, std::stop_token{}, asio::use_awaitable);
        std::printf("select with a far deadline index=%zu\n", waited.index);
        if (waited.index!=0) throw std::runtime_error("a far deadline was selected early");
    }

    // The same for a timer waited on directly: one 25 days away, and one with
    // no deadline, are still pending when a 100 ms one fires.
    for (auto const deadline : {std::chrono::steady_clock::now() + std::chrono::hours(25 * 24),
                                std::chrono::steady_clock::time_point::max()}) {
        kth::browser::timer far(executor);
        far.expires_at(deadline);
        auto far_fired = std::make_shared<bool>(false);
        far.async_wait([far_fired](std::error_code ec) { *far_fired = !ec; });
        kth::browser::timer near(executor, std::chrono::milliseconds(100));
        co_await near.async_wait(asio::use_awaitable);
        std::printf("timer with a far deadline fired=%d\n", int(*far_fired));
        if (*far_fired) throw std::runtime_error("a far timer fired early");
        far.cancel();
    }
}
int main() {
    asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("SELECT FAILED: %s\n", e.what()); }
        } else {
            std::puts("select control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
