#include <kth/network/browser_stream.hpp>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
::asio::awaitable<void> control() {
    auto executor=co_await ::asio::this_coro::executor;
    {
        kth::network::browser_stream stream(executor);
        co_await stream.async_connect("ws://127.0.0.1:18770",::asio::use_awaitable);
        kth::browser::timer deadline(executor,std::chrono::milliseconds(500));
        co_await deadline.async_wait(::asio::use_awaitable);
        unsigned char byte{};
        auto [ec,count]=co_await stream.async_read_some(::asio::buffer(&byte,1),::asio::as_tuple(::asio::use_awaitable));
        std::printf("abrupt close reset=%d bytes=%zu\n",ec==::asio::error::connection_reset,count);
        if (ec!=::asio::error::connection_reset || count) throw std::runtime_error("abrupt close lost its error");
    }
    {
        kth::network::browser_stream stream(executor);
        co_await stream.async_connect("ws://127.0.0.1:18769",::asio::use_awaitable);
        kth::browser::timer deadline(executor,std::chrono::milliseconds(500));
        co_await deadline.async_wait(::asio::use_awaitable);
        std::error_code ignored; stream.cancel(ignored);
        unsigned char byte{};
        auto [ec,count]=co_await stream.async_read_some(::asio::buffer(&byte,1),::asio::as_tuple(::asio::use_awaitable));
        std::printf("explicit cancel after EOF discards admission=%d bytes=%zu\n",ec==::asio::error::operation_aborted,count);
        if (ec!=::asio::error::operation_aborted || count) throw std::runtime_error("explicit cancel preserved unread admission");
    }
}
int main() {
    ::asio::co_spawn(owner.context(),control(),[](std::exception_ptr failure){
        std::puts(failure ? "TERMINAL FAILED" : "terminal control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
