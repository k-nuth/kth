// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/job_store.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/rpc/mining.hpp>
#include <kth/node/rpc/query.hpp>

namespace kth::node::rpc {

namespace {

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
    co_return render_mining_template(*tmpl, job_id);
}

// getmininginfo -> height, difficulty, mempool size, and network.
// C-API counterpart: kth_chain_async_fetch_mining_info (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mining_info(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    co_return render_mining_info(*info);
}

// submitblocklight -> reconstruct the full block from the miner's solved header +
// coinbase and the cached job selection, then submit it to the chain. Returns
// (Bitcoin submitblock semantics) null on accept, or a reject-reason string.
// C-API counterpart: kth_chain_async_organize_block (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
submit_block_light(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.size() < 2 || args[0].empty() || args[1].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "submitblocklight requires [hexdata, job_id]"));
    }

    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexdata is not valid hex"));
    }

    // The submitted block carries the solved header and just the coinbase.
    byte_reader reader(*raw);
    auto header_and_coinbase = domain::message::block::from_data(reader, 0u);
    if ( ! header_and_coinbase || header_and_coinbase->transactions().empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexdata is not a decodable block with a coinbase"));
    }

    auto const job = ctx.jobs.get(args[1]);
    if ( ! job) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "job_id not found or expired"));
    }

    // Reassemble: coinbase (from the miner) followed by the cached selection.
    auto const block = std::make_shared<domain::message::block>(assemble_block(
        header_and_coinbase->header(),
        header_and_coinbase->transactions().front(),
        *job));

    auto const ec = co_await ctx.chain.organize(block);

    writer w;
    if (ec) {
        w.value(ec.message()); // BIP22-style reject reason
    } else {
        w.value_null();        // accepted
    }
    co_return w.str();
}

// ---- blockchain query methods --------------------------------------------

// getbestblockhash -> the tip block hash.
// C-API counterpart: kth_chain_sync_block_hash (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_best_block_hash(method_context& ctx, request const& /*req*/) {
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
    auto const hash = ctx.chain.get_block_hash(heights->block);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "best block hash unavailable"));
    }
    writer w;
    w.value(encode_hash(*hash));
    co_return w.str();
}

// getblockhash <height> -> the block hash at that height.
// C-API counterpart: kth_chain_sync_block_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_hash(method_context& ctx, request const& req) {
    auto const height = params_uint(req.params, 0);
    if ( ! height) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getblockhash requires [height] (a non-negative integer)"));
    }
    auto const hash = ctx.chain.get_block_hash(static_cast<std::size_t>(*height));
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "block height out of range"));
    }
    writer w;
    w.value(encode_hash(*hash));
    co_return w.str();
}

// getdifficulty -> the difficulty of the next required work.
// C-API counterpart: kth_chain_sync_mining_info.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_difficulty(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    writer w;
    w.value(info->difficulty);
    co_return w.str();
}

// getblockchaininfo -> chain, height, headers, best block hash, difficulty.
// C-API counterpart: kth_chain_sync_mining_info (+ sync_block_hash).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_blockchain_info(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
    auto const best = ctx.chain.get_block_hash(info->blocks);
    if ( ! best) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "best block hash unavailable"));
    }
    co_return render_blockchain_info(
        domain::config::name(info->chain), info->blocks, heights->header,
        encode_hash(*best), info->difficulty);
}

// getrawtransaction <txid> -> the wire-serialized transaction as hex.
// C-API counterpart: kth_chain_sync_transaction.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getrawtransaction requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid txid"));
    }
    auto const result = co_await ctx.chain.fetch_transaction(*hash, /*require_confirmed*/ false);
    if ( ! result) {
        co_return std::unexpected(from_code(result.error()));
    }
    auto const& tx = std::get<0>(*result);
    writer w;
    w.value(transaction_to_hex(*tx));
    co_return w.str();
}

// getblock <blockhash> -> the wire-serialized block as hex.
// C-API counterpart: kth_chain_sync_block_by_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getblock requires [blockhash]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid block hash"));
    }
    auto const result = co_await ctx.chain.fetch_block(*hash);
    if ( ! result) {
        co_return std::unexpected(from_code(result.error()));
    }
    auto const& block = std::get<0>(*result);
    writer w;
    w.value(block_to_hex(*block));
    co_return w.str();
}

} // namespace

void register_builtin_methods(dispatcher& d) {
    d.add("getblockcount", get_block_count);
    d.add("getbestblockhash", get_best_block_hash);
    d.add("getblockhash", get_block_hash);
    d.add("getdifficulty", get_difficulty);
    d.add("getblockchaininfo", get_blockchain_info);
    d.add("getrawtransaction", get_raw_transaction);
    d.add("getblock", get_block);
    d.add("getblocktemplatelight", get_block_template_light);
    d.add("getmininginfo", get_mining_info);
    d.add("submitblocklight", submit_block_light);
}

} // namespace kth::node::rpc
