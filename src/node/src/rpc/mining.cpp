// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/mining.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/mining/block_submit.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/rpc/query.hpp>

namespace kth::node::rpc {

std::string render_mining_template(
    blockchain::mining_template const& tmpl, std::string_view job_id,
    std::optional<chain_progress> progress) {

    // Expand compact bits to the 256-bit target. from_compact only fails on an
    // overflowing encoding, which a chain-produced nBits never is.
    std::string target_hex(64, '0');
    if (auto const target = domain::chain::compact::from_compact(tmpl.bits)) {
        target_hex = encode_hash(target->big());
    }

    writer w;
    w.begin_object();
    w.field("version", int64_t(tmpl.version));
    w.field("previousblockhash", encode_hash(tmpl.previous_block_hash));
    w.field("height", static_cast<std::int64_t>(tmpl.height));
    if (progress) {
        w.field("ticks", progress->ticks);
        w.field("target_time", progress->target_time);
    }
    w.field("coinbasevalue", static_cast<std::uint64_t>(tmpl.coinbase_value));
    w.field("target", target_hex);
    w.field("bits", encode_base16(tmpl.bits));
    w.field("mintime", int64_t(tmpl.min_time));
    w.field("curtime", int64_t(tmpl.current_time));
    w.field("sizelimit", uint64_t(tmpl.size_limit));
    w.field("sigchecklimit", uint64_t(tmpl.sigchecks_limit));
    w.field("noncerange", "00000000ffffffff");
    w.key("mutable").begin_array()
        .value("time").value("transactions").value("prevblock")
        .end_array();
    w.field("job_id", job_id);
    // The coinbase's merkle branch, so a miner can compute the merkle root for
    // its own coinbase while the transactions stay cached under job_id.
    w.key("merkle").begin_array();
    for (auto const& hash : mining::coinbase_merkle_branch(tmpl.selection.txs)) {
        w.value(encode_hash(hash));
    }
    w.end_array();
    w.end_object();
    return w.str();
}

std::string render_block_template(blockchain::mining_template const& tmpl,
    std::optional<chain_progress> progress) {

    std::string target_hex(64, '0');
    if (auto const target = domain::chain::compact::from_compact(tmpl.bits)) {
        target_hex = encode_hash(target->big());
    }

    auto const& txs = tmpl.selection.txs;
    boost::unordered_flat_map<hash_digest, size_t> position;
    position.reserve(txs.size());
    // One generation mark per possible parent, reused for every transaction.
    // No per-input scan or per-transaction clearing of the whole table.
    std::vector<size_t> last_dependency(txs.size(), txs.size());

    writer w;
    w.begin_object();
    w.key("capabilities").begin_array().end_array();
    w.field("version", int64_t(tmpl.version));
    w.field("previousblockhash", encode_hash(tmpl.previous_block_hash));
    w.key("transactions").begin_array();
    for (size_t i = 0; i < txs.size(); ++i) {
        domain::chain::transaction const& tx = *txs[i];
        auto const txid = tx.hash();
        w.begin_object();
        w.field("data", encode_base16(to_data_chunk(tx, /*wire*/ true)));
        w.field("txid", encode_hash(txid));
        w.field("hash", encode_hash(txid));
        // Parents that come earlier in the template, as BIP22 numbers them.
        std::vector<uint64_t> depends;
        for (auto const& input : tx.inputs()) {
            auto const it = position.find(input.previous_output().hash());
            if (it != position.end() && last_dependency[it->second - 1] != i) {
                last_dependency[it->second - 1] = i;
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
        if (i < tmpl.selection.policy.size()) {
            auto const& policy = tmpl.selection.policy[i];
            w.field("fee", policy.fee);
            w.field("sigops", uint64_t(policy.sigchecks));
        }
        w.end_object();
        position.emplace(txid, i + 1);
    }
    w.end_array();
    w.key("coinbaseaux").begin_object().field("flags", "").end_object();
    w.field("coinbasevalue", uint64_t(tmpl.coinbase_value));
    w.field("target", target_hex);
    w.field("mintime", int64_t(tmpl.min_time));
    w.key("mutable").begin_array()
        .value("time").value("transactions").value("prevblock")
        .end_array();
    w.field("noncerange", "00000000ffffffff");
    w.field("sigoplimit", uint64_t(tmpl.sigchecks_limit));
    w.field("sizelimit", uint64_t(tmpl.size_limit));
    w.field("curtime", int64_t(tmpl.current_time));
    w.field("bits", encode_base16(tmpl.bits));
    w.field("height", int64_t(tmpl.height));
    if (progress) {
        w.field("ticks", progress->ticks);
        w.field("target_time", progress->target_time);
    }
    w.end_object();
    return w.str();
}

std::string render_mining_info(blockchain::mining_info const& info,
    std::optional<chain_progress> progress) {
    writer w;
    w.begin_object();
    w.field("blocks", uint64_t(info.blocks));
    w.field("difficulty", info.difficulty);
    if (progress) {
        w.field("ticks", progress->ticks);
        w.field("target_time", progress->target_time);
        w.field("next_target_time", progress->next_target_time);
    }
    w.field("pooledtx", static_cast<std::uint64_t>(info.pooled_tx));
    w.field("chain", domain::config::chain_name(info.chain));
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
