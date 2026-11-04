#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <asio/experimental/channel_select.hpp>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
asio::awaitable<void> control() {
    asio::experimental::select_channel<int> input(1);
    auto result = co_await asio::experimental::make_select(
        asio::experimental::receive(input),
        asio::experimental::after(std::chrono::steady_clock::now()+std::chrono::milliseconds(25))
    ).async_wait(co_await asio::this_coro::executor, {}, std::stop_token{}, asio::use_awaitable);
    std::printf("select deadline fired index=%zu\n", result.index);
    if (result.index!=1) throw std::runtime_error("deadline not selected");
}
int main() {
    asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        std::puts(error ? "SELECT FAILED" : "select control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
