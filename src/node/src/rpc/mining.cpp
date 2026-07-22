// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/mining.hpp>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

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

domain::message::block assemble_block(
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs) {

    domain::chain::transaction::list txs;
    txs.reserve(1 + job_txs.size());
    txs.push_back(coinbase);
    for (auto const& tx : job_txs) {
        txs.push_back(*tx); // slice message::transaction -> chain::transaction
    }
    return domain::message::block(header, std::move(txs));
}

std::string render_mining_info(blockchain::mining_info const& info) {
    writer w;
    w.begin_object();
    w.field("blocks", static_cast<std::uint64_t>(info.blocks));
    w.field("difficulty", info.difficulty);
    w.field("pooledtx", static_cast<std::uint64_t>(info.pooled_tx));
    w.field("chain", domain::config::name(info.chain));
    w.field("warnings", "");
    w.end_object();
    return w.str();
}

} // namespace kth::node::rpc
