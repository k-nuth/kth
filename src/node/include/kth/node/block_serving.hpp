// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_BLOCK_SERVING_HPP
#define KTH_NODE_BLOCK_SERVING_HPP

#include <cstddef>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>

// What answers a peer that syncs from this node: `getheaders` and the block
// entries of `getdata`. Only chain data the node holds and has not found
// invalid is offered, so a peer that follows a headers answer can download
// every block it names.

namespace kth::node {

// The headers after the first locator hash on the active chain (genesis when
// none is), through the stop hash when it comes later on that chain, at most
// `limit` of them, and none above the last validated block. With no locator
// hashes, the stop hash's header alone, as a peer asks for one header.
KND_API domain::message::headers answer_header_request(
    domain::message::get_headers const& request, blockchain::block_chain const& chain,
    size_t limit = max_get_headers);

// Whether a getdata entry asks for a block. A compact block is answered with
// the full block, which BIP152 allows. Entries of other kinds are left to
// whatever serves them.
KND_API bool is_block_request(domain::message::inventory_vector const& entry);

// The block to send for `hash`: one whose data the node holds and that is not
// known invalid or built on an invalid block, on the active chain or not.
// Null when there is none.
KND_API ::asio::awaitable<domain::message::block::const_ptr> servable_block(
    blockchain::block_chain const& chain, hash_digest const& hash);

} // namespace kth::node

#endif // KTH_NODE_BLOCK_SERVING_HPP
