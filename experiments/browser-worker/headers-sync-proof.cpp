// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <asio/co_spawn.hpp>
#include <emscripten.h>
#include <cstdio>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

// Application binding integration is separate; this control exercises the library actor directly.
namespace {
std::unique_ptr<kth::node::header_sync> client;
long progress_timer = 0;
long stop_timer = 0;
uint64_t displayed = 0;
void progress(void*) {
    auto height = client->last_height();
    if (height != displayed) {
        displayed = height;
        std::printf("HEADERS validated height=%llu count=%llu memory=%zu\n",
                    static_cast<unsigned long long>(height), static_cast<unsigned long long>(height + 1),
                    client->index_memory_bytes());
    }
}
}

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::warn);
    auto network = argc > 2 && std::string(argv[2]) == "regtest" ? kth::domain::config::network::regtest
                                                               : kth::domain::config::network::mainnet;
    client = std::make_unique<kth::node::header_sync>(argc > 1 ? argv[1] : "ws://127.0.0.1:18766", network, 1'500'000);
    progress_timer = emscripten_set_interval(progress, 50, nullptr);
    if (argc > 3 && std::string(argv[3]) == "stop") {
        stop_timer = emscripten_set_timeout([](void*) {
            stop_timer = 0;
            ::asio::co_spawn(client->executor(), client->request_stop(),
                [](std::exception_ptr error, std::expected<uint64_t, std::error_code> result) {
                    if (error || !result) std::puts("HEADERS FAILED stop request");
                    else std::puts("HEADERS stop requested; waiting for run completion");
                });
        }, 100, nullptr);
    }
    ::asio::co_spawn(client->executor(), client->run(), [](std::exception_ptr error, std::expected<uint64_t, std::error_code> result) {
        emscripten_clear_interval(progress_timer);
        if (stop_timer) { emscripten_clear_timeout(stop_timer); stop_timer = 0; }
        progress(nullptr);
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& exception) { std::printf("HEADERS FAILED %s\n", exception.what()); }
        } else if (!result) {
            std::printf("HEADERS FAILED %s\n", client->status().c_str());
        } else if (client->status() == "stopped") {
            std::puts("HEADERS stopped");
        } else {
            std::printf("HEADERS caught_up height=%llu memory=%zu\n",
                        static_cast<unsigned long long>(*result), client->index_memory_bytes());
            std::puts("HEADERS joined");
        }
        kth::browser::runtime::current().when_idle([] {
            auto& runtime = kth::browser::runtime::current();
            std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", runtime.timers(), runtime.streams());
        });
    });
}
