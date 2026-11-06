// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <thread>
#include <utility>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/sync/header_tasks.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::node::sync;

namespace {

std::pair<::asio::ip::tcp::socket, ::asio::ip::tcp::socket>
connected_sockets(::asio::io_context& ctx) {
    ::asio::ip::tcp::acceptor acceptor(ctx, {::asio::ip::tcp::v4(), 0});
    auto const port = acceptor.local_endpoint().port();
    ::asio::ip::tcp::socket client(ctx);
    ::asio::ip::tcp::socket server(ctx);
    std::thread accept_thread([&] { server = acceptor.accept(); });
    client.connect({::asio::ip::address_v4::loopback(), port});
    accept_thread.join();
    return {std::move(client), std::move(server)};
}

} // namespace

// GCC 16 at -O1 and above never destroys the object behind a by-value
// structured binding that is still in scope at a later suspension: on every
// turn of a receive loop the previous message stays alive in the frame. The
// header validation task binds each batch and then awaits its result send, so
// with that binding it kept the batch, and the peer the batch names, after
// the batch was done.
TEST_CASE("header validation releases a batch before waiting for the next",
          "[header_validation][lifetime]") {
    test::chain_fixture fixture("header_validation_releases_batch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    ::asio::io_context ctx;
    auto sockets = connected_sockets(ctx);
    network::settings settings(domain::config::network::regtest);
    auto peer = std::make_shared<network::peer_session>(std::move(sockets.second), settings);

    header_validation_input_channel input(ctx, 4);
    header_validated_channel output(ctx, 4);
    ::asio::co_spawn(ctx, header_validation_task(fixture.organizer(), input, output), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, downloaded_headers{.headers = {}, .source_peer = peer}));
    ctx.poll();

    bool validated = false;
    REQUIRE(output.try_receive([&](std::error_code, headers_validated result) {
        validated = result.source_peer == peer;
    }));
    CHECK(validated);
    ctx.poll();

    // The task is waiting for the next batch: nothing it holds names the peer.
    CHECK(peer.use_count() == 1);

    input.close();
    ctx.run();
}
