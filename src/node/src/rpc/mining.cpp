// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/mining.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/json.hpp>

namespace kth::node::rpc {

namespace {

// 256-bit value as a 64-char, zero-padded, big-endian hex string (GBT target).
std::string to_hex256(uint256_t const& value) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(64) << value;
    return os.str();
}

// Compact nBits as an 8-char hex string (GBT bits).
std::string bits_to_hex(std::uint32_t bits) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << bits;
    return os.str();
}

} // namespace

std::string render_mining_template(
    blockchain::mining_template const& tmpl, std::string_view job_id) {

    // Expand compact bits to the 256-bit target. from_compact only fails on an
    // overflowing encoding, which a chain-produced nBits never is.
    std::string target_hex(64, '0');
    if (auto const target = domain::chain::compact::from_compact(tmpl.bits)) {
        target_hex = to_hex256(target->big());
    }

    writer w;
    w.begin_object();
    w.field("version", static_cast<std::int64_t>(tmpl.version));
    w.field("previousblockhash", encode_hash(tmpl.previous_block_hash));
    w.field("height", static_cast<std::int64_t>(tmpl.height));
    w.field("coinbasevalue", static_cast<std::uint64_t>(tmpl.coinbase_value));
    w.field("target", target_hex);
    w.field("bits", bits_to_hex(tmpl.bits));
    w.field("mintime", static_cast<std::int64_t>(tmpl.min_time));
    w.field("curtime", static_cast<std::int64_t>(tmpl.current_time));
    w.field("sizelimit", static_cast<std::uint64_t>(tmpl.size_limit));
    w.field("sigchecklimit", static_cast<std::uint64_t>(tmpl.sigchecks_limit));
    w.field("noncerange", "00000000ffffffff");
    w.key("mutable").begin_array()
        .value("time").value("transactions").value("prevblock")
        .end_array();
    w.field("job_id", job_id);
    w.end_object();
    return w.str();
}

std::string render_block_template(
    blockchain::mining_template const& tmpl,
    std::vector<std::optional<template_tx_policy>> const& policy,
    std::string_view longpollid) {

    std::string target_hex(64, '0');
    if (auto const target = domain::chain::compact::from_compact(tmpl.bits)) {
        target_hex = to_hex256(target->big());
    }

    auto const& txs = tmpl.selection.txs;
    boost::unordered_flat_map<hash_digest, std::size_t> position;
    position.reserve(txs.size());

    writer w;
    w.begin_object();
    w.key("capabilities").begin_array().end_array();
    w.field("version", static_cast<std::int64_t>(tmpl.version));
    w.field("previousblockhash", encode_hash(tmpl.previous_block_hash));
    w.key("transactions").begin_array();
    for (std::size_t i = 0; i < txs.size(); ++i) {
        domain::chain::transaction const& tx = *txs[i];
        auto const txid = tx.hash();
        w.begin_object();
        w.field("data", encode_base16(to_data_chunk(tx, /*wire*/ true)));
        w.field("txid", encode_hash(txid));
        w.field("hash", encode_hash(txid));
        // Parents that come earlier in the template, as BIP22 numbers them.
        std::vector<std::uint64_t> depends;
        for (auto const& input : tx.inputs()) {
            auto const it = position.find(input.previous_output().hash());
            if (it != position.end() && std::ranges::find(depends, it->second) == depends.end()) {
                depends.push_back(it->second);
            }
        }
        if ( ! depends.empty()) {
            w.key("depends").begin_array();
            for (auto const index : depends) {
                w.value(index);
            }
            w.end_array();
        }
        if (i < policy.size() && policy[i]) {
            w.field("fee", policy[i]->fee);
            w.field("sigops", static_cast<std::uint64_t>(policy[i]->sigchecks));
        }
        w.end_object();
        position.emplace(txid, i + 1);
    }
    w.end_array();
    w.key("coinbaseaux").begin_object().field("flags", "").end_object();
    w.field("coinbasevalue", static_cast<std::uint64_t>(tmpl.coinbase_value));
    w.field("longpollid", longpollid);
    w.field("target", target_hex);
    w.field("mintime", static_cast<std::int64_t>(tmpl.min_time));
    w.key("mutable").begin_array()
        .value("time").value("transactions").value("prevblock")
        .end_array();
    w.field("noncerange", "00000000ffffffff");
    w.field("sigoplimit", static_cast<std::uint64_t>(tmpl.sigchecks_limit));
    w.field("sizelimit", static_cast<std::uint64_t>(tmpl.size_limit));
    w.field("curtime", static_cast<std::int64_t>(tmpl.current_time));
    w.field("bits", bits_to_hex(tmpl.bits));
    w.field("height", static_cast<std::int64_t>(tmpl.height));
    w.end_object();
    return w.str();
}

std::string render_mining_info(blockchain::mining_info const& info) {
    writer w;
    w.begin_object();
    w.field("blocks", static_cast<std::uint64_t>(info.blocks));
    w.field("difficulty", info.difficulty);
    w.field("pooledtx", static_cast<std::uint64_t>(info.pooled_tx));
    w.field("chain", domain::config::name(info.chain));
    // Why mining work may be refused, reported apart. A transition is a
    // correctness barrier and clears in milliseconds; the other two are
    // operational and clear when the node catches up.
    w.field("transitioninprogress", info.transition_in_progress);
    w.field("caughtup", info.caught_up);
    w.field("fresh", info.fresh);
    w.field("warnings", "");
    w.end_object();
    return w.str();
}

} // namespace kth::node::rpc
