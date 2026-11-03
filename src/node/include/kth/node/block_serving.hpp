// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_BLOCK_SERVING_HPP
#define KTH_NODE_BLOCK_SERVING_HPP

#include <cstddef>
#include <optional>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>

namespace kth::network {
class peer_session;
struct raw_message;
}

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

// Whether a getdata entry asks for a block. A compact-block entry is answered
// with a cmpctblock when one can be built, and with the full block when it
// cannot, which is what BIP152 requires. Entries of other kinds are left to
// whatever serves them.
KND_API bool is_block_request(domain::message::inventory_vector const& entry);

// cmpctblock for `block`, coinbase prefilled. Empty when the block has no
// transactions, so the caller sends the block itself.
[[nodiscard]] KND_API
std::optional<domain::message::compact_block> compact_block_reply(
    domain::message::block const& block);

// blocktxn for the absolute indexes in `request`, in that order. Empty when
// the hash differs or an index is past the block.
[[nodiscard]] KND_API
std::optional<domain::message::block_transactions> block_transactions_reply(
    domain::message::block const& block, domain::message::get_block_transactions const& request);

// The block to send for `hash`: one whose data the node holds and that is not
// known invalid or built on an invalid block, on the active chain or not.
// Null when there is none.
KND_API ::asio::awaitable<domain::message::block::const_ptr> servable_block(
    blockchain::block_chain const& chain, hash_digest const& hash);

// BIP152 relays only blocks on the validated active chain.
[[nodiscard]] KND_API
::asio::awaitable<std::optional<domain::message::compact_block>> servable_compact_block(
    blockchain::block_chain const& chain, hash_digest const& hash);

KND_API ::asio::awaitable<void> serve_block_transactions(blockchain::block_chain const& chain,
    network::peer_session& peer, network::raw_message const& raw);

[[nodiscard]] KND_API
::asio::awaitable<code> send_compact_block(blockchain::block_chain const& chain,
    network::peer_session& peer, domain::message::block const& block);

} // namespace kth::node

#endif // KTH_NODE_BLOCK_SERVING_HPP
