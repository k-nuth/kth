// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// header_sync against the scripted BCH regtest peer in fixtures.py, as real
// WASM in a Worker: synchronization across batches, a header with invalid
// proof of work, and following the tip through the C API (a ping answered,
// a block announced on the same connection, and a handle that cannot be
// destroyed or reset while it runs).

#include <kth/capi/node/header_sync.h>
#include <kth/capi/handles.h>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/node/header_sync.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <string>

// The application owns the Worker's runtime; header_sync uses it.
kth::browser::runtime owner;

namespace {

using kth::node::header_sync;
constexpr auto regtest = kth::domain::config::network::regtest;
constexpr char regtest_genesis[] = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";

void require(bool condition, char const* what) {
    if (!condition) throw std::runtime_error(what);
}

// A client, or the control fails.
std::unique_ptr<header_sync> make_client(std::string const& url, kth::domain::config::network network, size_t capacity) {
    auto created = header_sync::create(url, network, capacity);
    require(bool(created), "the client was refused");
    return std::move(*created);
}

// Until `done` holds, rechecked every 20 ms, for at most 20 s.
template <typename Predicate>
::asio::awaitable<void> wait_for(Predicate done, char const* what) {
    auto executor = co_await ::asio::this_coro::executor;
    for (int turn = 0; turn < 1000; ++turn) {
        if (done()) co_return;
        kth::browser::timer tick(executor, std::chrono::milliseconds(20));
        co_await tick.async_wait(::asio::use_awaitable);
    }
    throw std::runtime_error(what);
}

std::string owned(char* text) {
    std::string result = text ? text : "";
    kth_core_destruct_string(text);
    return result;
}

// Operations called from outside the runtime, as JavaScript calls them: each is
// queued, and has not started when the call returns.
struct queued {
    kth_header_sync_mut_t handle = nullptr;
    int handlers = 0;
    bool destroyed = false;
} queued_state;

void queued_handler(void* context) {
    auto& state = *static_cast<queued*>(context);
    if (++state.handlers == 2) state.destroyed = kth_node_header_sync_destruct(state.handle) == kth_ec_success;
}

// The handle is neither destroyed nor reset under a queued follow or stop request.
void queue_operations() {
    require(kth_node_header_sync_construct("ws://127.0.0.1:18780", kth_network_regtest, 10'000,
        &queued_state.handle) == kth_ec_success, "construction failed");
    auto const handle = queued_state.handle;
    kth_node_header_sync_async_follow(handle, &queued_state,
        [](kth_header_sync_mut_t, void* context, kth_error_code_t, uint64_t) { queued_handler(context); });
    require(!kth_node_header_sync_running(handle), "the follow was not queued");
    require(kth_node_header_sync_destruct(handle) == kth_ec_operation_failed,
        "a handle with a queued follow was destroyed");
    require(kth_node_header_sync_reset(handle) == kth_ec_operation_failed,
        "a handle with a queued follow was reset");
    kth_node_header_sync_async_request_stop(handle, &queued_state,
        [](kth_header_sync_const_t, void* context, kth_error_code_t, uint64_t) { queued_handler(context); });
    require(kth_node_header_sync_destruct(handle) == kth_ec_operation_failed,
        "a handle with a queued stop request was destroyed");
}

struct follow_result {
    bool done = false;
    kth_error_code_t status = kth_ec_success;
};

struct stop_result {
    bool done = false;
};

::asio::awaitable<void> control() {
    // Invalid arguments are refused, as values: an empty bridge URL, and a
    // capacity outside 1..2000000.
    require(!header_sync::create("", regtest, 100), "an empty URL was accepted");
    require(!header_sync::create("ws://127.0.0.1:18780", regtest, 0), "a capacity of 0 was accepted");
    require(!header_sync::create("ws://127.0.0.1:18780", regtest, 2'000'001), "a capacity past the maximum was accepted");
    {
        kth_header_sync_mut_t refused = nullptr;
        require(kth_node_header_sync_construct("ws://127.0.0.1:18780", kth_network_regtest, 0, &refused) ==
            kth_ec_operation_failed && refused == nullptr, "the C-API accepted a capacity of 0");
    }
    std::puts("invalid arguments refused");

    // 2500 headers, more than one batch of 2000.
    {
        auto client_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& client = *client_owned;
        auto const height = co_await client.run();
        if (!height || *height != 2500) {
            throw std::runtime_error("synchronization did not reach the peer's tip: height=" +
                std::to_string(client.last_height()) + " status=" + client.status());
        }
        require(client.status() == "caught_up", "synchronization did not end caught up");
        require(client.header_hash(0) == regtest_genesis, "genesis is not the regtest genesis");
        require(!client.header_hash(2500).empty() && client.header_hex(2500).size() == 160,
            "the tip header is not readable");
        require(client.header_hash(2501).empty(), "a header past the tip was returned");
        std::printf("headers synchronized height=%llu\n", static_cast<unsigned long long>(*height));
    }

    // A header with invalid proof of work at height 10 ends the session, and
    // nothing from it on is admitted.
    {
        auto client_owned = make_client("ws://127.0.0.1:18781", regtest, 100);
        auto& client = *client_owned;
        auto const height = co_await client.run();
        require(!height, "a header with invalid proof of work was accepted");
        require(client.last_height() < 10, "an invalid header was admitted");
        std::printf("invalid proof of work refused height=%llu\n",
            static_cast<unsigned long long>(client.last_height()));
    }

    // A message larger than any a header client accepts ends the session at
    // its heading: neither waited for (a response deadline) nor allocated.
    {
        auto client_owned = make_client("ws://127.0.0.1:18783", regtest, 100);
        auto& client = *client_owned;
        auto const started = std::chrono::steady_clock::now();
        auto const height = co_await client.run();
        auto const waited = std::chrono::steady_clock::now() - started;
        require(!height, "an oversized message was accepted");
        // Without the bound the client waits for the payload until its 30 s
        // response deadline.
        if (waited > std::chrono::seconds(5)) {
            throw std::runtime_error("an oversized message did not end the session at its heading: status=" +
                client.status());
        }
        std::puts("oversized message refused at its heading");
    }

    // The operations queued from main() complete; the handle is destroyed
    // from the last handler.
    co_await wait_for([] { return queued_state.handlers == 2; }, "the queued operations did not complete");
    require(queued_state.destroyed, "the handle could not be destroyed from its last handler");
    std::puts("queued operations held the handle");

    // A heavier branch announced while following: the client reorganizes to
    // it on the same connection, and keeps following.
    {
        auto client_owned = make_client("ws://127.0.0.1:18787", regtest, 10'000);
        auto& client = *client_owned;
        auto watch = [&]() -> ::asio::awaitable<void> {
            co_await wait_for([&] { return client.last_height() == 2510 || (client.status() != "idle" && !client.running()); },
                "the heavier branch was not followed");
            (void)co_await client.request_stop();
        };
        using namespace ::asio::experimental::awaitable_operators;
        auto const followed = co_await (client.follow() && watch());
        if (client.last_height() != 2510) {
            throw std::runtime_error("the heavier branch was not followed: height=" +
                std::to_string(client.last_height()) + " status=" + client.status());
        }
        require(bool(followed), "following ended with an error after the reorganization");
        std::puts("heavier branch followed height=2510");
    }

    // A branch whose stored headers are announced again, after a batch that
    // failed past them: the retry adopts it, as progress.
    {
        auto client_owned = make_client("ws://127.0.0.1:18788", regtest, 10'000);
        auto& client = *client_owned;
        auto watch = [&]() -> ::asio::awaitable<void> {
            co_await wait_for([&] { return client.status() != "idle" && !client.running(); },
                "the invalid branch did not end the first session");
        };
        using namespace ::asio::experimental::awaitable_operators;
        auto const first = co_await (client.follow() && watch());
        require(!first, "a branch with an invalid header was followed");
        require(client.last_height() == 2500, "the first session moved the tip");
        require(!client.reset(), "the client could not be reset");
        auto const retried = co_await client.run();
        if (!retried || *retried != 2504) {
            throw std::runtime_error("the stored branch was not adopted on retry: height=" +
                std::to_string(client.last_height()) + " status=" + client.status());
        }
        std::puts("stored branch adopted on retry height=2504");
    }

    // Following the tip, through the C API.
    {
        kth_header_sync_mut_t handle = nullptr;
        require(kth_node_header_sync_construct("ws://127.0.0.1:18782", kth_network_regtest, 10'000, &handle) == kth_ec_success,
            "construction failed");
        require(kth_node_header_sync_configure_legacy_discovery(handle, 1), "legacy configuration refused while settled");
        require(kth_node_header_sync_configure_legacy_discovery(handle, 0), "legacy configuration could not be disabled");
        follow_result followed;
        kth_node_header_sync_async_follow(handle, &followed,
            [](kth_header_sync_mut_t, void* context, kth_error_code_t status, uint64_t) {
                auto& result = *static_cast<follow_result*>(context);
                result.status = status;
                result.done = true;
            });
        // The peer pings before its first answer, and announces block 2005 once
        // the client has caught up at 2000: it is fetched on the same connection.
        co_await wait_for([&] { return kth_node_header_sync_last_height(handle) == 2005; },
            "the announced block was not fetched");
        require(!followed.done, "follow ended at the tip");

        // Still running: neither destroyed nor reset.
        require(kth_node_header_sync_destruct(handle) == kth_ec_operation_failed,
            "a running client was destroyed");
        require(kth_node_header_sync_reset(handle) == kth_ec_operation_failed,
            "a running client was reset");
        require(owned(kth_node_header_sync_header_hash(handle, 0)) == regtest_genesis,
            "genesis is not readable through the C API");
        // Its one peer, through the C API: connected while following, and no
        // peer can be added meanwhile.
        require(kth_node_header_sync_peer_count(handle) == 1, "the C API does not report one peer");
        require(owned(kth_node_header_sync_peer_endpoint(handle, 0)) == "ws://127.0.0.1:18782", "the peer endpoint");
        co_await wait_for([&] { return owned(kth_node_header_sync_peer_state(handle, 0)) == "caught_up"; },
            "the peer did not report caught_up");
        require(owned(kth_node_header_sync_peer_agent(handle, 0)) == "/kth-ci-peer/", "the peer agent");
        require(kth_node_header_sync_peer_connected(handle, 0) && kth_node_header_sync_peer_height(handle, 0) == 2005,
            "the peer session");
        require(kth_node_header_sync_peer_announcements(handle, 0) >= 1, "the announcement was not counted");
        require(owned(kth_node_header_sync_peer_endpoint(handle, 1)).empty(), "a peer past the last one");
        require(!kth_node_header_sync_add_peer(handle, "ws://127.0.0.1:18785"), "a peer was added while following");
        require(!kth_node_header_sync_configure_discovery(handle, 0, 0, 0), "discovery was configured while following");
        require(!kth_node_header_sync_configure_legacy_discovery(handle, 1), "legacy mode was configured while following");

        stop_result stopped;
        kth_node_header_sync_async_request_stop(handle, &stopped,
            [](kth_header_sync_const_t, void* context, kth_error_code_t, uint64_t) {
                static_cast<stop_result*>(context)->done = true;
            });
        co_await wait_for([&] { return stopped.done && followed.done; }, "follow did not settle after stop");
        require(followed.status == kth_ec_success, "a stopped follow reported an error");
        require(owned(kth_node_header_sync_status(handle)) == "stopped", "follow did not end stopped");
        require(kth_node_header_sync_reset(handle) == kth_ec_success, "a settled client could not be reset");
        require(kth_node_header_sync_destruct(handle) == kth_ec_success, "a settled client could not be destroyed");
        std::puts("follow fetched an announced block height=2005 and stopped");
    }
}

} // namespace

int main() {
    try {
        queue_operations();
    } catch (std::exception const& e) {
        std::printf("HEADERS FAILED: %s\n", e.what());
        return 0;
    }
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("HEADERS FAILED: %s\n", e.what()); }
            catch (...) { std::puts("HEADERS FAILED"); }
        } else {
            std::puts("headers control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
