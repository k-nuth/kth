// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_CAPNP_RPC_READER_HPP
#define KTH_NODE_CAPNP_RPC_READER_HPP

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <asio/awaitable.hpp>

#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/node/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

// The node-side contract of the Cap'n Proto RPC backend (schema:
// kth/rpc/v1/chain.capnp). These are plain values: nothing here mentions Cap'n
// Proto or KJ, and every function runs on the RPC owner's asio strand. A
// reader is the only thing the transport calls to touch chain state.

namespace kth::node::capnp_rpc {

// Mirrors kth.rpc.v1.ErrorCode (except unauthenticated, which is the
// transport's own).
enum class status {
    invalid_argument,
    not_found,
    resource_exhausted,
    unavailable,
    internal,
};

struct failure {
    status code;
    std::string message;
};

template <typename T>
using result = std::expected<T, failure>;

struct chain_info {
    std::uint64_t block_height;
    std::uint64_t header_height;
    hash_digest best_block_hash;   // internal byte order
};

struct block_data {
    std::uint64_t height;
    data_chunk wire;               // peer-to-peer block serialization
};

struct transaction_data {
    data_chunk wire;               // peer-to-peer transaction serialization
    // Absent: the transaction is unconfirmed (mempool).
    struct confirmed_position {
        std::uint64_t block_height;
        std::uint64_t index;
    };
    std::optional<confirmed_position> confirmed;
};

// The element order of what block_chain::fetch_transaction returns on success:
// (transaction, index within its block, block height). It is the order the C API
// relies on (kth_chain_sync_transaction); `index` and `height` are both size_t, so
// swapping them compiles silently. This is the one place that maps the tuple.
KND_API transaction_data to_transaction_data(
    std::tuple<std::shared_ptr<domain::message::transaction const>, std::size_t, std::size_t> const& confirmed);

// Typed asynchronous handlers. Implementations must honor asio cancellation
// where they await it; whatever they cannot interrupt finishes and its result is
// discarded, and the operation keeps its admission slot until then.
struct reader {
    virtual ~reader() = default;

    virtual ::asio::awaitable<result<chain_info>> get_chain_info() = 0;
    virtual ::asio::awaitable<result<block_data>> get_block(hash_digest const& hash) = 0;
    virtual ::asio::awaitable<result<transaction_data>> get_transaction(hash_digest const& hash) = 0;
};

// The production reader: the real blockchain APIs. The chain must outlive it.
struct KND_API chain_reader final : reader {
    explicit chain_reader(blockchain::block_chain& chain);

    ::asio::awaitable<result<chain_info>> get_chain_info() override;
    ::asio::awaitable<result<block_data>> get_block(hash_digest const& hash) override;
    ::asio::awaitable<result<transaction_data>> get_transaction(hash_digest const& hash) override;

private:
    blockchain::block_chain& chain_;
};

} // namespace kth::node::capnp_rpc

#endif // KTH_NODE_CAPNP_RPC_READER_HPP
