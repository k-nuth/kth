// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_GRPC_RPC_CHAIN_HANDLERS_HPP
#define KTH_NODE_GRPC_RPC_CHAIN_HANDLERS_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/database/databases/result_code.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/node/define.hpp>
#include <kth/node/grpc_rpc/transport.hpp>

// The typed API (node.proto) on top of the blockchain's own query methods.
//
// Threading. A handler resumes on the transport's thread, validates the request
// there (a bad hash needs no chain), and then hops: the chain work is co_spawned
// onto the chain's executor and awaited, so the answer comes back to the
// transport thread and the chain executor never waits on gRPC. That executor is
// a pool, not a strand, so the reader uses only what the chain makes safe to
// read from any thread: the published chain view, the header index and the
// fetch_* queries. The number of hops in flight is bounded by the transport's
// max_in_flight, block bodies by max_block_reads, and every hop is owned by the
// handler awaiting it, which the transport joins before the chain is closed.

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::grpc_rpc {

using transaction_ptr = std::shared_ptr<domain::message::transaction const>;

struct chain_tip {
    uint64_t block_height;
    uint64_t header_height;
    hash_digest best_block_hash;
};

/// A block body exactly as stored: the consensus wire bytes. Shared and const so
/// the chain-pool thread that read it can hand it to the transport thread.
struct confirmed_block {
    std::shared_ptr<data_chunk const> raw;
    uint64_t height;
};

struct found_transaction {
    transaction_ptr transaction;
    /// Empty when the transaction is only in the mempool.
    std::optional<std::pair<uint64_t, uint64_t>> confirmed_at;   // {height, index}
};

/// What the handlers need from a chain, already expressed in API terms (errors
/// are rpc_status, not kth::code). Everything here runs on the chain executor.
/// A real chain is adapted by make_chain_reader(); tests supply their own.
struct chain_reader {
    std::function<result<chain_tip>()> tip;
    std::function<::asio::awaitable<result<confirmed_block>>(hash_digest const&)> block;
    std::function<::asio::awaitable<result<found_transaction>>(hash_digest const&)> transaction;
};

/// The node's network as the API enum.
KND_API ::kth::rpc::v1::Network to_proto(domain::config::network network);

/// Chain error -> API status. `what` names the operation for the message.
KND_API rpc_status to_status(kth::code const& ec, std::string_view what);

/// Storage error -> API status.
KND_API rpc_status to_status(database::result_code code, std::string_view what);

/// Exactly 32 bytes become a hash, in the order given (no reversal).
KND_API result<hash_digest> to_hash(std::string const& bytes, std::string_view field);

/// Adapt any type with the block_chain query methods. A template so the logic
/// (checking a body against the active chain, falling back to the mempool) is
/// the code that runs in production and is also what the tests exercise.
template <typename Chain>
chain_reader make_chain_reader(Chain& chain) {
    chain_reader reader;

    reader.tip = [&chain]() -> result<chain_tip> {
        // One publication: the connected tip's height and hash are read together,
        // never a hash beside a height it does not belong to.
        auto const view = chain.chain_view();
        if ( ! view) {
            return std::unexpected(rpc_status{status_code::unavailable,
                "the chain has not published a tip yet"});
        }
        auto const headers = chain.headers().active_tip_height();
        auto const block_height = uint64_t(view->connected_tip_height);
        return chain_tip{block_height, std::max(block_height, uint64_t(std::max(headers, 0))),
            view->tip_hash};
    };

    // The by-hash lookup `fetch_block(hash)` is a stub on this chain, so the block
    // is found the way the chain itself serves bodies: hash -> height -> the
    // ACTIVE chain's flat-file body at that height. The disk read runs on the
    // chain's priority pool, not on the chain executor or a network thread.
    // Nothing is taken on trust across the read: the bytes must hash to what was
    // asked for (a side branch or a block that moved fails here), and the active
    // chain must still say the same afterwards (a reorg during the read).
    reader.block = [&chain](hash_digest const& hash) -> ::asio::awaitable<result<confirmed_block>> {
        auto const height = co_await chain.fetch_block_height(hash);
        if ( ! height) {
            co_return std::unexpected(to_status(height.error(), "block lookup"));
        }
        auto const at = uint32_t(*height);
        co_return co_await ::asio::co_spawn(chain.thread_pool().get_executor(),
            [&chain, hash, at]() -> ::asio::awaitable<result<confirmed_block>> {
                auto raw = chain.fetch_blocks_raw(at, at);
                if ( ! raw) {
                    co_return std::unexpected(to_status(raw.error(), "block body"));
                }
                if (raw->size() != 1 || raw->front().size() < 80) {
                    co_return std::unexpected(rpc_status{status_code::internal,
                        "block body: the store returned an unexpected record"});
                }
                auto const& body = raw->front();
                if (bitcoin_hash(byte_span(body.data(), 80)) != hash) {
                    co_return std::unexpected(rpc_status{status_code::not_found,
                        "block body: that block is not on the active chain"});
                }
                auto const& headers = chain.headers();
                auto const active = headers.active_at(int32_t(at));
                if (active == headers.null_index || headers.get_hash(active) != hash) {
                    co_return std::unexpected(rpc_status{status_code::aborted,
                        "block body: the chain reorganized while it was read; retry"});
                }
                co_return confirmed_block{
                    std::make_shared<data_chunk const>(std::move(raw->front())), at};
            },
            ::asio::use_awaitable);
    };

    reader.transaction = [&chain](hash_digest const& hash)
            -> ::asio::awaitable<result<found_transaction>> {
        auto confirmed = co_await chain.fetch_transaction(hash, /*require_confirmed*/ true);
        if (confirmed) {
            // Tuple order is (transaction, index in block, block height), as the C API reads it.
            auto& [tx, index, height] = *confirmed;
            co_return found_transaction{std::move(tx),
                std::pair{uint64_t(height), uint64_t(index)}};
        }
        if (confirmed.error() != error::not_found) {
            co_return std::unexpected(to_status(confirmed.error(), "transaction lookup"));
        }
        auto pooled = co_await chain.fetch_unconfirmed_transaction(hash);
        if ( ! pooled && pooled.error() == error::not_found) {
            // The confirmed lookup needs a transaction index the chain does not
            // keep yet (#491): say so, rather than claim the transaction is unknown.
            co_return std::unexpected(rpc_status{status_code::not_found,
                "transaction lookup: not in the mempool; confirmed transactions are not "
                "indexed by this node yet"});
        }
        if ( ! pooled) {
            co_return std::unexpected(to_status(pooled.error(), "mempool lookup"));
        }
        co_return found_transaction{std::move(*pooled), std::nullopt};
    };

    return reader;
}

/// The production reader over a real chain.
KND_API chain_reader make_chain_reader(blockchain::block_chain& chain);

/// Block bodies read at once by default: each can be tens of megabytes, read
/// on the chain's pool, which validation shares.
inline constexpr size_t default_max_block_reads = 2;

/// Build the three typed handlers. `chain_executor` is where `reader` must run.
/// More than `max_block_reads` GetBlock calls at once are refused with
/// RESOURCE_EXHAUSTED.
KND_API handlers make_chain_handlers(chain_reader reader, ::kth::rpc::v1::Network network,
    ::asio::any_io_executor chain_executor, size_t max_block_reads = default_max_block_reads);

} // namespace kth::node::grpc_rpc

#endif // KTH_NODE_GRPC_RPC_CHAIN_HANDLERS_HPP
