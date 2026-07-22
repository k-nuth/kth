// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdint>
#include <expected>
#include <iomanip>
#include <sstream>
#include <string>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/job_store.hpp>
#include <kth/node/rpc/json.hpp>

namespace kth::node::rpc {

namespace {

// 256-bit value as a 64-char, zero-padded, big-endian hex string (the GBT target
// format).
std::string to_hex256(uint256_t const& value) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(64) << value;
    return os.str();
}

// Compact nBits as an 8-char hex string (the GBT bits format).
std::string bits_to_hex(std::uint32_t bits) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << bits;
    return os.str();
}

// getblockcount -> the height of the most-work fully-validated block.
// C-API counterpart: kth_chain_sync_last_height (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_count(method_context& ctx, request const& /*req*/) {
    auto const heights = co_await ctx.chain.fetch_last_height();
    if ( ! heights) {
        co_return std::unexpected(from_code(heights.error()));
    }

    writer w;
    w.value(static_cast<std::uint64_t>(heights->block));
    co_return w.str();
}

// getblocktemplatelight -> the next-block template minus the transactions, which
// are cached under `job_id` for a later submitblocklight to reassemble.
// C-API counterpart: kth_chain_async_fetch_mining_template (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_template_light(method_context& ctx, request const& /*req*/) {
    auto const tmpl = co_await ctx.chain.fetch_mining_template();
    if ( ! tmpl) {
        co_return std::unexpected(from_code(tmpl.error()));
    }

    auto const job_id = ctx.jobs.add(tmpl->selection.txs);

    // Expand compact bits to the 256-bit target. from_compact only fails on an
    // overflowing encoding, which a chain-produced nBits never is.
    std::string target_hex(64, '0');
    if (auto const target = domain::chain::compact::from_compact(tmpl->bits)) {
        target_hex = to_hex256(target->big());
    }

    writer w;
    w.begin_object();
    w.field("version", static_cast<std::int64_t>(tmpl->version));
    w.field("previousblockhash", encode_hash(tmpl->previous_block_hash));
    w.field("height", static_cast<std::int64_t>(tmpl->height));
    w.field("coinbasevalue", static_cast<std::uint64_t>(tmpl->coinbase_value));
    w.field("target", target_hex);
    w.field("bits", bits_to_hex(tmpl->bits));
    w.field("mintime", static_cast<std::int64_t>(tmpl->min_time));
    w.field("curtime", static_cast<std::int64_t>(tmpl->current_time));
    w.field("sizelimit", static_cast<std::uint64_t>(tmpl->size_limit));
    w.field("sigchecklimit", static_cast<std::uint64_t>(tmpl->sigchecks_limit));
    w.field("noncerange", "00000000ffffffff");
    w.key("mutable").begin_array()
        .value("time").value("transactions").value("prevblock")
        .end_array();
    w.field("job_id", job_id);
    w.end_object();
    co_return w.str();
}

} // namespace

void register_builtin_methods(dispatcher& d) {
    d.add("getblockcount", get_block_count);
    d.add("getblocktemplatelight", get_block_template_light);
}

} // namespace kth::node::rpc
