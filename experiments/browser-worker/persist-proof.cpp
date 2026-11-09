// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The header cache, as real WASM in a Worker, against the scripted BCH regtest
// peers in fixtures.py. The cache directory is the one an embedding Worker
// mounts on IndexedDB; here it lives in memory, which is all header_sync sees.
// A synchronized chain is restored, validated, by the next client without the
// network; it is extended in place, rewritten after a reorganization, and
// discarded when it does not validate.

#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/node/header_sync.hpp>
#include <asio/co_spawn.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <string>

kth::browser::runtime owner;

namespace {

using kth::node::header_sync;
constexpr auto regtest = kth::domain::config::network::regtest;
constexpr char directory[] = "/kth-header-cache/Regtest";
constexpr char file[] = "/kth-header-cache/Regtest/chain-v1.bin";

void require(bool condition, std::string const& what) {
    if (!condition) throw std::runtime_error(what);
}

// A client, or the control fails.
std::unique_ptr<header_sync> make_client(std::string const& url, kth::domain::config::network network, size_t capacity) {
    auto created = header_sync::create(url, network, capacity);
    require(bool(created), "the client was refused: " + url);
    return std::move(*created);
}

uintmax_t cached_headers() {
    std::error_code ec;
    auto const size = std::filesystem::file_size(file, ec);
    return ec ? 0 : size / 80;
}

::asio::awaitable<uint64_t> synchronize(char const* url) {
    auto client_owned = make_client(url, regtest, 10'000);
    auto& client = *client_owned;
    auto const height = co_await client.run();
    require(bool(height), std::string("synchronization failed: ") + client.status());
    co_return *height;
}

::asio::awaitable<void> control() {
    // Without the directory there is no cache, and nothing is written.
    require(co_await synchronize("ws://127.0.0.1:18780") == 2500, "synchronization without a cache");
    require(!std::filesystem::exists("/kth-header-cache"), "a cache was written without its directory");

    std::filesystem::create_directories(directory);
    require(co_await synchronize("ws://127.0.0.1:18780") == 2500, "first synchronization");
    require(cached_headers() == 2501, "the cache does not hold the chain: " + std::to_string(cached_headers()));

    // The next client starts at the cached tip, before any connection.
    std::string tip;
    std::string extended;
    {
        auto restored_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& restored = *restored_owned;
        require(restored.last_height() == 2500, "the cache was not restored");
        tip = restored.header_hash(2500);
    }
    std::puts("cached chain restored height=2500");

    // Extended in place by a longer chain; rewritten by a fork that wins.
    require(co_await synchronize("ws://127.0.0.1:18785") == 2600, "extension");
    require(cached_headers() == 2601, "the cache was not extended");
    {
        auto restored_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& restored = *restored_owned;
        require(restored.last_height() == 2600 && restored.header_hash(2500) == tip, "the extension was not restored");
        extended = restored.header_hash(2600);
    }
    // Two clients of the network, both restored at the same height, extend it
    // one after the other: the file is still one chain.
    {
        std::filesystem::remove(file);
        require(co_await synchronize("ws://127.0.0.1:18780") == 2500, "synchronization before two clients");
        auto first_owned = make_client("ws://127.0.0.1:18785", regtest, 10'000);
        auto& first = *first_owned;
        auto second_owned = make_client("ws://127.0.0.1:18785", regtest, 10'000);
        auto& second = *second_owned;
        require(first.last_height() == 2500 && second.last_height() == 2500, "two clients were not restored");
        require(co_await first.run() == 2600 && co_await second.run() == 2600, "two clients did not synchronize");
        require(cached_headers() == 2601, "two clients corrupted the cache: " + std::to_string(cached_headers()));
        auto restored_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& restored = *restored_owned;
        require(restored.last_height() == 2600, "the cache written by two clients was not restored");
    }
    std::puts("two clients kept one chain height=2600");

    require(co_await synchronize("ws://127.0.0.1:18786") == 2610, "reorganization");
    require(cached_headers() == 2611, "the cache was not rewritten");
    {
        auto restored_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& restored = *restored_owned;
        require(restored.last_height() == 2610 && restored.header_hash(2500) == tip &&
            restored.header_hash(2600) != extended, "the reorganized chain was not restored");
    }
    std::puts("cache extended, then rewritten after a reorganization height=2610");

    // A header that does not validate: the cache is discarded, the client
    // starts at genesis and synchronizes again.
    {
        std::fstream corrupt(file, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(100 * 80 + 4);  // the 100th header's previous hash
        corrupt.put('\xff');
    }
    {
        auto discarded_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& discarded = *discarded_owned;
        require(discarded.last_height() == 0, "a corrupt cache was restored");
        require(cached_headers() == 0, "a corrupt cache was kept");
    }
    require(co_await synchronize("ws://127.0.0.1:18780") == 2500, "synchronization after a discarded cache");
    require(cached_headers() == 2501, "the cache was not written again");
    std::puts("corrupt cache discarded and rebuilt");
}

} // namespace

int main() {
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("PERSIST FAILED: %s\n", e.what()); }
            catch (...) { std::puts("PERSIST FAILED"); }
        } else {
            std::puts("persist control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
