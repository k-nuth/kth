#include <kth/node/p2p_node.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
kth::network::settings configuration() {
    kth::network::settings result(kth::domain::config::network::regtest);
    result.browser_bridge_url = "ws://127.0.0.1:18767";
    result.seeds.clear();
    result.peers.clear();
    result.peers.emplace_back("127.0.0.1", 19444);
    result.outbound_connections = 0;
    result.inbound_connections = 0;
    result.channel_handshake_seconds = 5;
    return result;
}
::asio::awaitable<void> control() {
    auto executor = co_await ::asio::this_coro::executor;
    {
        auto config = configuration(); config.browser_bridge_url.clear();
        kth::node::p2p_node node(config);
        auto ec = co_await node.start();
        if (!ec) throw std::runtime_error("invalid start admitted");
        node.stop(); node.join();
        std::puts("refused P2P start then stop completed");
    }
    {
        auto config = configuration();
        kth::node::p2p_node node(config);
        node.stop();
        auto ec = co_await node.start();
        if (!ec) throw std::runtime_error("start after stop admitted");
        node.join();
        std::puts("P2P stop before start refused admission");
    }
    {
        auto config = configuration();
        kth::node::p2p_node node(config);
        auto ec = co_await node.start();
        if (ec) throw std::runtime_error(ec.message());
        kth::steady_timer deadline(executor, std::chrono::milliseconds(100));
        auto started = std::chrono::steady_clock::now();
        auto stop_later = [&]() -> ::asio::awaitable<void> {
            co_await deadline.async_wait(::asio::use_awaitable);
            std::puts("stop requested during pending BCH handshake");
            node.stop();
        };
        auto run = [&]() -> ::asio::awaitable<void> { co_await node.run(); };
        using namespace ::asio::experimental::awaitable_operators;
        co_await (run() && stop_later());
        node.join();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::printf("pending handshake stop/join elapsed_ms=%lld\n", (long long)elapsed);
        if (elapsed > 1000) throw std::runtime_error("pending handshake shutdown exceeded bound");
    }
}
int main() {
    spdlog::set_level(spdlog::level::warn);
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); } catch(std::exception const& e) { std::printf("LIFECYCLE FAILED %s\n", e.what()); } }
        else std::puts("P2P lifecycle control joined");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
