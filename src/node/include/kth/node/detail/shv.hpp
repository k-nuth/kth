// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_SHV_HPP
#define KTH_NODE_DETAIL_SHV_HPP

#include <cstdint>

#include <kth/domain/message/shv.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/define.hpp>
#include <kth/node/p2p_node.hpp>

namespace kth::blockchain { struct block_chain; }

namespace kth::node::detail {

enum class shv_action { ignored, malformed, oversized, reply };

struct shv_reply {
    shv_action action;
    domain::message::shv message;
};

[[nodiscard]] KND_API shv_reply process_get_shv(blockchain::block_chain const& chain,
    byte_span payload, uint32_t version);

[[nodiscard]] KND_API message_handler_fn make_get_shv_handler(
    blockchain::block_chain const& chain, p2p_node& p2p);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_SHV_HPP
