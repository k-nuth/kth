// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node/header_sync.h>
#include <kth/node/header_sync.hpp>
#include <kth/infrastructure/utility/browser/runtime.hpp>
#include <asio/experimental/channel.hpp>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

namespace {
std::unique_ptr<kth::node::header_sync> client;
// Also compiles against the previous void ABI to reproduce its escaping exception.
template<class Reset>
bool reset(Reset function, kth_header_sync_mut_t handle) {
    if constexpr (std::is_void_v<std::invoke_result_t<Reset, kth_header_sync_mut_t>>) {
        function(handle);
        return true;
    } else return function(handle);
}
asio::awaitable<void> control() {
    auto executor = co_await asio::this_coro::executor;
    asio::experimental::channel<void(std::error_code, bool)> done(executor, 1);
    bool terminal = false;
    asio::co_spawn(executor, client->follow(), [&](std::exception_ptr error, auto result) {
        terminal = true;
        done.try_send(std::error_code{}, !error && bool(result));
    });
    // Start follow before probing reset. No network handshake is required.
    co_await asio::post(executor, asio::use_awaitable);
    if (!client->running()) throw std::runtime_error("follow did not start");
    auto const status = client->status();
    auto handle = static_cast<kth_header_sync_mut_t>(client.get());
    bool refused = false;
    try { refused = !reset(kth_node_header_sync_reset, handle); }
    catch (...) {
        // Join before reporting the regression so the control never destroys
        // a borrowed client while follow/peer tasks still reference it.
    }
    bool unchanged = client->running() && client->status() == status;
    co_await client->request_stop();
    auto stopped = co_await done.async_receive(asio::use_awaitable);
    if (!terminal || !stopped) throw std::runtime_error("follow did not join after stop");
    if (!refused || !unchanged) throw std::runtime_error("active reset threw or changed the session");
    if (!reset(kth_node_header_sync_reset, handle) || client->status() != "idle")
        throw std::runtime_error("reset failed after terminal callback");
}
}
int main() {
    client = std::make_unique<kth::node::header_sync>(
        "ws://127.0.0.1:18767", kth::domain::config::network::regtest, 100);
    asio::co_spawn(client->executor(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("RESET FAILED %s\n", e.what()); }
        } else std::puts("reset control joined");
        kth::browser::runtime::current().when_idle([] {
            auto& runtime = kth::browser::runtime::current();
            std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", runtime.timers(), runtime.streams());
        });
    });
}
