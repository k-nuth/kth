// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// TEST-ONLY. Not installed, not compiled into any library.
//
// Real peer_session objects with no network, for the header controls: a peer
// whose identity is all that matters, and a way to run an io_context until
// nothing more can happen.
//
// Everything here is `inline` or lives in an anonymous namespace, so including
// it from two translation units is not an ODR problem.

#ifndef KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP
#define KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP

#include <test_helpers.hpp>

#include <cstdint>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>

namespace kth::test {
namespace {

using peer_ptr = network::peer_session::ptr;

// A peer over an unconnected socket. peer_session tolerates this: it resolves
// the authority defensively (any() when the endpoint is unavailable) and send()
// serializes onto an internal channel rather than the socket. Nothing in these
// controls needs the socket, so nothing connects one.
peer_ptr make_peer(::asio::io_context& ioc, network::settings const& config, uint64_t nonce) {
    ::asio::ip::tcp::socket socket(ioc);
    auto peer = std::make_shared<network::peer_session>(std::move(socket), config, false);
    peer->set_nonce(nonce);
    return peer;
}

// Run until nothing can make further progress without time passing. Every
// operation these controls start is ready or immediately-ready, so the queue
// draining IS the work having finished; a timer still pending is left pending.
void drain(::asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        // keep going while handlers are still becoming ready
    }
}

} // namespace
} // namespace kth::test

#endif // KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP
