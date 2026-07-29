// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_MINING_HPP
#define KTH_NODE_RPC_MINING_HPP

#include <string>
#include <string_view>

#include <kth/blockchain/pools/block_template.hpp>

// The pure (chain-free) rendering behind the mining RPCs, split out so it can be
// unit-tested without a live block_chain: the handlers are just fetch/submit
// glue around these. Block reassembly and submission live in
// kth/node/mining/block_submit.hpp.

namespace kth::node::rpc {

// Serialize a mining template as the getblocktemplatelight "result" object
// (compact bits expanded to the 256-bit target, hashes in display order),
// tagged with `job_id`.
std::string render_mining_template(
    blockchain::mining_template const& tmpl, std::string_view job_id);

// Serialize a mining_info as the getmininginfo "result" object.
std::string render_mining_info(blockchain::mining_info const& info);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_MINING_HPP
