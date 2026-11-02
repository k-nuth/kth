#include <kth/network/browser_stream.hpp>
#include <cstdio>
#include <stdexcept>
#include <vector>
kth::browser::runtime owner;
::asio::awaitable<void> control() {
    auto executor = co_await ::asio::this_coro::executor;
    kth::network::browser_stream stream(executor);
    co_await stream.async_connect("ws://127.0.0.1:18769", ::asio::use_awaitable);
    kth::browser::timer deadline(executor, std::chrono::milliseconds(500));
    co_await deadline.async_wait(::asio::use_awaitable);
    std::vector<unsigned char> bytes(100000);
    auto [ec, count] = co_await ::asio::async_read(stream, ::asio::buffer(bytes), ::asio::as_tuple(::asio::use_awaitable));
    bool equal = std::all_of(bytes.begin(), bytes.end(), [](auto byte){return byte==0x63;});
    std::printf("closed stream buffered bytes=%zu equal=%d error=%s\n",count,equal,ec.message().c_str());
    if (ec || count!=100000 || !equal) throw std::runtime_error("accepted final bytes lost");
    unsigned char byte{};
    auto [eof, remaining] = co_await stream.async_read_some(::asio::buffer(&byte,1),::asio::as_tuple(::asio::use_awaitable));
    std::printf("after drain eof=%d remaining=%zu\n",eof==::asio::error::eof,remaining);
    if (eof!=::asio::error::eof || remaining) throw std::runtime_error("EOF reason lost");
}
int main() {
    ::asio::co_spawn(owner.context(),control(),[](std::exception_ptr error){std::puts(error ? "EOF FAILED" : "eof control joined"); owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });});
    owner.wake();
}
