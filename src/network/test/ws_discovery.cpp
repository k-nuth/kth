// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license. See COPYING or LICENSE.
#include <catch2/catch_test_macros.hpp>
#include <kth/network/ws_discovery.hpp>

#include <algorithm>
#include <set>

namespace wd = kth::network::ws_discovery;
namespace {
constexpr std::uint32_t now = 1700000000;
wd::endpoint public_endpoint(std::string host = "8.8.8.8", std::uint8_t transport = wd::wss) {
    return {transport, std::move(host), 8443, "/p2p", now + 600};
}
}

TEST_CASE("WebSocket discovery negotiation announces relay separately from listening", "[ws_discovery]") {
    CHECK_FALSE(wd::understands("/Knuth:1.0/"));
    CHECK_FALSE(wd::understands("/wsdisc:10/"));
    CHECK_FALSE(wd::understands("wsdisc:1"));
    CHECK_FALSE(wd::understands(std::string(256, 'a') + std::string(wd::capability)));
    auto agent = wd::advertise("/Knuth:1.0/");
    REQUIRE(agent);
    CHECK(wd::understands(*agent));
    CHECK(wd::advertise(*agent) == agent);
    CHECK_FALSE(wd::negotiated(*agent, "/Old:1/"));
    CHECK(wd::negotiated(*agent, "/Browser:1//wsdisc:1/"));
    CHECK_FALSE(wd::advertise(std::string(256, 'a')));
    CHECK_FALSE(wd::advertise(std::string(256, 'a') + std::string(wd::capability)));
    // Negotiation says nothing about listening. An empty store is legitimate.
    wd::endpoint_store store;
    CHECK(store.select(wd::all_transports, now).empty());
}

TEST_CASE("WebSocket endpoint syntax preserves all connection parameters", "[ws_discovery]") {
    auto endpoint = wd::from_url("wss://NODE.bitcoincash.org:8443/bch/p2p", now + 60, now);
    REQUIRE(endpoint);
    CHECK(endpoint->host == "node.bitcoincash.org");
    CHECK(endpoint->transport == wd::wss);
    CHECK(endpoint->port == 8443);
    CHECK(endpoint->path == "/bch/p2p");
    CHECK(wd::url(*endpoint) == "wss://node.bitcoincash.org:8443/bch/p2p");
    auto v6 = wd::from_url("ws://[2001:4860:4860::8888]:80/", now + 60, now);
    REQUIRE(v6);
    CHECK(wd::url(*v6) == "ws://[2001:4860:4860::8888]:80/");
    auto mapped = wd::from_url("wss://[::ffff:8.8.8.8]/", now + 60, now);
    REQUIRE(mapped);
    CHECK(mapped->host == "8.8.8.8");
    CHECK(wd::from_url("ws://8.8.8.8", now + 60, now)->port == 80);
    CHECK(wd::from_url("wss://8.8.8.8", now + 60, now)->port == 443);
}

TEST_CASE("WebSocket endpoint public policy rejects private and ambiguous endpoints", "[ws_discovery]") {
    for (auto const* url : {"ws://127.0.0.1/", "ws://10.1.2.3/", "ws://192.168.1.1/", "ws://172.16.0.1/",
        "ws://169.254.1.1/", "ws://100.64.1.1/", "ws://198.18.0.1/", "ws://192.0.2.1/", "ws://0.0.0.0/",
        "ws://224.0.0.1/", "ws://[::1]/", "ws://[fe80::1]/", "ws://[fc00::1]/", "ws://[2001:db8::1]/",
        "ws://[::ffff:127.0.0.1]/", "ws://[2002:7f00:1::1]/", "ws://localhost/", "ws://node.local/", "ws://node.home.arpa/"}) {
        INFO(url);
        CHECK_FALSE(wd::from_url(url, now + 60, now));
    }
    for (auto const* url : {"http://8.8.8.8/", "wss://user@8.8.8.8/", "ws://8.8.8.8:0/", "ws://8.8.8.8:65536/",
        "ws://8.8.8.8:/", "ws://8.8.8.8:80evil/", "ws://8.8.8.8/p?q=a", "ws://8.8.8.8/#fragment",
        "ws://8.8.8.8/<bad>", "ws://8.8.8.8/[bad]", "ws://8.8.8.8//p2p", "ws://8.8.8.8/../p2p", "ws://8.8.8.8/%2e%2e/p2p", "ws://8.8.8.8/%00",
        "ws://8.8.8.8/p\r\nheader", "ws://[fe80::1%25en0]/", "ws://a..b.org/", "ws://-bad.org/", "ws://node.org./"}) {
        INFO(url);
        CHECK_FALSE(wd::from_url(url, now + 60, now));
    }
    auto lan = wd::from_url("ws://127.0.0.1:8080/", now + 60, now, true);
    REQUIRE(lan);
    CHECK_FALSE(wd::validate(*lan, now));
    CHECK_FALSE(wd::from_url("ws://0.0.0.0/", now + 60, now, true));
    CHECK_FALSE(wd::validate(public_endpoint(), now + 600));
    auto too_far = public_endpoint();
    too_far.expires = now + wd::maximum_ttl + 1;
    CHECK_FALSE(wd::validate(too_far, now));
}

TEST_CASE("WebSocket discovery wire layouts are bounded and strict", "[ws_discovery]") {
    wd::request request{wd::wss, 0x0102030405060708};
    auto bytes = wd::encode(request);
    REQUIRE(bytes);
    CHECK(*bytes == std::vector<std::uint8_t>{1, 2, 8, 7, 6, 5, 4, 3, 2, 1});
    CHECK(wd::decode_request(*bytes) == request);
    bytes->push_back(0);
    CHECK_FALSE(wd::decode_request(*bytes));
    CHECK_FALSE(wd::encode(wd::request{4, 1}));
    CHECK_FALSE(wd::encode(wd::request{wd::ws, 0}));
    wd::response response{request.id, {public_endpoint(), public_endpoint("1.1.1.1", wd::ws)}};
    auto payload = wd::encode(response, now);
    REQUIRE(payload);
    CHECK(wd::decode_response(*payload, now) == response);
    for (std::size_t length = 0; length < payload->size(); ++length) {
        CHECK_FALSE(wd::decode_response(std::span(*payload).first(length), now));
    }
    payload->push_back(0);
    CHECK_FALSE(wd::decode_response(*payload, now));
    auto duplicate = response;
    duplicate.endpoints.push_back(response.endpoints[0]);
    CHECK_FALSE(wd::encode(duplicate, now));
    response.endpoints.resize(wd::maximum_endpoints + 1, public_endpoint());
    CHECK_FALSE(wd::encode(response, now));
    CHECK_FALSE(wd::decode_response(std::vector<std::uint8_t>(wd::maximum_payload + 1), now));
    // Empty replies are valid for browser clients that have no incoming listener.
    auto empty = wd::encode(wd::response{5, {}}, now);
    REQUIRE(empty);
    CHECK(empty->size() == 10);
    CHECK(wd::decode_response(*empty, now)->endpoints.empty());
}

TEST_CASE("WebSocket discovery session refuses unnegotiated unsolicited and excessive traffic", "[ws_discovery]") {
    wd::request request{wd::wss, 9};
    wd::response response{9, {public_endpoint()}};
    wd::session_policy old_peer(false);
    CHECK_FALSE(old_peer.accept_request(request, now));
    CHECK_FALSE(old_peer.begin_request(request, now));
    CHECK_FALSE(old_peer.accept_response(response, now));
    wd::session_policy peer(true);
    CHECK_FALSE(peer.accept_response(response, now));
    REQUIRE(peer.begin_request(request, now));
    CHECK_FALSE(peer.begin_request(request, now));
    auto wrong_id = response;
    wrong_id.id = 10;
    CHECK_FALSE(peer.accept_response(wrong_id, now));
    auto wrong_transport = response;
    wrong_transport.endpoints[0].transport = wd::ws;
    CHECK_FALSE(peer.accept_response(wrong_transport, now));
    REQUIRE(peer.accept_response(response, now + 1));
    CHECK_FALSE(peer.accept_response(response, now + 1));
    CHECK_FALSE(peer.begin_request(request, now + 59));
    REQUIRE(peer.begin_request(request, now + 60));
    CHECK_FALSE(peer.accept_response(response, now + 90));
    REQUIRE(peer.accept_request(request, now));
    CHECK_FALSE(peer.accept_request(request, now + 59));
    CHECK(peer.accept_request(request, now + 60));
}

TEST_CASE("WebSocket discovery store verifies public addresses before relay and never invents listeners", "[ws_discovery]") {
    wd::endpoint_store store;
    auto numeric = public_endpoint();
    auto dns = public_endpoint("node.bitcoincash.org");
    REQUIRE(store.ingest(numeric, now));
    REQUIRE(store.ingest(dns, now));
    CHECK(store.size() == 2);
    CHECK(store.select(wd::all_transports, now) == std::vector<wd::endpoint>{numeric});
    REQUIRE(store.verify(dns, wd::verification::public_address_verified, now));
    CHECK(store.select(wd::wss, now).size() == 2);
    auto duplicate = numeric;
    duplicate.expires += 100;
    REQUIRE(store.ingest(duplicate, now));
    CHECK(store.size() == 2);
    REQUIRE(store.lookup(duplicate, now));
    CHECK(store.lookup(duplicate, now)->expires == numeric.expires);
    CHECK(store.select(wd::ws, now).empty());
    CHECK(store.select(wd::all_transports, now + 600).empty());
    CHECK(store.size() == 0);
    CHECK_FALSE(store.verify(dns, wd::verification::public_address_verified, now + 600));
    auto renewed = dns;
    renewed.expires = now + 1200;
    REQUIRE(store.ingest(renewed, now + 600));
    CHECK_FALSE(store.verify(dns, wd::verification::public_address_verified, now + 600));
    CHECK(store.select(wd::wss, now + 600).empty());
    REQUIRE(store.verify(renewed, wd::verification::public_address_verified, now + 600));
    CHECK(store.select(wd::wss, now + 600).size() == 1);

    wd::endpoint_store lan(true);
    auto local = public_endpoint("127.0.0.1", wd::ws);
    REQUIRE(lan.ingest(local, now));
    CHECK(lan.select(wd::all_transports, now).empty());
    CHECK_FALSE(lan.verify(local, wd::verification::public_address_verified, now));
    REQUIRE(lan.verify(local, wd::verification::explicit_lan, now));
    CHECK(lan.select(wd::ws, now) == std::vector<wd::endpoint>{local});
}

TEST_CASE("WebSocket discovery store capacity and reply rotation are bounded", "[ws_discovery]") {
    wd::endpoint_store store;
    for (std::size_t i = 0; i < wd::maximum_store; ++i) {
        auto item = public_endpoint("8.8.8.8");
        item.path = "/p2p/" + std::to_string(i);
        REQUIRE(store.ingest(item, now));
    }
    CHECK(store.size() == wd::maximum_store);
    auto overflow = public_endpoint("1.1.1.1");
    CHECK_FALSE(store.ingest(overflow, now));
    auto first = store.select(wd::all_transports, now);
    auto second = store.select(wd::all_transports, now);
    REQUIRE(first.size() == wd::maximum_endpoints);
    REQUIRE(second.size() == wd::maximum_endpoints);
    CHECK(first[0].path != second[0].path);
    std::set<std::string> urls;
    for (auto const& item : first) urls.insert(wd::url(item));
    for (auto const& item : second) urls.insert(wd::url(item));
    CHECK(urls.size() == wd::maximum_endpoints * 2);
    overflow.expires = now + 1200;
    CHECK(store.ingest(overflow, now + 600));
    CHECK(store.size() == 1);
}

TEST_CASE("WebSocket discovery responses drop run-out leases and cut long ones", "[ws_discovery]") {
    // An honest sender's clock differs from ours: a record that ran out in
    // transit is dropped, a lease past our maximum is cut to it, and the rest
    // of the response is kept.
    auto soon = public_endpoint("8.8.8.8");
    auto later = public_endpoint("1.1.1.1");
    later.expires = now + 1200;
    auto payload = wd::encode(wd::response{7, {soon, later}}, now);
    REQUIRE(payload);
    auto decoded = wd::decode_response(*payload, now + 900);
    REQUIRE(decoded);
    REQUIRE(decoded->endpoints.size() == 1);
    CHECK(decoded->endpoints[0].host == "1.1.1.1");

    auto lease = public_endpoint();
    lease.expires = now + wd::maximum_ttl;
    payload = wd::encode(wd::response{8, {lease}}, now);
    REQUIRE(payload);
    decoded = wd::decode_response(*payload, now - 10);  // our clock behind theirs
    REQUIRE(decoded);
    REQUIRE(decoded->endpoints.size() == 1);
    CHECK(decoded->endpoints[0].expires == now - 10 + wd::maximum_ttl);

    // A record that ran out is still checked: one with a private address
    // (refused outside LAN mode) fails the response even after its lease.
    auto lan = public_endpoint("192.168.1.10");
    payload = wd::encode(wd::response{9, {lan}}, now, true);
    REQUIRE(payload);
    CHECK(wd::decode_response(*payload, now, true));
    CHECK_FALSE(wd::decode_response(*payload, now));
    CHECK_FALSE(wd::decode_response(*payload, now + 900));
}

TEST_CASE("WebSocket discovery store makes room for new gossip and the node's own endpoints", "[ws_discovery]") {
    // Full of unverified gossip (names, not yet resolved): the oldest makes room.
    wd::endpoint_store candidates;
    for (std::size_t i = 0; i < wd::maximum_store; ++i) {
        auto item = public_endpoint("node" + std::to_string(i) + ".example.org");
        REQUIRE(candidates.ingest(item, now));
    }
    auto newest = public_endpoint("newest.example.org");
    CHECK(candidates.ingest(newest, now));
    CHECK(candidates.size() == wd::maximum_store);
    CHECK(candidates.lookup(newest, now));
    CHECK_FALSE(candidates.lookup(public_endpoint("node0.example.org"), now));

    // Full of verified gossip: more gossip is refused, the node's own endpoint
    // is not, and it is never evicted.
    wd::endpoint_store verified;
    for (std::size_t i = 0; i < wd::maximum_store; ++i) {
        auto item = public_endpoint("8.8.8.8");
        item.path = "/p2p/" + std::to_string(i);
        REQUIRE(verified.ingest(item, now));
    }
    auto own = public_endpoint("9.9.9.9");
    CHECK_FALSE(verified.ingest(public_endpoint("1.1.1.1"), now));
    CHECK(verified.ingest_own(own, now));
    CHECK(verified.size() == wd::maximum_store);
    for (std::size_t i = 0; i < wd::maximum_store; ++i) {
        auto item = public_endpoint("8.8.4.4");
        item.path = "/p2p/" + std::to_string(i);
        (void)verified.ingest_own(item, now);
    }
    CHECK(verified.lookup(own, now));
}
