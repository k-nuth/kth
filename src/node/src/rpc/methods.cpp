// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <functional>


#include <asio/awaitable.hpp>
#include <simdjson.h>

#include <kth/blockchain/interface/block_chain.hpp>
#if defined(KTH_CURRENCY_BCH)
#include <kth/blockchain/tick_schedule.hpp>
#endif
#include <kth/domain/config/network.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/chain/faster_blocks.hpp>
#include <kth/domain/version.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/expect.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/error.hpp>
#include <kth/node/mining/block_submit.hpp>
#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/node/rpc/mining.hpp>
#include <kth/node/rpc/query.hpp>
#include <kth/node/p2p_node.hpp>

namespace kth::node::rpc {

namespace {
#if defined(KTH_CURRENCY_BCH)
std::expected<chain_progress, rpc_error> progress_for(
    blockchain::block_chain const& chain, hash_digest const& hash, size_t height,
    blockchain::block_chain::chain_view_ptr const& captured = {}) {
    auto const progress = chain.get_block_progress(hash, captured ? captured : chain.chain_view());
    if (!progress) return kth::fail(from_code(progress.error()));
    if (height == progress->height) {
        return chain_progress{progress->ticks, progress->target_time, progress->next_target_time};
    }
    if (height == progress->height + 1) {
        return chain_progress{progress->ticks + progress->next_target_time,
            progress->next_target_time, progress->next_target_time};
    }
    return kth::fail(make_error(error_code::internal_error, "block progress height mismatch"));
}
#endif

// A block or undo file of the flat file store: blk/rev followed by at least
// five digits (flat_file_seq's minimum, not a maximum) and `.dat`.
bool is_block_file(std::string_view name) {
    return name.size() >= 12 && name.ends_with(".dat") &&
        (name.starts_with("blk") || name.starts_with("rev")) &&
        std::ranges::all_of(name.substr(3, name.size() - 7),
            [](char c) { return c >= '0' && c <= '9'; });
}

// getblock and getrawtransaction arguments, as BCHN takes them: the hash, then
// the verbosity as a boolean or a number (null keeps the default), then, for
// getrawtransaction only, the block to read the transaction from. Types and
// arity are checked before storage is touched.
struct raw_read_arguments {
    hash_digest hash;
    std::optional<hash_digest> block_hash;
    // getblock: 0 hex, 1 fields and txids, 2 decoded transactions (default 1).
    // getrawtransaction: 0 hex, otherwise decoded (default 0).
    uint64_t verbosity;
};

std::expected<raw_read_arguments, rpc_error> raw_read_params(request const& req, bool transaction) {
    auto const invalid = [&] {
        return kth::fail(make_error(error_code::invalid_params,
            transaction ? "getrawtransaction requires [txid, verbose=false, blockhash]" :
                          "getblock requires [blockhash, verbosity=1]"));
    };
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(req.params);
    simdjson::ondemand::document doc;
    simdjson::ondemand::array args;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS ||
        doc.get_array().get(args) != simdjson::SUCCESS) {
        return invalid();
    }
    raw_read_arguments result{.hash = {}, .block_hash = std::nullopt,
        .verbosity = transaction ? 0u : 1u};
    size_t count = 0;
    for (auto element : args) {
        simdjson::ondemand::value value;
        simdjson::ondemand::json_type type;
        if (count >= (transaction ? 3u : 2u) || element.get(value) != simdjson::SUCCESS ||
            value.type().get(type) != simdjson::SUCCESS) {
            return invalid();
        }
        if (count == 0 || count == 2) {
            if (count == 2 && type == simdjson::ondemand::json_type::null) {
                ++count;
                continue;
            }
            std::string_view text;
            if (value.get_string().get(text) != simdjson::SUCCESS) {
                return invalid();
            }
            if (count == 2 && text.empty()) {
                ++count;
                continue;
            }
            auto hash = decode_hash(text);
            if (!hash) {
                return kth::fail(make_error(error_code::invalid_params,
                    transaction && count == 0 ? "invalid txid" : "invalid block hash"));
            }
            if (count == 0) {
                result.hash = *hash;
            } else {
                result.block_hash = *hash;
            }
        } else if (type == simdjson::ondemand::json_type::boolean) {
            bool verbose;
            if (value.get_bool().get(verbose) != simdjson::SUCCESS) {
                return invalid();
            }
            result.verbosity = verbose ? 1u : 0u;
        } else if (type == simdjson::ondemand::json_type::number) {
            int64_t verbosity;
            // getrawtransaction's verbosity 2 (fees and prevouts) is not
            // implemented, so it is rejected instead of answered as 1.
            if (value.get_int64().get(verbosity) != simdjson::SUCCESS || verbosity < 0 ||
                    verbosity > (transaction ? 1 : 2)) {
                return kth::fail(make_error(error_code::invalid_params, transaction ?
                    "verbose must be 0, 1 or a boolean" :
                    "verbosity must be 0, 1, 2 or a boolean"));
            }
            result.verbosity = uint64_t(verbosity);
        } else if (type != simdjson::ondemand::json_type::null) {
            return invalid();
        }
        ++count;
    }
    if (count == 0) {
        return invalid();
    }
    return result;
}


// getblockcount -> the height of the most-work fully-validated block.
// C-API counterpart: kth_chain_sync_last_height (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_count(method_context& ctx, request const& /*req*/) {
    auto const heights = co_await ctx.chain.fetch_last_height();
    if ( ! heights) {
        co_return kth::fail(from_code(heights.error()));
    }

    writer w;
    w.value(uint64_t(heights->block));
    co_return w.str();
}

// getblocktemplatelight -> the next-block template minus the transactions, which
// are cached under `job_id` for a later submitblocklight to reassemble.
// C-API counterpart: kth_chain_async_fetch_mining_template (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_template_light(method_context& ctx, request const& /*req*/) {
    auto const tmpl = co_await ctx.chain.fetch_mining_template(
        blockchain::default_coinbase_reserve_size);
    if ( ! tmpl) {
        co_return kth::fail(from_code(tmpl.error()));
    }

    auto const job_id = ctx.jobs.add(tmpl->selection.txs);
#if defined(KTH_CURRENCY_BCH)
    // The template's parent fixes its branch even if the active tip changes.
    auto const progress = progress_for(ctx.chain, tmpl->previous_block_hash, tmpl->height);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_mining_template(*tmpl, job_id, *progress);
#else
    co_return render_mining_template(*tmpl, job_id);
#endif
}

// getblocktemplate [template_request] -> the next-block template with its
// transactions in full (BIP22), for pools that build and submit whole blocks.
// Only "template" mode; "proposal" is refused. The request's capabilities and
// rules ask for nothing this template leaves out.
// C-API counterpart: kth_chain_async_fetch_mining_template.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_template(method_context& ctx, request const& req) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(req.params.empty() ? std::string_view{"[]"} : req.params);
    simdjson::ondemand::document doc;
    simdjson::ondemand::array params;
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "getblocktemplate requires [] or [template_request object]"));
    };
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS ||
        doc.get_array().get(params) != simdjson::SUCCESS) {
        co_return invalid_params();
    }
    size_t count = 0;
    for (auto element : params) {
        simdjson::ondemand::object request_object;
        if (count++ != 0 || element.get_object().get(request_object) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        std::string_view mode;
        auto const mode_error = request_object["mode"].get_string().get(mode);
        if (mode_error != simdjson::NO_SUCH_FIELD) {
            if (mode_error != simdjson::SUCCESS) {
                co_return invalid_params();
            }
            if (mode != "template") {
                co_return kth::fail(make_error(error_code::invalid_params,
                    "only template mode is supported"));
            }
        }
    }

    auto const tmpl = co_await ctx.chain.fetch_mining_template(
        blockchain::default_coinbase_reserve_size);
    if ( ! tmpl) {
        co_return kth::fail(from_code(tmpl.error()));
    }

#if defined(KTH_CURRENCY_BCH)
    auto const progress = progress_for(ctx.chain, tmpl->previous_block_hash, tmpl->height);
    if (!progress) co_return kth::fail(progress.error());
    co_return render_block_template(*tmpl, *progress);
#else
    co_return render_block_template(*tmpl);
#endif
}

// getmininginfo -> height, difficulty, mempool size, and network.
// C-API counterpart: kth_chain_async_fetch_mining_info (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mining_info(method_context& ctx, request const& /*req*/) {
#if defined(KTH_CURRENCY_BCH)
    auto const published = ctx.chain.chain_view();
#endif
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return kth::fail(from_code(info.error()));
    }
#if defined(KTH_CURRENCY_BCH)
    if (!published || ctx.chain.chain_view() != published) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "mining progress unavailable during chain transition"));
    }
    auto const progress = progress_for(ctx.chain, published->tip_hash, info->blocks, published);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_mining_info(*info, *progress);
#else
    co_return render_mining_info(*info);
#endif
}

[[nodiscard]]
std::expected<domain::message::block, rpc_error> parse_submitted_block(std::string_view hexdata) {
    auto const raw = decode_base16(hexdata);
    if ( ! raw) {
        return kth::fail(make_error(error_code::deserialization_error,
            "hexdata is not valid hex"));
    }

    byte_reader reader(*raw);
    auto block = domain::message::block::from_data(reader, 0u);
    if ( ! block || reader.remaining_size() != 0) {
        return kth::fail(make_error(error_code::deserialization_error,
            "hexdata is not a decodable block"));
    }
    if (block->transactions().empty() || ! block->transactions().front().is_coinbase()) {
        return kth::fail(make_error(error_code::deserialization_error,
            "block does not start with a coinbase"));
    }
    return std::move(*block);
}

// Translate only information supplied by admission. Some KTH codes combine
// multiple BCHN rejection reasons; those cannot be reconstructed at the RPC
// boundary and use BIP22's generic "rejected" instead of a KTH diagnostic.
[[nodiscard]]
std::optional<std::string> bip22_reject_reason(code const& ec, uint32_t version) {
    static constexpr std::pair<error::error_code_t, std::string_view> reasons[]{
        {error::invalid_proof_of_work, "high-hash"},
        {error::futuristic_timestamp, "time-too-new"},
        {error::checkpoints_failed, "bad-fork-prior-to-checkpoint"},
        {error::incorrect_proof_of_work, "bad-diffbits"},
        {error::timestamp_too_early, "time-too-old"},
        {error::orphan_block, "prev-blk-not-found"},
        {error::store_block_missing_parent, "prev-blk-not-found"},
        {error::invalid_previous_block, "bad-prevblk"},
        {error::block_size_limit, "bad-blk-length"},
        {error::empty_block, "bad-cb-missing"},
        {error::first_not_coinbase, "bad-cb-missing"},
        {error::extra_coinbases, "bad-tx-coinbase"},
        {error::internal_duplicate, "bad-txns-duplicate"},
        {error::block_internal_double_spend, "bad-txns-inputs-missingorspent"},
        {error::merkle_mismatch, "bad-txnmrklroot"},
        {error::block_non_final, "bad-txns-nonfinal"},
        {error::coinbase_height_mismatch, "bad-cb-height"},
        {error::coinbase_value_limit, "bad-cb-amount"},
        {error::previous_output_null, "bad-txns-prevout-null"},
        {error::invalid_coinbase_script_size, "bad-cb-length"},
        {error::coinbase_transaction, "bad-tx-coinbase"},
        {error::transaction_internal_double_spend, "bad-txns-inputs-duplicate"},
        {error::transaction_size_limit, "bad-txns-oversize"},
        {error::transaction_non_final, "bad-txns-nonfinal"},
        {error::unspent_duplicate, "bad-txns-BIP30"},
        {error::missing_previous_output, "bad-txns-inputs-missingorspent"},
        {error::double_spend, "bad-txns-inputs-missingorspent"},
        {error::coinbase_maturity, "bad-txns-premature-spend-of-coinbase"},
        {error::spend_exceeds_value, "bad-txns-in-belowout"},
        {error::sequence_locked, "bad-txns-nonfinal"},
        {error::transaction_version_out_of_range, "bad-txns-version"},
        {error::empty_transaction, "rejected"},
        {error::spend_overflow, "rejected"},
        {error::premature_validation, "rejected"},
        {error::finalized_header_violation, "rejected"},
#if defined(KTH_CURRENCY_BCH)
        {error::non_canonical_ordered, "tx-ordering"},
        {error::invalid_bitfield, "bad-txns-token-bad-bitfield"},
        {error::token_amount_negative, "bad-txns-token-amount-negative"},
        {error::token_fungible_only_amount_zero, "bad-txns-token-non-nft-amount-zero"},
        {error::token_amount_bitfield_mismatch, "bad-txns-token-amount-bitfield-mismatch"},
        {error::token_commitment_bitfield_mismatch, "bad-txns-token-commitment-bitfield-mismatch"},
        {error::token_fungible_with_commitment, "bad-txns-token-fungible-with-commitment"},
        {error::token_commitment_oversized, "bad-txns-token-commitment-oversized"},
        {error::token_coinbase_has_tokens, "bad-txns-coinbase-has-tokens"},
        {error::token_unparseable_output, "bad-txns-vout-tokenprefix"},
        {error::token_unparseable_input, "bad-txns-vin-tokenprefix"},
        {error::token_input_created_pre_activation, "bad-txns-vin-token-created-pre-activation"},
        {error::token_inputs_missing_or_spent, "bad-txns-inputs-missingorspent"},
        {error::token_duplicate_genesis, "bad-txns-token-dupe-genesis"},
        {error::token_invalid_category, "bad-txns-token-invalid-category"},
        {error::token_fungible_insufficient, "bad-txns-token-in-belowout"},
        {error::token_nft_ex_nihilo, "bad-txns-token-nft-ex-nihilo"},
        {error::token_amount_overflow, "bad-txns-token-amount-overflow"},
        {error::token_pre_activation_input, "bad-txns-vin-tokenprefix-preactivation"},
#endif
    };
    if (ec == error::old_version_block) {
        return fmt::format("bad-version(0x{:08x})", version);
    }
    for (auto const& [value, reason] : reasons) {
        // Compare categories as well as values: system errors can have the same
        // numeric value as a consensus error.
        if (ec == value) {
            return std::string(reason);
        }
    }
    return std::nullopt;
}

// Full and light RPC submissions share the complete admission response.
[[nodiscard]]
std::expected<std::string, rpc_error> render_block_submission(
    blockchain::block_submission_result const& result, uint32_t version) {
    auto const ec = result.error;
    if (ec == error::service_stopped) {
        return kth::fail(make_error(error_code::in_warmup, ec.message()));
    }
    if (ec == error::operation_failed || ec == error::file_system || ec == error::unknown) {
        return kth::fail(make_error(error_code::verify_error, ec.message()));
    }
    if (result.duplicate) {
        writer w;
        switch (result.validity) {
            case blockchain::block_validity::valid:
                w.value("duplicate");
                break;
            case blockchain::block_validity::invalid:
                w.value("duplicate-invalid");
                break;
            case blockchain::block_validity::unknown:
                w.value("duplicate-inconclusive");
                break;
        }
        return w.str();
    }
    writer w;
    if (ec == error::insufficient_work || ec == error::stale_chain) {
        w.value("inconclusive");
    } else if ( ! ec) {
        if (result.validity == blockchain::block_validity::valid) {
            w.value_null();
        } else {
            w.value(result.validity == blockchain::block_validity::invalid ? "rejected" : "inconclusive");
        }
    } else if (auto reason = bip22_reject_reason(ec, version)) {
        w.value(*reason);
    } else if (result.validity == blockchain::block_validity::invalid) {
        w.value("rejected");
    } else {
        return kth::fail(make_error(error_code::verify_error, ec.message()));
    }
    return w.str();
}

// submitblock -> submit a full wire block, with an optional ignored second arg.
// C-API counterpart: kth_chain_async_submit_block (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
submit_block(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args.size() > 2 || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "submitblock requires [hexdata] or [hexdata, dummy]"));
    }

    auto decoded = parse_submitted_block(args[0]);
    if ( ! decoded) {
        co_return kth::fail(decoded.error());
    }
    auto const block = std::make_shared<domain::message::block>(std::move(*decoded));
    auto const result = co_await ctx.chain.submit_block(block);
    co_return render_block_submission(result, block->header().version());
}

// submitblocklight -> reconstruct the full block from the miner's solved header +
// coinbase and the cached job selection, then submit it to the chain. Returns
// (Bitcoin submitblock semantics) null on accept, or a reject-reason string.
// C-API counterpart: kth_chain_async_submit_block (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
submit_block_light(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.size() < 2 || args[0].empty() || args[1].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "submitblocklight requires [hexdata, job_id]"));
    }

    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "hexdata is not valid hex"));
    }

    // The submitted block carries the solved header and just the coinbase.
    byte_reader reader(*raw);
    auto header_and_coinbase = domain::message::block::from_data(reader, 0u);
    if ( ! header_and_coinbase || header_and_coinbase->transactions().empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "hexdata is not a decodable block with a coinbase"));
    }

    auto const job = ctx.jobs.get(args[1]);
    if ( ! job) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "job_id not found or expired"));
    }

    // Reassemble (coinbase followed by the cached selection) and submit.
    auto const block = std::make_shared<domain::message::block>(mining::assemble_block(
        header_and_coinbase->header(), header_and_coinbase->transactions().front(), *job));
    auto const result = co_await ctx.chain.submit_block(block);
    co_return render_block_submission(result, block->header().version());
}

// ---- blockchain query methods --------------------------------------------

// getbestblockhash -> the tip block hash.
// C-API counterpart: kth_chain_sync_block_hash (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_best_block_hash(method_context& ctx, request const& /*req*/) {
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
    auto const hash = ctx.chain.get_block_hash(heights->block);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::internal_error,
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
        co_return kth::fail(make_error(error_code::invalid_params,
            "getblockhash requires [height] (a non-negative integer)"));
    }
    auto const hash = ctx.chain.get_block_hash(size_t(*height));
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params,
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
        co_return kth::fail(from_code(info.error()));
    }
    writer w;
    w.value(info->difficulty);
    co_return w.str();
}

// getnetworkhashps [nblocks=120] [height=-1] -> the hashes per second the
// network did over the `nblocks` blocks ending at `height`, on the connected
// chain: the work those blocks add, over the time their timestamps span (the
// block before them included, as the first time). nblocks must be positive
// except at genesis; a height of -1 or past the connected tip means the
// connected tip. 0 when the window spans no time.
// C-API counterpart: kth_chain_sync_block_header_by_height (each block's time
// and bits, from which the work follows).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_network_hash_ps(method_context& ctx, request const& req) {
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "getnetworkhashps takes [nblocks, height] as integers"));
    };
    if ( ! req.params.empty()) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.params);
        simdjson::ondemand::document document;
        simdjson::ondemand::array args;
        if (parser.iterate(padded).get(document) != simdjson::SUCCESS ||
            document.get_array().get(args) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        size_t count = 0;
        for (auto element : args) {
            if (element.error() != simdjson::SUCCESS || ++count > 2) {
                co_return invalid_params();
            }
        }
    }
    constexpr int64_t default_blocks = 120;
    auto const nblocks = params_int(req.params, 0, default_blocks);
    auto const height = params_int(req.params, 1, -1);
    auto const fits_rpc_integer = [](auto const& value) {
        return value && *value >= std::numeric_limits<int32_t>::min() &&
            *value <= std::numeric_limits<int32_t>::max();
    };
    if ( ! fits_rpc_integer(nblocks) || ! fits_rpc_integer(height)) {
        co_return invalid_params();
    }

    auto const view = ctx.chain.chain_view();
    if ( ! view) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const& index = ctx.chain.headers();
    auto const tip = index.find(view->tip_hash);
    if (tip == blockchain::header_index::null_index) {
        co_return kth::fail(make_error(error_code::internal_error,
            "connected tip not in the header index"));
    }

    auto const tip_height = view->connected_tip_height;
    auto const end_height = *height < 0 || uint64_t(*height) > tip_height
        ? tip_height : size_t(*height);
    // BCHN returns zero at genesis before validating a non-positive lookup.
    if (end_height == 0) {
        co_return render_network_hash_ps({}, {}, 0, 0);
    }
    if (*nblocks <= 0) {
        co_return kth::fail(make_error(error_code::invalid_parameter,
            "getnetworkhashps requires a positive number of blocks"));
    }
    auto const window = std::min(size_t(*nblocks), end_height);

    auto const last = index.get_ancestor(tip, int32_t(end_height));
    auto const first = index.get_ancestor(last, int32_t(end_height - window));
    if (last == blockchain::header_index::null_index ||
        first == blockchain::header_index::null_index) {
        co_return kth::fail(make_error(error_code::internal_error,
            "window not in the header index"));
    }

    // Block times need not increase, so take the extremes over the window.
    auto min_time = std::numeric_limits<uint32_t>::max();
    auto max_time = std::numeric_limits<uint32_t>::min();
    for (auto idx = last; idx != blockchain::header_index::null_index;
         idx = index.get_parent_index(idx)) {
        auto const time = index.get_timestamp(idx);
        min_time = std::min(min_time, time);
        max_time = std::max(max_time, time);
        if (idx == first) {
            break;
        }
    }

    co_return render_network_hash_ps(index.get_chain_work(first), index.get_chain_work(last),
        min_time, max_time);
}

// getblockchaininfo -> chain, heights, best block hash, difficulty, median time
// past, sync state, chain work, and the size of the block and undo files.
// C-API counterpart: kth_chain_sync_mining_info (+ sync_block_hash).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_blockchain_info(method_context& ctx, request const& /*req*/) {
    // The tip fields from one published view: a height and a hash read apart
    // can describe two different tips.
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const header_tip = ctx.chain.headers().active_tip_height();
    if (header_tip < 0) {
        co_return kth::fail(make_error(error_code::internal_error, "chain height unavailable"));
    }
    auto const synced = ctx.chain.synchronization(*view, header_tip);

    auto const chain_work = ctx.chain.headers().total_chain_work(view->tip_hash);


#if defined(KTH_CURRENCY_BCH)
    auto const progress = progress_for(ctx.chain, view->tip_hash, view->connected_tip_height, view);
    if (!progress) {
        co_return kth::fail(progress.error());
    }
#endif
    co_return render_blockchain_info({
        .chain = domain::config::chain_name(view->state->network()),
        .blocks = view->connected_tip_height,
        .headers = size_t(header_tip),
        .best_block_hash = view->tip_hash,
        .difficulty = blockchain::difficulty_from_bits(view->state->work_required()),
        .median_time = view->state->median_time_past(),
        .chain_work = chain_work,
        .size_on_disk = files_size(ctx.chain.data_dir() / "blocks", is_block_file),
        .initial_block_download = ! synced.synchronized()
#if defined(KTH_CURRENCY_BCH)
        , .progress = *progress
#endif
    });
}

// getchaintips -> every chain tip the header index knows, highest first: the
// connected tip as "active", and each other header without children with its
// branch length from the connected chain, as "valid-headers" when the node
// holds every block of the branch and "headers-only" otherwise.
// C-API counterpart: none yet; the C-API does not expose the header index.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_chain_tips(method_context& ctx, request const& /*req*/) {
    auto const view = ctx.chain.chain_view();
    if ( ! view) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const tips = ctx.chain.headers().get_chain_tips(view->tip_hash
#if defined(KTH_CURRENCY_BCH)
        , ctx.chain.chain_settings().cantor_activation_time
#endif
    );
    if (!tips) {
        co_return kth::fail(make_error(error_code::internal_error,
            "connected tip not in the header index"));
    }
    co_return render_chain_tips(*tips);
}

// getrawtransaction <txid> [verbose] [blockhash] -> the wire-serialized
// transaction as hex. With a block hash it is read from that block; without
// one only the mempool can answer, since confirmed transactions are not
// indexed by txid (#491).
// C-API counterpart: kth_chain_sync_transaction (+ kth_chain_sync_block_by_hash).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_raw_transaction(method_context& ctx, request const& req) {
    auto const args = raw_read_params(req, true);
    if (!args) {
        co_return kth::fail(args.error());
    }
    auto const& hash = args->hash;
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const render = [&](domain::chain::transaction const& tx,
                            std::optional<transaction_location> const& location) {
        if (args->verbosity != 0) {
            return render_transaction_verbose(tx, view->state->network(),
                view->state->enabled_flags(), location);
        }
        writer w;
        w.value(transaction_to_hex(tx));
        return w.str();
    };

    if (args->block_hash) {
        auto const block = co_await ctx.chain.fetch_block(*args->block_hash);
        if (!block) {
            if (block.error() != error::not_found) {
                co_return kth::fail(from_code(block.error()));
            }
            co_return kth::fail(make_error(error_code::invalid_address_or_key,
                "Block hash not found"));
        }
        auto const& found = std::get<0>(*block);
        for (auto const& tx : found->transactions()) {
            if (tx.hash() == hash) {
                if (args->verbosity == 0) {
                    co_return render(tx, std::nullopt);
                }
                auto const progress = ctx.chain.get_block_progress(*args->block_hash, view);
                if (!progress) {
                    co_return kth::fail(from_code(progress.error()));
                }
                co_return render(tx, transaction_location{*args->block_hash,
                    progress->confirmations, found->header().timestamp(), progress->confirmations_ticks});
            }
        }
        co_return kth::fail(make_error(error_code::invalid_address_or_key,
            "No such transaction found in the provided block"));
    }

    auto const result = co_await ctx.chain.fetch_transaction(hash, /*require_confirmed*/ false);
    if ( ! result) {
        if (result.error() == error::not_found) {
            co_return kth::fail(make_error(error_code::invalid_address_or_key,
                "No such mempool transaction; pass the block hash for a confirmed one"));
        }
        co_return kth::fail(from_code(result.error()));
    }
    co_return render(*std::get<0>(*result), std::nullopt);
}

// getblock <blockhash> [verbosity=1] -> with verbosity 0 (or false) the
// wire-serialized block as hex; with 1 (or true) its fields and txids; with 2
// its fields and its transactions decoded.
// C-API counterpart: kth_chain_sync_block_by_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block(method_context& ctx, request const& req) {
    auto const args = raw_read_params(req, false);
    if (!args) {
        co_return kth::fail(args.error());
    }
    auto const result = co_await ctx.chain.fetch_block(args->hash);
    if ( ! result) {
        if (result.error() == error::not_found) {
            co_return kth::fail(make_error(error_code::invalid_address_or_key,
                "Block not found"));
        }
        co_return kth::fail(from_code(result.error()));
    }
    auto const& [block, height] = *result;
    if (args->verbosity == 0) {
        writer w;
        w.value(block_to_hex(*block));
        co_return w.str();
    }

    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const& index = ctx.chain.headers();
    auto const idx = index.find(args->hash);
    if (idx == blockchain::header_index::null_index) {
        co_return kth::fail(make_error(error_code::invalid_address_or_key,
            "Block not found"));
    }
    auto const progress = ctx.chain.get_block_progress(args->hash, view);
    if (!progress) {
        co_return kth::fail(from_code(progress.error()));
    }
    block_location location{
        .height = height,
        .confirmations = progress->confirmations,
        .median_time = domain::chain::median_time_past_of(idx,
            [&](blockchain::header_index::index_t at) { return index.get_timestamp(at); },
            [&](blockchain::header_index::index_t at) -> std::optional<blockchain::header_index::index_t> {
                auto const parent = index.get_parent_index(at);
                if (parent == blockchain::header_index::null_index) {
                    return std::nullopt;
                }
                return parent;
            }),
        .chain_work = index.total_chain_work(args->hash),
        .next_block_hash = std::nullopt,
        .progress = chain_progress{progress->ticks, progress->target_time, progress->next_target_time},
        .confirmations_ticks = progress->confirmations_ticks};
    if (location.confirmations > 1) {
        auto const connected_tip = index.find(view->tip_hash);
        auto const next = index.get_ancestor(connected_tip, int32_t(height + 1));
        if (next != blockchain::header_index::null_index) {
            location.next_block_hash = index.get_hash(next);
        }
    }
    co_return render_block_verbose(*block, location, args->verbosity == 2,
        view->state->network(), view->state->enabled_flags());
}

// getblockheader <blockhash> [verbose=true] -> the header fields with its
// height and confirmations, or with verbose=false the 80-byte serialized
// header as hex.
// C-API counterpart: kth_chain_sync_block_header_by_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_header(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "getblockheader requires [blockhash]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params, "invalid block hash"));
    }
    auto const verbose = params_bool(req.params, 1, /*fallback*/ true);
    if ( ! verbose) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "verbose must be a boolean"));
    }
    auto const result = co_await ctx.chain.fetch_block_header(*hash);
    if ( ! result) {
        co_return kth::fail(from_code(result.error()));
    }
    if ( ! *verbose) {
        writer w;
        w.value(header_to_hex(*std::get<0>(*result)));
        co_return w.str();
    }

    auto const& [header, height] = *result;
    auto const progress = ctx.chain.get_block_progress(*hash);
    if (!progress) co_return kth::fail(from_code(progress.error()));
#if defined(KTH_CURRENCY_BCH)
    co_return render_block_header(*header, height, *hash, progress->confirmations,
        chain_progress{progress->ticks, progress->target_time, progress->next_target_time},
        progress->confirmations_ticks);
#else
    co_return render_block_header(*header, height, *hash, progress->confirmations);
#endif

}

// sendrawtransaction <hexstring> -> the accepted txid, or a reject error.
// C-API counterpart: kth_chain_sync_organize_transaction.
::asio::awaitable<std::expected<std::string, rpc_error>>
send_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "sendrawtransaction requires [hexstring]"));
    }
    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "hexstring is not valid hex"));
    }
    byte_reader reader(*raw);
    auto tx = domain::chain::transaction::from_data(reader, /*wire*/ true);
    if ( ! tx) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "transaction decode failed"));
    }

    auto const txid = encode_hash(tx->hash());
    auto const pooled = std::make_shared<domain::message::transaction>(std::move(*tx));
    auto const ec = co_await ctx.chain.organize(pooled);
    if (ec) {
        co_return kth::fail(make_error(error_code::misc_error, ec.message()));
    }

    writer w;
    w.value(txid);
    co_return w.str();
}

// validateaddress <address> -> whether it is a P2PKH or P2SH address of this
// node's network and, if it is, its cashaddr form and the scriptPubKey it pays.
// C-API counterpart: kth_wallet_payment_address_parse_from.
::asio::awaitable<std::expected<std::string, rpc_error>>
validate_address(method_context& ctx, request const& req) {
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "validateaddress requires [address]"));
    };
    // An empty string is an invalid address, not a missing argument. Parse
    // the type explicitly because params_strings maps non-strings to empty.
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(req.params);
    simdjson::ondemand::document doc;
    simdjson::ondemand::array args;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS ||
        doc.get_array().get(args) != simdjson::SUCCESS) {
        co_return invalid_params();
    }
    std::string address;
    size_t count = 0;
    for (auto element : args) {
        if (count++ != 0) {
            co_return invalid_params();
        }
        simdjson::ondemand::value value;
        std::string_view text;
        if (element.get(value) != simdjson::SUCCESS ||
            value.get_string().get(text) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        address.assign(text);
    }
    if (count != 1) {
        co_return invalid_params();
    }
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    co_return render_validate_address(address, view->state->network());
}

// estimatefee [nblocks] -> the fee per kB, in BCH, that the size part of this
// node's transaction price asks for (chain.byte_fee_satoshis). The price also
// charges per signature operation (chain.sigop_fee_satoshis), which a per-kB
// rate cannot carry. kth has no fee estimator, so nblocks, which older clients
// send, does not change the answer. Anything other than no argument or one
// non-negative integer is refused rather than answered.
// C-API counterpart: kth_blockchain_settings.byte_fee_satoshis.
::asio::awaitable<std::expected<std::string, rpc_error>>
estimate_fee(method_context& ctx, request const& req) {
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "estimatefee takes no argument or [nblocks] (a non-negative integer)"));
    };
    if ( ! req.params.empty()) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.params);
        simdjson::ondemand::document document;
        simdjson::ondemand::array args;
        if (parser.iterate(padded).get(document) != simdjson::SUCCESS ||
            document.get_array().get(args) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        size_t count = 0;
        for (auto element : args) {
            if (element.error() != simdjson::SUCCESS || ++count > 1) {
                co_return invalid_params();
            }
        }
        // Preserve the existing target conversion, but only after validating
        // its container and arity; a non-array must never mean "no target".
        if (count == 1 && ! params_uint(req.params, 0)) {
            co_return invalid_params();
        }
    }
    co_return render_estimate_fee(ctx.chain.chain_settings().byte_fee_satoshis);
}

// decoderawtransaction <hexstring> -> the transaction's fields, decoded for
// this node's network and current script rules.
// C-API counterpart: kth_chain_transaction_construct_from_data (+ its accessors).
::asio::awaitable<std::expected<std::string, rpc_error>>
decode_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "decoderawtransaction requires [hexstring]"));
    }
    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "hexstring is not valid hex"));
    }
    byte_reader reader(*raw);
    auto const tx = domain::chain::transaction::from_data(reader, /*wire*/ true);
    if ( ! tx || ! reader.is_exhausted()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "transaction decode failed"));
    }
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    co_return render_transaction(*tx, view->state->network(), view->state->enabled_flags());
}

// gettxout <txid> <n> [include_mempool=true] -> the output if it is unspent,
// else null. With include_mempool, an output that a mempool transaction spends
// is null, and an output of a mempool transaction has 0 confirmations.
// C-API counterpart: none yet; the C-API has no UTXO reader.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_tx_out(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    auto const index = params_uint(req.params, 1);
    if (args.empty() || args[0].empty() || ! index ||
        *index > std::numeric_limits<uint32_t>::max()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "gettxout requires [txid, n]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params, "invalid txid"));
    }
    auto const include_mempool = params_bool(req.params, 2, /*fallback*/ true);
    if ( ! include_mempool) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "include_mempool must be a boolean"));
    }
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return kth::fail(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const net = view->state->network();
    auto const flags = view->state->enabled_flags();
    domain::chain::output_point const outpoint{*hash, uint32_t(*index)};
    writer none;
    none.value_null();

    if (*include_mempool) {
        auto const& pool = ctx.chain.mempool_ref();
        if (pool.spender(outpoint)) {
            co_return none.str();
        }
        if (auto const output = pool.resolve(outpoint)) {
            co_return render_tx_out(view->tip_hash, 0, *output, /*coinbase*/ false, net, flags
#if defined(KTH_CURRENCY_BCH)
                , int64_t{0}
#endif
            );
        }
    }

    // The confirmed UTXO set at the connected tip. "Not resolved" means only
    // that the active versions cannot answer: the older ones are asked with a
    // request of one, and only their miss is absence.
    auto const tip = view->connected_tip_height;
    auto utxo = ctx.chain.get_utxo(outpoint, tip);
    if ( ! utxo && utxo.error() == database::result_code::not_resolved) {
        auto const height = database::to_store_height(tip);
        if ( ! height) {
            co_return kth::fail(make_error(error_code::internal_error,
                "tip height does not fit the UTXO store"));
        }
        auto const& txid = outpoint.hash();
        std::array<utxoz::lookup_request, 1> const own{utxoz::lookup_request{
            utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, outpoint.index()),
            *height}};
        auto const resolved = ctx.chain.utxo_resolve(own);
        if ( ! resolved) {
            co_return kth::fail(make_error(error_code::internal_error,
                "the UTXO store could not be read"));
        }
        auto const it = resolved->found.find(own[0].key);
        if (it == resolved->found.end()) {
            co_return none.str();
        }
        utxo = blockchain::block_chain::output_info{
            it->second.output(), it->second.height(),
            it->second.median_time_past(), it->second.coinbase()};
    }
    if ( ! utxo) {
        co_return kth::fail(make_error(error_code::internal_error,
            "the UTXO store could not be read"));
    }
    co_return render_tx_out(view->tip_hash,
        int64_t(tip - utxo->height + 1), utxo->output, utxo->coinbase, net, flags
#if defined(KTH_CURRENCY_BCH)
        , int64_t(view->state->ticks().height_to_tick(tip)
            - view->state->ticks().height_to_tick(utxo->height)
            + view->state->ticks().block_interval(utxo->height))
#endif
    );
}

// getnetworkinfo -> this node's version and user agent, protocol version,
// services, whether it relays transactions, its connection count, and the
// relay fee (the size part of its transaction price) in BCH per kB.
// C-API counterpart: kth_network_settings (user_agent, protocol_maximum,
// services, relay_transactions) + kth_blockchain_settings.byte_fee_satoshis.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_network_info(method_context& ctx, request const& req) {
    // Omitted parameters and an empty array are the only accepted forms.
    if ( ! req.params.empty()) {
        auto const invalid_params = [] {
            return kth::fail(make_error(error_code::invalid_params,
                "getnetworkinfo takes no arguments"));
        };
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.params);
        simdjson::ondemand::document document;
        simdjson::ondemand::array args;
        if (parser.iterate(padded).get(document) != simdjson::SUCCESS ||
            document.get_array().get(args) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        for (auto element : args) {
            (void)element;
            co_return invalid_params();
        }
    }
    if (ctx.network == nullptr) {
        co_return kth::fail(make_error(error_code::internal_error,
            "network unavailable"));
    }
    // Plain reads, no hop to the network's executors: the settings are fixed
    // before the server starts, and the lifecycle and the connection count are
    // atomics. The P2P node outlives the server (full_node destroys the server
    // first).
    auto const& settings = ctx.network->network_settings();
    co_return render_network_info({
        .version = kth::version,
        .subversion = settings.user_agent,
        .protocol_version = settings.protocol_maximum,
        .services = settings.services,
        .relay = settings.relay_transactions,
        .connections = ctx.network->connection_count(),
        .active = ! ctx.network->stopped(),
        .byte_fee_satoshis = ctx.chain.chain_settings().byte_fee_satoshis});
}

// ---- mempool query methods -----------------------------------------------

// getrawmempool [verbose=false] -> the txids currently in the mempool; with
// verbose, an object keyed by txid with each transaction's entry.
// C-API counterpart: kth_chain_get_mempool_txids (+ kth_chain_get_mempool_entry).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_raw_mempool(method_context& ctx, request const& req) {
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "verbose must be a boolean"));
    };
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(req.params.empty() ? std::string_view{"[]"} : req.params);
    simdjson::ondemand::document doc;
    simdjson::ondemand::array params;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS ||
        doc.get_array().get(params) != simdjson::SUCCESS) {
        co_return invalid_params();
    }
    bool verbose = false;
    size_t count = 0;
    for (auto element : params) {
        simdjson::ondemand::value value;
        simdjson::ondemand::json_type type;
        if (count++ != 0 || element.get(value) != simdjson::SUCCESS ||
            value.type().get(type) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
        // A null optional argument retains the existing default.
        if (type != simdjson::ondemand::json_type::null &&
            value.get_bool().get(verbose) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
    }
    auto const txids = ctx.chain.get_mempool_txids();
    if ( ! verbose) {
        co_return render_hash_list(txids);
    }

    std::vector<mempool_listing> listings;
    listings.reserve(txids.size());
    for (auto const& txid : txids) {
        // A transaction can leave the pool between the two reads; it is then
        // simply not listed.
        auto const entry = ctx.chain.get_mempool_entry(txid);
        if ( ! entry) {
            continue;
        }
        listings.push_back({txid, entry->fee, entry->size, entry->time,
            ctx.chain.get_mempool_depends(txid), ctx.chain.get_mempool_spentby(txid)});
    }
    co_return render_raw_mempool_verbose(listings);
}

// getmempoolinfo -> mempool size, byte total, and fee total.
// C-API counterpart: kth_chain_get_mempool_info.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_info(method_context& ctx, request const& /*req*/) {
    co_return render_mempool_info(ctx.chain.get_mempool_info());
}

// getmempoolentry <txid> -> the entry's fee/size/time plus depends/spentby.
// C-API counterpart: kth_chain_get_mempool_entry.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_entry(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "getmempoolentry requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params, "invalid txid"));
    }
    auto const entry = ctx.chain.get_mempool_entry(*hash);
    if ( ! entry) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "transaction not in mempool"));
    }
    co_return render_mempool_entry(*entry,
        ctx.chain.get_mempool_depends(*hash), ctx.chain.get_mempool_spentby(*hash));
}

// getmempoolancestors <txid> -> in-mempool ancestor txids.
// C-API counterpart: kth_chain_get_mempool_ancestors.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_ancestors(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "getmempoolancestors requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params, "invalid txid"));
    }
    co_return render_hash_list(ctx.chain.get_mempool_ancestors(*hash));
}

// getmempooldescendants <txid> -> in-mempool descendant txids.
// C-API counterpart: kth_chain_get_mempool_descendants.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_descendants(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return kth::fail(make_error(error_code::invalid_params,
            "getmempooldescendants requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return kth::fail(make_error(error_code::invalid_params, "invalid txid"));
    }
    co_return render_hash_list(ctx.chain.get_mempool_descendants(*hash));
}

// Prepared method name / JSON result pairs, sorted by name.
using help_response = std::pair<std::string, std::string>;

// help [command] selects an immutable prepared result. The owning handler
// and request outlive this coroutine; no dispatcher or mutable table is read.
// C-API counterpart: none; it describes this server.
::asio::awaitable<std::expected<std::string, rpc_error>>
help(std::string const& listing_json, std::vector<help_response> const& responses,
     request const& req) {
    auto const invalid_params = [] {
        return kth::fail(make_error(error_code::invalid_params,
            "help requires [] or [command]"));
    };
    simdjson::ondemand::parser parser;
    auto const params = req.params.empty() ? std::string_view{"[]"} : std::string_view{req.params};
    simdjson::padded_string padded(params);
    simdjson::ondemand::document doc;
    simdjson::ondemand::array args;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS ||
        doc.get_array().get(args) != simdjson::SUCCESS) {
        co_return invalid_params();
    }
    std::string_view command;
    size_t count = 0;
    for (auto element : args) {
        if (count++ != 0) {
            co_return invalid_params();
        }
        simdjson::ondemand::value value;
        if (element.get(value) != simdjson::SUCCESS ||
            value.get_string().get(command) != simdjson::SUCCESS) {
            co_return invalid_params();
        }
    }

    if (command.empty()) {
        co_return listing_json;
    }
    auto const found = std::ranges::lower_bound(responses, command, {}, &help_response::first);
    if (found != responses.end() && found->first == command) {
        co_return found->second;
    }
    writer w;
    w.value(fmt::format("help: unknown command: {}", command));
    co_return w.str();
}

} // namespace

void register_builtin_methods(dispatcher& d) {
    d.add("getblockcount", get_block_count);
    d.add("getbestblockhash", get_best_block_hash);
    d.add("getblockhash", get_block_hash);
    d.add("getblockheader", get_block_header);
    d.add("getdifficulty", get_difficulty);
    d.add("getblockchaininfo", get_blockchain_info);
    d.add("getnetworkhashps", get_network_hash_ps);
    d.add("getchaintips", get_chain_tips);
    d.add("getnetworkinfo", get_network_info);
    d.add("getrawtransaction", get_raw_transaction);
    d.add("getblock", get_block);
    d.add("sendrawtransaction", send_raw_transaction);
    d.add("validateaddress", validate_address);
    d.add("decoderawtransaction", decode_raw_transaction);
    d.add("gettxout", get_tx_out);
    d.add("getrawmempool", get_raw_mempool);
    d.add("getmempoolinfo", get_mempool_info);
    d.add("estimatefee", estimate_fee);
    d.add("getmempoolentry", get_mempool_entry);
    d.add("getmempoolancestors", get_mempool_ancestors);
    d.add("getmempooldescendants", get_mempool_descendants);
    d.add("getblocktemplate", get_block_template);
    d.add("getblocktemplatelight", get_block_template_light);
    d.add("getmininginfo", get_mining_info);
    d.add("submitblock", submit_block);
    d.add("submitblocklight", submit_block_light);

    // Help MUST be the last method registered: capture the completed catalog,
    // including its own name, before any request can be served.
    auto names = d.methods();
    auto const position = std::ranges::lower_bound(names, std::string_view{"help"});
    if (position == names.end() || *position != "help") {
        names.insert(position, "help");
    }
    writer listing;
    listing.value(fmt::format("{}", fmt::join(names, "\n")));
    std::vector<help_response> responses;
    responses.reserve(names.size());
    for (auto const& name : names) {
        writer individual;
        individual.value(name);
        responses.emplace_back(name, individual.str());
    }
    d.add("help", [listing_json = listing.str(), responses = std::move(responses)]
        (method_context&, request const& req) {
        return help(listing_json, responses, req);
    });
}

} // namespace kth::node::rpc
