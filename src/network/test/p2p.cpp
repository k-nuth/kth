// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdio>
#include <filesystem>
#include <future>
#include <iostream>

#include <test_helpers.hpp>

#include <kth/network.hpp>

using namespace kth;
using namespace kd::message;
using namespace kth::network;

#define TEST_SET_NAME \
    "p2p_tests"

#define TEST_NAME \
    Catch::getResultCapture().getCurrentTestName()

// TODO: build mock and/or use dedicated test service.
#define SEED1 "testnet-seed.bitcoin.petertodd.org:18333"
#define SEED2 "testnet-seed.bitcoin.schildbach.de:18333"

// NOTE: this is insufficient as the address varies.
#define SEED1_AUTHORITIES \
    { \
      { "52.8.185.53:18333" }, \
      { "178.21.118.174:18333" }, \
      { "[2604:880:d:2f::c7b2]:18333" }, \
      { "[2604:a880:1:20::269:b001]:18333" }, \
      { "[2602:ffea:1001:6ff::f922]:18333" }, \
      { "[2401:2500:203:9:153:120:11:18]:18333" }, \
      { "[2600:3c00::f03c:91ff:fe89:305f]:18333" }, \
      { "[2600:3c01::f03c:91ff:fe98:68bb]:18333" } \
    }

#define SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(name) \
    auto name = network::settings(domain::config::network::testnet); \
    name.threads = 1; \
    name.outbound_connections = 0; \
    name.manual_attempt_limit = 2

#define SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(name) \
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(name); \
    name.host_pool_capacity = 42; \
    name.seeds = { { SEED1 } }; \
    name.hosts_file = get_log_path(TEST_NAME, "hosts")

#define SETTINGS_TESTNET_THREE_THREADS_ONE_SEED_FIVE_OUTBOUND(name) \
    auto name = network::settings(domain::config::network::testnet); \
    name.threads = 3; \
    name.host_pool_capacity = 42; \
    name.outbound_connections = 5; \
    name.seeds = { { SEED1 } }; \
    name.hosts_file = get_log_path(TEST_NAME, "hosts")

std::string get_log_path(std::string const& test, std::string const& file) {
    auto const path = test + "." + file + ".log";
    std::filesystem::remove_all(path);
    return path;
}

static
void print_headers(std::string const& test) {
    spdlog::info("[test] =========== {} ==========", test);
}

static
int start_result(p2p& network) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec) {
        promise.set_value(ec);
    };
    network.start(handler);
    return promise.get_future().get().value();
}

static
int connect_result(p2p& network, infrastructure::config::endpoint const& host) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec, channel::ptr) {
        promise.set_value(ec);
    };
    network.connect(host.host(), host.port(), handler);
    return promise.get_future().get().value();
}

static
int run_result(p2p& network) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec) {
        promise.set_value(ec);
    };
    network.run(handler);
    return promise.get_future().get().value();
}

static
int subscribe_result(p2p& network) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec, channel::ptr) {
        promise.set_value(ec);
        return false;
    };
    network.subscribe_connection(handler);
    return promise.get_future().get().value();
}

static
int subscribe_connect1_result(p2p& network, infrastructure::config::endpoint const& host) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec, channel::ptr) {
        promise.set_value(ec);
        return false;
    };
    network.subscribe_connection(handler);
    network.connect(host.host(), host.port());
    return promise.get_future().get().value();
}

static
int subscribe_connect2_result(p2p& network, infrastructure::config::endpoint const& host) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec, channel::ptr) {
        promise.set_value(ec);
        return false;
    };
    network.subscribe_connection(handler);
    network.connect(host);
    return promise.get_future().get().value();
}

template<typename Message>
static
int send_result(Message const& message, p2p& network, int channels) {
    auto const channel_counter = [&channels](code ec, channel::ptr channel) {
        REQUIRE(ec == error::success);
        --channels;
    };

    std::promise<code> promise;
    auto const completion_handler = [&promise](code ec) {
        promise.set_value(ec);
    };

    network.broadcast(message, channel_counter, completion_handler);
    auto const result = promise.get_future().get().value();

    REQUIRE(channels == 0);
    return result;
}

// Trivial tests just validate static inits (required because p2p tests disabled in travis).
// Start Test Suite: empty tests

TEST_CASE("empty test", "[empty tests]") {
    REQUIRE(true);
}

// End Test Suite

// Start Test Suite: p2p tests

TEST_CASE("p2p top block default zero null hash", "[p2p tests]") {
    print_headers(TEST_NAME);
    network::settings const configuration;
    p2p network(configuration);
    REQUIRE(network.top_block().height() == 0);
    REQUIRE(network.top_block().hash() == null_hash);
}

TEST_CASE("p2p set top block1 values expected", "[p2p tests]") {
    print_headers(TEST_NAME);
    network::settings const configuration;
    p2p network(configuration);
    size_t const expected_height = 42;
    auto const expected_hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    network.set_top_block({ expected_hash, expected_height });
    REQUIRE(network.top_block().hash() == expected_hash);
    REQUIRE(network.top_block().height() == expected_height);
}

TEST_CASE("p2p set top block2 values expected", "[p2p tests]") {
    print_headers(TEST_NAME);
    network::settings const configuration;
    p2p network(configuration);
    size_t const expected_height = 42;
    auto const hash = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash;
    infrastructure::config::checkpoint const expected{ hash, expected_height };
    network.set_top_block(expected);
    REQUIRE(network.top_block().hash() == expected.hash());
    REQUIRE(network.top_block().height() == expected.height());
}

TEST_CASE("p2p start no sessions start success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    REQUIRE(start_result(network) == error::success);
}

TEST_CASE("p2p start no connections start stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(network.stop());
}

TEST_CASE("p2p start no sessions start success start operation fail", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(start_result(network) == error::operation_failed);
}

////TEST_CASE("p2p  start  seed session  start stop start success", "[p2p tests]")
////{
////    print_headers(TEST_NAME);
////    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
////    p2p network(configuration);
////    REQUIRE(start_result(network) == error::success);
////    REQUIRE(network.stop());
////    REQUIRE(start_result(network) == error::success);
////}

TEST_CASE("p2p start seed session handshake timeout start peer throttling stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
    configuration.channel_handshake_seconds = 0;
    p2p network(configuration);

    // The (timeout) error on the individual connection is ignored.
    // The connection failure results in a failure to generate any addresses.
    // The failure to generate an increase of 100 addresses produces error::peer_throttling.
    REQUIRE(start_result(network) == error::peer_throttling);

    // The service never started but stop will still succeed (and is optional).
    REQUIRE(network.stop());
}

TEST_CASE("p2p start seed session connect timeout start peer throttling stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
    configuration.connect_timeout_seconds = 0;
    p2p network(configuration);
    REQUIRE(start_result(network) == error::peer_throttling);
    REQUIRE(network.stop());
}

TEST_CASE("p2p start seed session germination timeout start peer throttling stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
    configuration.channel_germination_seconds = 0;
    p2p network(configuration);
    REQUIRE(start_result(network) == error::peer_throttling);
    REQUIRE(network.stop());
}

TEST_CASE("p2p start seed session inactivity timeout start peer throttling stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
    configuration.channel_inactivity_minutes = 0;
    p2p network(configuration);
    REQUIRE(start_result(network) == error::peer_throttling);
    REQUIRE(network.stop());
}

TEST_CASE("p2p start seed session expiration timeout start peer throttling stop success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_ONE_SEED(configuration);
    configuration.channel_expiration_minutes = 0;
    p2p network(configuration);
    REQUIRE(start_result(network) == error::peer_throttling);
    REQUIRE(network.stop());
}

// Disabled for live test reliability.
// This may fail due to missing blacklist entries for the specified host.
////TEST_CASE("p2p  start  seed session blacklisted  start operation fail stop success", "[p2p tests]")
////{
////    print_headers(TEST_NAME);
////    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
////    configuration.host_pool_capacity = 42;
////    configuration.hosts_file = get_log_path(TEST_NAME, "hosts");
////    configuration.seeds = { { SEED1 } };
////    configuration.blacklist = SEED1_AUTHORITIES;
////    p2p network(configuration);
////    REQUIRE(start_result(network) == error::operation_failed);
////    REQUIRE(network.stop());
////}

TEST_CASE("p2p start outbound no seeds success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    configuration.outbound_connections = 1;
    p2p network(configuration);
    REQUIRE(start_result(network) == error::success);
}

TEST_CASE("p2p connect not started service stopped", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    infrastructure::config::endpoint const host(SEED1);
    REQUIRE(connect_result(network, host) == error::service_stopped);
}

TEST_CASE("p2p connect started success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    infrastructure::config::endpoint const host(SEED1);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(run_result(network) == error::success);
    REQUIRE(connect_result(network, host) == error::success);
}

// Disabled for live test reliability.
// This may fail due to connecting to the same host on different addresses.
////TEST_CASE("p2p  connect  twice  address in use", "[p2p tests]")
////{
////    print_headers(TEST_NAME);
////    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
////    p2p network(configuration);
////    infrastructure::config::endpoint const host(SEED1);
////    REQUIRE(start_result(network) == error::success);
////    REQUIRE(run_result(network) == error::success);
////    REQUIRE(connect_result(network, host) == error::success);
////    REQUIRE(connect_result(network, host) == error::address_in_use);
////}

TEST_CASE("p2p subscribe stopped service stopped", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);

    // Expect immediate return because service is not started.
    REQUIRE(subscribe_result(network) == error::service_stopped);
}

TEST_CASE("p2p subscribe started stop service stopped", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    REQUIRE(start_result(network) == error::success);

    std::promise<code> promise;
    auto const handler = [](code ec, channel::ptr channel) {
        REQUIRE( ! channel);
        REQUIRE(ec == error::service_stopped);
        return false;
    };

    // Expect queued handler until destruct because service is started.
    network.subscribe_connection(handler);
}

TEST_CASE("p2p subscribe started connect1 success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    infrastructure::config::endpoint const host(SEED1);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(subscribe_connect1_result(network, host) == error::success);
}

TEST_CASE("p2p subscribe started connect2 success", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    infrastructure::config::endpoint const host(SEED1);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(subscribe_connect2_result(network, host) == error::success);
}

TEST_CASE("p2p broadcast ping two distinct hosts two sends and successful completion", "[p2p tests]") {
    print_headers(TEST_NAME);
    SETTINGS_TESTNET_ONE_THREAD_NO_CONNECTIONS(configuration);
    p2p network(configuration);
    infrastructure::config::endpoint const host1(SEED1);
    infrastructure::config::endpoint const host2(SEED2);
    REQUIRE(start_result(network) == error::success);
    REQUIRE(run_result(network) == error::success);
    REQUIRE(connect_result(network, host1) == error::success);
    REQUIRE(connect_result(network, host2) == error::success);
    REQUIRE(send_result(ping(0), network, 2) == error::success);
}

////TEST_CASE("p2p  subscribe  seed outbound  success", "[p2p tests]")
////{
////    print_headers(TEST_NAME);
////    SETTINGS_TESTNET_THREE_THREADS_ONE_SEED_FIVE_OUTBOUND(configuration);
////    p2p network(configuration);
////    REQUIRE(start_result(network) == error::success);
////
////    std::promise<code> subscribe;
////    auto const subscribe_handler = [&subscribe, &network](code ec, channel::ptr)
////    {
////        // Fires on first connection.
////        subscribe.set_value(ec);
////        return false;
////    };
////    network.subscribe_connection(subscribe_handler);
////
////    std::promise<code> run;
////    auto const run_handler = [&run, &network](code ec)
////    {
////        // Fires once the session is started.
////        run.set_value(ec);
////    };
////    network.run(run_handler);
////
////    REQUIRE(run.get_future().get().value() == error::success);
////    REQUIRE(subscribe.get_future().get().value() == error::success);
////
////    // ~network blocks on stopping all channels.
////    // during channel.stop each channel removes itself from the collection.
////}

// End Test Suite
