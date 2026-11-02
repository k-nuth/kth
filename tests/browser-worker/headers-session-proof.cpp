// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#include <kth/infrastructure/utility/browser/runtime.hpp>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
std::unique_ptr<kth::node::header_sync> client;
asio::awaitable<void> control() {
    auto result = co_await client->run();
    if (!result || *result != 5 || client->last_height() != 5)
        throw std::runtime_error("duplicate/reorg session did not reach height 5: " + client->status());
    // The fixture's branch B has a different Merkle root and timestamp at height 2.
    // Its tip hash pins adoption of the heavier branch, not just stored height.
    if (client->header_hash(5) != "6d122a96faa90126de2547dd07595f85d69164e98473992847b4da894f8189b1")
        throw std::runtime_error("heavier branch was not adopted");
}
}
int main(int argc, char** argv) {
    client = std::make_unique<kth::node::header_sync>(
        argc > 1 ? argv[1] : "ws://127.0.0.1:18971", kth::domain::config::network::regtest, 100);
    client->configure_discovery(false, false, false);
    asio::co_spawn(client->executor(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("HEADERS FAILED %s\n", e.what()); }
        } else std::puts("headers-session control joined");
        kth::browser::runtime::current().when_idle([] {
            auto& runtime = kth::browser::runtime::current();
            std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", runtime.timers(), runtime.streams());
        });
    });
}
