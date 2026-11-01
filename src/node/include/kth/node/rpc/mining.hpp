// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_MINING_HPP
#define KTH_NODE_RPC_MINING_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// What the template's mempool entries say about one of its transactions:
// the fee it pays and the sigchecks it counts. Absent when the entry has left
// the mempool since the template was built.
struct template_tx_policy {
    std::uint64_t fee;
    std::uint32_t sigchecks;
};

// Serialize a mining template as the getblocktemplate "result" object (BIP22,
// with the transactions in full): the transactions in template order with
// their data, txid, the 1-based indexes of earlier template transactions they
// spend from, and fee and sigchecks where `policy` knows them; then the header
// fields, coinbase value, target, limits and `longpollid`.
std::string render_block_template(
    blockchain::mining_template const& tmpl,
    std::vector<std::optional<template_tx_policy>> const& policy,
    std::string_view longpollid);

// Serialize a mining_info as the getmininginfo "result" object.
std::string render_mining_info(blockchain::mining_info const& info);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_MINING_HPP
