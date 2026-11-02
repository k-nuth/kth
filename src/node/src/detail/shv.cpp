// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/shv.hpp>

#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/message/get_shv.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::node::detail {

shv_reply process_get_shv(blockchain::block_chain const& chain, byte_span payload, uint32_t version) {
    if ( ! chain.shv_enabled()) {
        return {shv_action::ignored, {}};
    }

    byte_reader count_reader(payload);
    auto const count = count_reader.read_size_little_endian();
    if ( ! count) {
        return {shv_action::malformed, {}};
    }
    // Score an oversized batch before its parser can allocate request storage.
    if (*count > domain::message::max_shv_requests) {
        return {shv_action::oversized, {}};
    }

    byte_reader reader(payload);
    auto const request = domain::message::get_shv::from_data(reader, version);
    if ( ! request) {
        return {shv_action::malformed, {}};
    }

    domain::message::shv response;
    response.responses.reserve(request->requests.size());
    for (auto const& entry : request->requests) {
        if ( ! entry.is_valid()) {
            continue;
        }
        auto proof = entry.is_by_hash()
            ? chain.shv_proof(entry.block_height_or_hash, entry.commitment_height, entry.is_proof_to_root())
            : chain.shv_proof(entry.block_height(), entry.commitment_height, entry.is_proof_to_root());
        if ( ! proof) {
            continue;
        }
        response.responses.push_back({entry.type, entry.commitment_height, proof->block_height,
            std::move(proof->header), std::move(proof->proof), proof->target});
    }
    return {shv_action::reply, std::move(response)};
}

message_handler_fn make_get_shv_handler(blockchain::block_chain const& chain, p2p_node& p2p) {
    return [&chain, &p2p](network::peer_session& peer, network::raw_message const& raw)
        -> ::asio::awaitable<message_result> {
        auto result = process_get_shv(chain, raw.payload, peer.negotiated_version());
        if (result.action == shv_action::oversized) {
            if (p2p.report_misbehavior(peer.authority(), 20, "oversized-getshv")) {
                co_return message_result::disconnect;
            }
        } else if (result.action == shv_action::reply) {
            co_await peer.send(result.message);
        }
        co_return message_result::handled;
    };
}

} // namespace kth::node::detail
