// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Content sampling, as real WASM in a Worker, against the scripted BCH
// regtest peers in fixtures.py: recent block bodies are fetched once caught
// up, checked against their headers and kept as metadata; an announced
// transaction is fetched and observed; a body that does not match its header
// rejects its peer.

#include <kth/capi/handles.h>
#include <kth/capi/node/header_sync.h>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/node/header_sync.hpp>
#include <kth/node/detail/header_sync_test_seam.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <string>

kth::browser::runtime owner;

namespace {

using kth::node::header_sync;
constexpr auto regtest = kth::domain::config::network::regtest;

void require(bool condition, std::string const& what) {
    if (!condition) throw std::runtime_error(what);
}

// A client, or the control fails.
std::unique_ptr<header_sync> make_client(std::string const& url, kth::domain::config::network network, size_t capacity) {
    auto created = header_sync::create(url, network, capacity);
    require(bool(created), "the client was refused: " + url);
    return std::move(*created);
}

bool contains(std::string const& text, std::string const& part) { return text.find(part) != std::string::npos; }

// Until `done` holds, rechecked every 100 ms, for at most 30 s.
template <typename Predicate>
::asio::awaitable<void> wait_for(Predicate done, std::string const& what) {
    auto executor = co_await ::asio::this_coro::executor;
    for (int turn = 0; turn < 300; ++turn) {
        if (done()) co_return;
        kth::browser::timer tick(executor, std::chrono::milliseconds(100));
        co_await tick.async_wait(::asio::use_awaitable);
    }
    throw std::runtime_error(what);
}

::asio::awaitable<void> control() {
    // Off by default: nothing is sampled.
    {
        auto client_owned = make_client("ws://127.0.0.1:18800", regtest, 1'000);
        auto& client = *client_owned;
        auto const height = co_await client.run();
        require(height && *height == 150, "synchronization: " + client.status());
        require(client.block_metadata(150, false) == "null", "a block was sampled while sampling was off");
        require(contains(client.content_state(), "\"cached_blocks\":0"), "content while off: " + client.content_state());
    }

    // Blocks and transactions: the window's 100 most recent bodies, each
    // checked; the announced transaction observed.
    {
        auto client_owned = make_client("ws://127.0.0.1:18800", regtest, 1'000);
        auto& client = *client_owned;
        require(client.configure_content(true, true), "content could not be configured");
        require(contains(client.runtime_settings(), "\"relay_transactions\":true"), "relay was not asked for");
        auto const height = co_await client.run();
        require(height && *height == 150, "synchronization with content: " + client.status());
        auto const state = client.content_state();
        std::printf("JSON %s\n", state.c_str());
        require(contains(state, "\"cached_blocks\":100") && contains(state, "\"rejected_bodies\":0"), "sampled blocks: " + state);
        require(client.block_metadata(50, false) == "null", "a block below the window was sampled");
        auto const tip = client.block_metadata(150, true);
        std::printf("JSON %s\n", tip.c_str());
        require(contains(tip, "\"hash\":\"" + client.header_hash(150) + "\"") && contains(tip, "\"transactions\":1") &&
            contains(tip, "\"coinbase_tag\":\"") && contains(tip, "kth browser node control") &&
            contains(tip, "\"coinbase_merkle_proof\":[]"), "the tip's metadata: " + tip);
        auto const observed = client.observed_transactions();
        std::printf("JSON %s\n", observed.c_str());
        require(contains(state, "\"observed_transactions\":1") && contains(observed, "\"txid\":\""),
            "the announced transaction was not observed: " + state + " " + observed);
    }

    // A body that does not match its header: the peer is rejected, and the
    // body counted. Through the C API.
    {
        kth_header_sync_mut_t handle = nullptr;
        require(kth_node_header_sync_construct("ws://127.0.0.1:18801", kth_network_regtest, 1'000, &handle) == kth_ec_success,
            "construction failed");
        require(kth_node_header_sync_configure_content(handle, 1, 0), "content could not be configured through the C API");
        header_sync& client = *reinterpret_cast<header_sync*>(handle);
        auto const height = co_await client.run();
        require(!height, "a peer with an invalid body succeeded");
        auto* state = kth_node_header_sync_content_state(handle);
        std::string const content = state ? state : "";
        kth_core_destruct_string(state);
        require(contains(content, "\"rejected_bodies\":1"), "the invalid body was not counted: " + content);
        auto* peer = kth_node_header_sync_peer_state(handle, 0);
        std::string const peer_state = peer ? peer : "";
        kth_core_destruct_string(peer);
        require(peer_state == "rejected", "the peer with an invalid body was not rejected: " + peer_state);
        require(kth_node_header_sync_destruct(handle) == kth_ec_success, "destruct");
    }
    // A block a peer has not got: the others are still sampled.
    {
        auto client_owned = make_client("ws://127.0.0.1:18809", regtest, 1'000);
        auto& client = *client_owned;
        require(client.configure_content(true, false), "content could not be configured");
        auto const height = co_await client.run();
        require(height && *height == 150, "synchronization: " + client.status());
        auto const state = client.content_state();
        require(contains(state, "\"cached_blocks\":99") && client.block_metadata(150, false) == "null",
            "one missing block stopped sampling: " + state);
    }

    // Transactions only: one of 300 KiB is accepted; a peer that leaves 16
    // requests unanswered gets them expired and the 17th asked for.
    for (auto const* url : {"ws://127.0.0.1:18808", "ws://127.0.0.1:18807"}) {
        auto client_owned = make_client(url, regtest, 1'000);
        auto& client = *client_owned;
        require(client.configure_content(false, true), "content could not be configured");
        auto watch = [&]() -> ::asio::awaitable<void> {
            co_await wait_for([&] { return contains(client.content_state(), "\"observed_transactions\":1"); },
                std::string("no transaction observed from ") + url + ": " + client.content_state() + " " + client.status());
            (void)co_await client.request_stop();
        };
        using namespace ::asio::experimental::awaitable_operators;
        (void)co_await (client.follow() && watch());
    }

    // A peer's notfound leaves another peer's request alone: the transaction
    // asked of the slow peer is still taken when it comes.
    {
        auto client_owned = make_client("ws://127.0.0.1:18811", regtest, 1'000);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18812"), "the second peer was refused");
        require(client.configure_content(false, true), "content could not be configured");
        auto watch = [&]() -> ::asio::awaitable<void> {
            co_await wait_for([&] { return contains(client.content_state(), "\"observed_transactions\":1"); },
                "another peer's notfound dropped the request: " + client.content_state() + " " + client.status());
            (void)co_await client.request_stop();
        };
        using namespace ::asio::experimental::awaitable_operators;
        (void)co_await (client.follow() && watch());
    }

    // A session task that fails ends its session, which is retried: the
    // transaction is still observed. The task throws while the peer keeps
    // answering; nothing else ends that session.
    {
        auto client_owned = make_client("ws://127.0.0.1:18811", regtest, 1'000);
        auto& client = *client_owned;
        require(client.configure_content(false, true), "content could not be configured");
        kth::node::detail::fail_next_transaction_requests();
        auto watch = [&]() -> ::asio::awaitable<void> {
            co_await wait_for([&] { return contains(client.content_state(), "\"observed_transactions\":1"); },
                "a failed transaction task left its session running: " + client.content_state() + " " + client.status());
            (void)co_await client.request_stop();
        };
        using namespace ::asio::experimental::awaitable_operators;
        (void)co_await (client.follow() && watch());
    }

    std::puts("content sampled and checked");
}

} // namespace

int main() {
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("CONTENT FAILED: %s\n", e.what()); }
            catch (...) { std::puts("CONTENT FAILED"); }
        } else {
            std::puts("content control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
