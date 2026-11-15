#include <kth/network/browser_stream.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
::asio::awaitable<void> control() {
    auto executor = co_await ::asio::this_coro::executor;
    kth::network::browser_stream stream(executor);
    co_await stream.async_connect("ws://127.0.0.1:18768", ::asio::use_awaitable);

    // 1000 single-byte messages: a session that bounds bytes, not messages,
    // keeps them, and reads them back in order.
    size_t small = 0;
    while (small < 1000) {
        std::array<unsigned char, 256> chunk{};
        // Never past the 1000th byte: the marker follows it.
        auto const wanted = std::min(chunk.size(), size_t(1000) - small);
        auto [ec, bytes] = co_await stream.async_read_some(::asio::buffer(chunk.data(), wanted), ::asio::as_tuple(::asio::use_awaitable));
        if (ec) throw std::runtime_error("small messages ended the session: " + ec.message());
        for (size_t i = 0; i < bytes; ++i) {
            if (chunk[i] != 's') throw std::runtime_error("small message bytes reordered");
        }
        small += bytes;
    }
    unsigned char marker{};
    auto [mark_ec, mark_bytes] = co_await stream.async_read_some(::asio::buffer(&marker, 1), ::asio::as_tuple(::asio::use_awaitable));
    if (mark_ec || mark_bytes != 1 || marker != 'm') throw std::runtime_error("marker lost");
    std::printf("small messages kept=%zu\n", small);
    // Read everything so far: the fixture may send the rest.
    unsigned char ready = 'r';
    co_await ::asio::async_write(stream, ::asio::buffer(&ready, 1), ::asio::use_awaitable);

    // Then more than the admitted bytes arrive unread: the session ends with the
    // admission bound as its reason.
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
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("CAP FAILED: %s\n", e.what()); }
            catch (...) { std::puts("CAP FAILED"); }
        } else {
            std::puts("cap control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
