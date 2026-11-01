#include <kth/network/browser_stream.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
::asio::awaitable<void> control() {
    auto executor = co_await ::asio::this_coro::executor;
    kth::network::browser_stream stream(executor);
    co_await stream.async_connect("ws://127.0.0.1:18768", ::asio::use_awaitable);
    kth::browser::timer deadline(executor, std::chrono::milliseconds(500));
    co_await deadline.async_wait(::asio::use_awaitable);
    unsigned char byte{};
    auto [ec, bytes] = co_await stream.async_read_some(::asio::buffer(&byte,1), ::asio::as_tuple(::asio::use_awaitable));
    if (!ec || bytes) throw std::runtime_error("saturation was not observed");
    std::printf("bounded admission terminal error retained=%d bytes=%zu error=%s\n", ec==::asio::error::no_buffer_space, bytes, ec.message().c_str());
    if (ec!=::asio::error::no_buffer_space) throw std::runtime_error("terminal admission reason lost");
}
int main() {
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        std::puts(error ? "CAP FAILED" : "cap control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
