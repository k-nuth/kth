// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license. See COPYING or LICENSE.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <kth/network.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/write.hpp>

namespace {
using namespace kth;
using namespace kth::network;
using namespace std::chrono_literals;

struct exchange_result {
    std::expected<handshake_result, code> result = std::unexpected(error::unknown);
    std::vector<raw_message> sent;
};

data_chunk packet(std::string command, data_chunk const& payload, uint32_t magic) {
    domain::message::heading head(magic, std::move(command), uint32_t(payload.size()), bitcoin_checksum(payload));
    auto result = head.to_data();
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

// The remote writes its finite transcript before the local coroutine starts.
// Both sockets and all I/O tasks are owned and joined by this scope.
exchange_result exchange(bool direct, bool discovery, std::vector<std::pair<std::string, data_chunk>> const& messages, bool legacy = false, bool header_only = false) {
    ::asio::io_context context;
    ::asio::ip::tcp::acceptor acceptor(context, {::asio::ip::tcp::v4(), 0});
    ::asio::ip::tcp::socket remote(context);
    remote.connect(acceptor.local_endpoint());
    settings config(domain::config::network::regtest);
    config.user_agent = "/KnuthTest:1/";
    config.ws_discovery = discovery;
    config.ws_discovery_legacy_agent = legacy;
    config.validate_checksum = true;
    auto peer = std::make_shared<peer_session>(acceptor.accept(), config);
    auto handshake = make_handshake_config(config, 0, 1234);
    handshake.timeout = 2s;
    for (auto const& [command, payload] : messages) {
        auto bytes = packet(command, payload, config.identifier);
        if (header_only && command == "sendwsaddr") bytes.resize(24);
        ::asio::write(remote, ::asio::buffer(bytes));
    }
    exchange_result output;
    std::exception_ptr failure;
    auto negotiate = [&]() -> ::asio::awaitable<void> {
        // GCC 16.2 evaluates both awaited arms of a conditional returning
        // std::expected. These paths have mutually exclusive socket readers:
        // the direct handshake must not execute alongside peer->run().
        if (direct) {
            output.result = co_await perform_handshake_direct(*peer, handshake);
        } else {
            output.result = co_await perform_handshake(*peer, handshake);
        }
        if (!output.result) peer->stop();
    };
    auto capture = [&]() -> ::asio::awaitable<void> {
        for (;;) {
            std::array<uint8_t, 24> bytes;
            auto [ec, count] = co_await ::asio::async_read(remote, ::asio::buffer(bytes), ::asio::as_tuple(::asio::use_awaitable));
            (void)count;
            if (ec == ::asio::error::eof) co_return;
            if (ec) throw std::system_error(ec);
            byte_reader reader(bytes);
            auto head = domain::message::heading::from_data(reader, 0);
            if (!head || head->payload_size() > 1024) throw std::runtime_error("invalid test frame heading");
            data_chunk payload(head->payload_size());
            if (!payload.empty()) co_await ::asio::async_read(remote, ::asio::buffer(payload), ::asio::use_awaitable);
            if (bitcoin_checksum(payload) != head->checksum()) throw std::runtime_error("invalid test frame checksum");
            output.sent.push_back({*head, std::move(payload)});
            if (head->command() == "verack") co_return;
        }
    };
    auto run = [&]() -> ::asio::awaitable<void> {
        task_group tasks("negotiation test", co_await ::asio::this_coro::executor);
        if (!direct) tasks.spawn("peer I/O", peer->run());
        try {
            using namespace ::asio::experimental::awaitable_operators;
            co_await (negotiate() && capture());
        } catch (...) {
            failure = std::current_exception();
        }
        peer->stop();
        co_await tasks.join();
    };
    ::asio::co_spawn(context, run(), [&](std::exception_ptr error) { if (error) failure = error; peer->stop(); });
    context.run();
    if (failure) std::rethrow_exception(failure);
    return output;
}

auto transcript(data_chunk announcement = {1}, std::string agent = "/Other:1/") {
    domain::message::version version;
    version.set_value(70016);
    version.set_timestamp(uint64_t(zulu_time()));
    version.set_user_agent(std::move(agent));
    version.set_nonce(4567);
    return std::vector<std::pair<std::string, data_chunk>>{
        {"version", to_data_chunk(version, 70016)}, {"sendwsaddr", std::move(announcement)}, {"verack", {}}};
}
}

TEST_CASE("discovery handshake keeps the user agent informational", "[ws_negotiation]") {
    settings config;
    config.ws_discovery = true;
    config.user_agent = "/KnuthTest:1/";
    CHECK(make_handshake_config(config, 0, 1234).user_agent == config.user_agent);
}

TEST_CASE("discovery announces its version before verack on both handshake paths", "[ws_negotiation]") {
    auto const direct = GENERATE(false, true);
    CAPTURE(direct);
    auto output = exchange(direct, true, transcript());
    REQUIRE(output.result);
    CHECK(output.result->ws_discovery);
    auto announcement = std::find_if(output.sent.begin(), output.sent.end(), [](auto const& m) { return m.heading.command() == "sendwsaddr"; });
    auto verack = std::find_if(output.sent.begin(), output.sent.end(), [](auto const& m) { return m.heading.command() == "verack"; });
    REQUIRE(announcement != output.sent.end());
    CHECK(announcement->payload == data_chunk{1});
    REQUIRE(verack != output.sent.end());
    CHECK(announcement < verack);
}

TEST_CASE("discovery refuses malformed or repeated handshake announcements", "[ws_negotiation]") {
    auto const direct = GENERATE(false, true);
    CAPTURE(direct);
    auto messages = transcript();
    SECTION("empty") { messages[1].second.clear(); }
    SECTION("trailing data") { messages[1].second = {1, 0}; }
    SECTION("duplicate") { messages.insert(messages.begin() + 2, messages[1]); }
    SECTION("before version") { std::swap(messages[0], messages[1]); }
    CHECK_FALSE(exchange(direct, true, messages).result);
}

TEST_CASE("discovery negotiation is explicit and versioned", "[ws_negotiation]") {
    auto const direct = GENERATE(false, true);
    CAPTURE(direct);
    SECTION("disabled locally") {
        auto output = exchange(direct, false, transcript());
        REQUIRE(output.result);
        CHECK_FALSE(output.result->ws_discovery);
        CHECK(std::none_of(output.sent.begin(), output.sent.end(), [](auto const& m) { return m.heading.command() == "sendwsaddr"; }));
    }
    SECTION("unknown version does not negotiate") {
        auto output = exchange(direct, true, transcript({2}));
        REQUIRE(output.result);
        CHECK_FALSE(output.result->ws_discovery);
    }
    SECTION("unknown version does not downgrade through a legacy marker") {
        auto output = exchange(direct, true, transcript({2}, "/Old:1//wsdisc:1/"), true);
        REQUIRE(output.result);
        CHECK_FALSE(output.result->ws_discovery);
    }
    SECTION("an announcement after verack does not negotiate") {
        auto messages = transcript();
        std::swap(messages[1], messages[2]);
        auto output = exchange(direct, true, messages);
        REQUIRE(output.result);
        CHECK_FALSE(output.result->ws_discovery);
    }
    SECTION("the legacy marker is not implicitly trusted") {
        auto messages = transcript({1}, "/Old:1//wsdisc:1/");
        messages.erase(messages.begin() + 1);
        auto output = exchange(direct, true, messages);
        REQUIRE(output.result);
        CHECK_FALSE(output.result->ws_discovery);
    }
    SECTION("legacy mode explicitly advertises and negotiates the old protocol") {
        auto messages = transcript({1}, "/Old:1//wsdisc:1/");
        messages.erase(messages.begin() + 1);
        auto output = exchange(direct, true, messages, true);
        REQUIRE(output.result);
        CHECK(output.result->ws_discovery);
        auto version = std::find_if(output.sent.begin(), output.sent.end(), [](auto const& m) { return m.heading.command() == "version"; });
        REQUIRE(version != output.sent.end());
        kth::byte_reader reader(version->payload);
        auto decoded = kth::domain::message::version::from_data(reader, 70016);
        REQUIRE(decoded);
        CHECK(decoded->user_agent() == "/KnuthTest:1/wsdisc:1/");
    }
}

TEST_CASE("discovery handshake bounds the payload before reading its body", "[ws_negotiation]") {
    auto const direct = GENERATE(false, true);
    CAPTURE(direct);
    auto messages = transcript({1, 2});
    messages.pop_back();
    auto output = exchange(direct, true, messages, false, true);
    REQUIRE_FALSE(output.result);
    CHECK(output.result.error() == (direct ? kth::error::bad_stream : kth::error::channel_stopped));
}
