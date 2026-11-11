// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// header_sync with several peers, as real WASM in a Worker, against the
// scripted BCH regtest peers in fixtures.py: peers synchronize one index
// together, a peer that sends invalid data or cannot be reached does not stop
// the others, and a peer learned through endpoint discovery joins.

#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/node/header_sync.hpp>
#include <kth/domain/version.hpp>
#include <asio/co_spawn.hpp>
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

std::string describe(header_sync const& client) {
    std::string out = "status=" + client.status();
    for (size_t at = 0; at < client.peer_count(); ++at) {
        out += " [" + client.peer_endpoint(at) + " " + client.peer_state(at) + " h=" +
            std::to_string(client.peer_height(at)) + " " + client.peer_error(at) + "]";
    }
    return out;
}

::asio::awaitable<void> control() {
    // Configuration: distinct ws/wss URLs, at most eight, while settled.
    {
        auto client_owned = make_client("ws://127.0.0.1:18780", regtest, 100);
        auto& client = *client_owned;
        require(!client.add_peer("ws://127.0.0.1:18780"), "a duplicate peer was added");
        require(!client.add_peer("http://127.0.0.1:18785"), "a peer that is not ws/wss was added");
        require(!client.add_peer("ws://"), "a peer without a host was added");
        for (int port = 18790; port < 18797; ++port) {
            require(client.add_peer("ws://127.0.0.1:" + std::to_string(port)), "a peer was refused below the limit");
        }
        require(client.peer_count() == 8, "eight peers were not kept");
        require(!client.add_peer("ws://127.0.0.1:18797"), "a ninth peer was added");
        require(client.peer_source(1) == "configured", "a configured peer has another source");
    }
    std::puts("peer configuration bounded");

    // Two peers, one index: the longer chain wins, each peer reports its own.
    {
        auto client_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18785"), "the second peer was refused");
        auto const height = co_await client.run();
        require(height && *height == 2600, "two peers did not synchronize: " + describe(client));
        require(client.peer_height(0) == 2500 && client.peer_height(1) == 2600, "peer heights: " + describe(client));
        require(client.peer_state(0) == "caught_up" && client.peer_state(1) == "caught_up", "peer states: " + describe(client));
        require(!client.peer_connected(0) && client.peer_agent(0).empty(), "a finished peer still reports a session");

        // Snapshots: the tip's consensus state and the effective settings, as
        // JSON (ci.py parses every "JSON " line).
        auto const state = client.chain_state_snapshot();
        auto const settings = client.runtime_settings();
        std::printf("JSON %s\n", state.c_str());
        std::printf("JSON %s\n", settings.c_str());
        require(state.find("\"height\":2600") != std::string::npos &&
            state.find("\"hash\":\"" + client.header_hash(2600) + "\"") != std::string::npos, "chain state: " + state);
        require(state.find("\"upgrades\":{") != std::string::npos && state.find("\"abla\":{\"available\":false") != std::string::npos,
            "chain state sections: " + state);
        for (auto const* expected : {"\"sync_peers\":8", "\"services\":0", "\"relay_transactions\":false",
                "\"ws_discovery_legacy_agent\":false", "\"network\":\"Regtest\"",
                "\"peer_endpoints\":[\"ws://127.0.0.1:18780\",\"ws://127.0.0.1:18785\"]",
                "\"cache_directory\":\"/kth-header-cache/Regtest\""}) {
            require(settings.find(expected) != std::string::npos, std::string("settings lack ") + expected + ": " + settings);
        }
    }
    {
        auto fresh_owned = make_client("ws://127.0.0.1:18780", regtest, 100);
        auto& fresh = *fresh_owned;
        require(fresh.chain_state_snapshot() == "null", "a genesis-only client has a chain state");
        // An endpoint with a quote and a backslash is escaped in the JSON.
        require(fresh.add_peer("ws://127.0.0.1:18798/\"q\\"), "the quoted endpoint was refused");
        std::printf("JSON %s\n", fresh.runtime_settings().c_str());
        auto const agent = "\"user_agent\":\"/Knuth:" + std::string(kth::version) + "(browser)/\"";
        require(fresh.runtime_settings().find(agent) != std::string::npos, "browser identification");
        require(fresh.configure_discovery(false, false, false) &&
            fresh.runtime_settings().find(agent) != std::string::npos,
            "disabling discovery changed identification");
        require(fresh.configure_legacy_discovery(true) &&
            fresh.runtime_settings().find("\"ws_discovery_legacy_agent\":true") != std::string::npos,
            "legacy migration mode was not configured");
    }
    std::puts("two peers synchronized one index height=2600");

    // A peer with invalid proof of work, and one that is not there: neither
    // stops the good one. The invalid one is rejected, the absent one is not.
    {
        auto client_owned = make_client("ws://127.0.0.1:18781", regtest, 10'000);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18799") && client.add_peer("ws://127.0.0.1:18780"), "peers refused");
        auto const height = co_await client.run();
        require(height && *height == 2500, "a bad peer stopped the good one: " + describe(client));
        require(client.peer_state(0) == "rejected" && !client.peer_error(0).empty(), "the invalid peer was not rejected: " + describe(client));
        require(client.peer_state(1) == "disconnected" && !client.peer_error(1).empty(), "the absent peer: " + describe(client));
        require(client.peer_state(2) == "caught_up", "the good peer: " + describe(client));
        // reset() gives the rejected peer another chance.
        require(!client.reset() && client.peer_state(0) == "idle" && client.peer_error(0).empty(), "reset kept the rejection");
    }
    std::puts("invalid and absent peers did not stop the good one");

    // A peer flooding more headers than fit, the first past the capacity with
    // invalid proof of work, is rejected; it does not stop the peer whose
    // chain fits.
    {
        auto client_owned = make_client("ws://127.0.0.1:18803", regtest, 100);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18804"), "the short peer was refused");
        auto const height = co_await client.run();
        // The flood's headers that fit are valid: they make the chain.
        require(height && *height == 99, "a flooding peer stopped the others: " + describe(client));
        require(client.peer_state(0) == "rejected" && client.peer_state(1) == "caught_up", "the flood: " + describe(client));
    }
    std::puts("a flooding peer rejected; the others went on");

    // A peer answering with known headers by turns makes no progress: it is
    // rejected once the others are done, and the run ends.
    {
        auto client_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18805"), "the alternating peer was refused");
        auto const height = co_await client.run();
        require(height && *height == 2500, "alternation: " + describe(client));
        require(client.peer_state(1) == "rejected" && client.peer_error(1) == "peer made no header progress",
            "the alternating peer: " + describe(client));
    }
    std::puts("a peer repeating known headers rejected");

    // A slow peer whose answers bring headers another peer delivered first
    // advances through them: it is not taken for one repeating them.
    {
        auto client_owned = make_client("ws://127.0.0.1:18780", regtest, 10'000);
        auto& client = *client_owned;
        require(client.add_peer("ws://127.0.0.1:18810"), "the slow peer was refused");
        auto const height = co_await client.run();
        require(height && *height == 2500, "slow peer: " + describe(client));
        require(client.peer_state(1) == "caught_up" && client.peer_error(1).empty() && client.peer_height(1) == 200,
            "the slow peer: " + describe(client));
    }
    std::puts("a slow peer through known headers kept");

    // A discovery answer that comes after the last headers still counts.
    {
        auto client_owned = make_client("ws://127.0.0.1:18806", regtest, 10'000);
        auto& client = *client_owned;
        require(client.configure_discovery(true, true, false), "discovery could not be configured");
        // A migration client still prefers the new announcement with a new peer.
        require(client.configure_legacy_discovery(true), "legacy configuration");
        auto const height = co_await client.run();
        require(height && *height == 2600 && client.peer_count() == 2, "the late discovery answer was lost: " + describe(client));
    }
    std::puts("a late discovery answer joined its peer");

    // A peer that closes while its discovery answer is awaited has still
    // completed its headers: the run succeeds.
    {
        auto client_owned = make_client("ws://127.0.0.1:18813", regtest, 10'000);
        auto& client = *client_owned;
        require(client.configure_discovery(true, true, false), "discovery could not be configured");
        auto const height = co_await client.run();
        require(height && *height == 2500, "a close during the discovery wait failed the run: " + describe(client));
    }
    std::puts("a close during the discovery wait kept the headers");

    // Discovery: the only configured peer advertises another, which joins and
    // brings the longer chain. Loopback endpoints need private ones allowed.
    {
        auto client_owned = make_client("ws://127.0.0.1:18789", regtest, 10'000);
        auto& client = *client_owned;
        require(client.configure_discovery(true, true, false), "discovery could not be configured");
        auto const height = co_await client.run();
        require(height && *height == 2600, "the discovered peer did not join: " + describe(client));
        require(client.peer_count() == 2 && client.peer_endpoint(1) == "ws://127.0.0.1:18785/" &&
            client.peer_source(1) == "ws://127.0.0.1:18789", "the discovered peer: " + describe(client));
    }
    {
        // Without private endpoints allowed, advertising one is invalid data:
        // the peer is rejected and nothing joins.
        auto client_owned = make_client("ws://127.0.0.1:18789", regtest, 10'000);
        auto& client = *client_owned;
        auto const height = co_await client.run();
        require(!height && client.peer_count() == 1 && client.peer_state(0) == "rejected",
            "a private endpoint was accepted: " + describe(client));
    }
    {
        // With discovery off, it is not negotiated.
        auto client_owned = make_client("ws://127.0.0.1:18789", regtest, 10'000);
        auto& client = *client_owned;
        require(client.configure_discovery(false, true, false), "discovery could not be disabled");
        auto const height = co_await client.run();
        require(height && *height == 2500 && client.peer_count() == 1, "discovery ran while disabled: " + describe(client));
    }
    // Old servers require the user-agent marker on both sides. It is never
    // used implicitly; synchronization itself remains backwards compatible.
    for (bool legacy : {false, true}) {
        auto client_owned = make_client("ws://127.0.0.1:18814", regtest, 10'000);
        auto& client = *client_owned;
        require(client.configure_discovery(true, true, false), "discovery configuration");
        require(client.configure_legacy_discovery(legacy), "legacy configuration");
        auto const height = co_await client.run();
        require(height && *height == (legacy ? 2600u : 2500u), "legacy interoperability: " + describe(client));
        require(client.peer_count() == (legacy ? 2u : 1u), "legacy admission: " + describe(client));
    }
    std::puts("legacy discovery required explicit opt-in");
    std::puts("discovered peer joined height=2600");
}

} // namespace

int main() {
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("PEERS FAILED: %s\n", e.what()); }
            catch (...) { std::puts("PEERS FAILED"); }
        } else {
            std::puts("peers control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
