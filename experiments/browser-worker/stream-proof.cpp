#include <kth/network/browser_stream.hpp>
#include <cstdio>
#include <stdexcept>
#include <asio/experimental/awaitable_operators.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <array>
#include <vector>
kth::browser::runtime owner;
auto owner_strand = asio::make_strand(owner.context());
asio::awaitable<void> control() {
    auto executor = co_await asio::this_coro::executor;
    kth::network::browser_stream stream(executor);
    co_await stream.async_connect("ws://127.0.0.1:18765", asio::use_awaitable);
    std::vector<unsigned char> sent(140000);
    for (std::size_t i=0; i<sent.size(); ++i) sent[i] = i % 251;
    auto written = co_await asio::async_write(stream, asio::buffer(sent), asio::use_awaitable);
    std::vector<unsigned char> received(sent.size());
    auto read = co_await asio::async_read(stream, asio::buffer(received), asio::use_awaitable);
    std::printf("stream bytes written=%zu read=%zu equal=%d affinity=%d\n", written, read, sent==received, owner_strand.running_in_this_thread());
    if (written!=sent.size() || read!=sent.size() || sent!=received || !owner_strand.running_in_this_thread()) throw std::runtime_error("stream/affinity mismatch");
    kth::browser::timer timer(executor);
    timer.expires_after(std::chrono::milliseconds(25));
    co_await timer.async_wait(asio::use_awaitable);
    std::printf("browser timer resumed affinity=%d\n", owner_strand.running_in_this_thread());
    timer.expires_after(std::chrono::seconds(10));
    auto wait_cancel = [&]() -> asio::awaitable<void> {
        auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        std::printf("timer cancellation=%d\n", ec == asio::error::operation_aborted);
        if (ec!=asio::error::operation_aborted) throw std::runtime_error("timer cancellation failed");
    };
    kth::browser::timer close_timer(executor);
    close_timer.expires_after(std::chrono::milliseconds(25));
    auto close_later = [&]() -> asio::awaitable<void> {
        co_await close_timer.async_wait(asio::use_awaitable);
        std::error_code ec; stream.close(ec); timer.cancel();
    };
    auto read_pending = [&]() -> asio::awaitable<void> {
        std::array<unsigned char, 1> byte{};
        auto [ec, size] = co_await stream.async_read_some(asio::buffer(byte), asio::as_tuple(asio::use_awaitable));
        std::printf("pending read cancellation=%d bytes=%zu\n", ec==asio::error::operation_aborted, size);
        if (ec!=asio::error::operation_aborted || size) throw std::runtime_error("pending read cancellation failed");
    };
    kth::task_group children("stream-control", executor);
    children.spawn("timer", wait_cancel());
    children.spawn("closer", close_later());
    children.spawn("reader", read_pending());
    co_await children.join();
    std::printf("nursery joined active=%zu total=%zu\n", children.active_count(), children.total_spawned());
}
int main() {
    asio::co_spawn(owner_strand, control(), [](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); } catch(std::exception const& e) { std::printf("FAILED %s\n", e.what()); } }
        else std::puts("stream control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
