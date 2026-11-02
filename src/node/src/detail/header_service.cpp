// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#include <kth/node/header_service.hpp>

namespace kth::node {
::asio::awaitable<code> serve_headers(blockchain::block_chain const& chain,
    network::peer_session& peer, domain::message::get_headers const& request, uint32_t magic) {
    if (request.start_hashes().size() > 101) co_return error::bad_stream;
    domain::message::header::list response;
    if (request.start_hashes().empty()) {
        // An empty locator asks for the one header named by stop_hash.
        auto height = chain.get_height(request.stop_hash());
        if (height) {
            auto header = chain.get_header(*height);
            if (header && domain::chain::hash(*header) == request.stop_hash()) response.push_back(*header);
        }
    } else {
        size_t begin = 1;
        hash_digest predecessor{};
        for (auto const& hash : request.start_hashes()) {
            auto height = chain.get_height(hash);
            if (!height) continue;
            auto header = chain.get_header(*height);
            if (!header || domain::chain::hash(*header) != hash) continue;
            begin = *height + 1;
            predecessor = hash;
            break;
        }
        // Unknown locators start after our genesis, as in the BCH protocol.
        if (predecessor == null_hash) {
            auto genesis = chain.get_header(0);
            if (!genesis) co_return error::not_found;
            predecessor = domain::chain::hash(*genesis);
        }
        for (size_t i = 0; i < max_get_headers; ++i) {
            auto header = chain.get_header(begin + i);
            if (!header) break;
            // A concurrent reorganization must not produce a mixed response.
            if (header->previous_block_hash() != predecessor) co_return error::operation_failed;
            predecessor = domain::chain::hash(*header);
            response.push_back(*header);
            if (predecessor == request.stop_hash()) break;
        }
    }
    auto message = domain::message::headers::create(std::move(response));
    if (!message) co_return error::bad_stream;
    auto bytes = domain::message::serialize(peer.negotiated_version(), *message,
        magic);
    if (!peer.try_send_raw(std::move(bytes))) {
        peer.stop(error::channel_stopped);
        co_return error::channel_stopped;
    }
    co_return error::success;
}
}
