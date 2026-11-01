// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/grpc_rpc/chain_handlers.hpp>

#include <algorithm>
#include <string_view>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::node::grpc_rpc {

namespace {

namespace v1 = ::kth::rpc::v1;

using reader_ptr = std::shared_ptr<chain_reader const>;

// The chain-facing halves. They run on the chain executor, take their inputs by
// value (they are coroutines: a reference parameter could dangle) and return
// plain data, so no chain object crosses back to the transport thread.
::asio::awaitable<result<chain_tip>> read_tip(reader_ptr reader) {
    co_return reader->tip();
}

::asio::awaitable<result<confirmed_block>> read_block(reader_ptr reader, hash_digest hash) {
    co_return co_await reader->block(hash);
}

::asio::awaitable<result<found_transaction>> read_transaction(reader_ptr reader, hash_digest hash) {
    co_return co_await reader->transaction(hash);
}

template <typename Awaitable>
auto on_chain(::asio::any_io_executor const& executor, Awaitable&& work) {
    return ::asio::co_spawn(executor, std::forward<Awaitable>(work), ::asio::use_awaitable);
}

} // namespace

::kth::rpc::v1::Network to_proto(domain::config::network network) {
    using domain::config::network;
    switch (network) {
        case network::mainnet: return v1::NETWORK_MAINNET;
        case network::testnet: return v1::NETWORK_TESTNET3;
        case network::regtest: return v1::NETWORK_REGTEST;
#if defined(KTH_CURRENCY_BCH)
        case network::testnet4: return v1::NETWORK_TESTNET4;
        case network::scalenet: return v1::NETWORK_SCALENET;
        case network::chipnet: return v1::NETWORK_CHIPNET;
#endif
    }
    return v1::NETWORK_UNSPECIFIED;
}

rpc_status to_status(kth::code const& ec, std::string_view what) {
    auto message = std::string(what) + ": " + ec.message();
    if (ec == error::not_found) {
        return {status_code::not_found, std::move(message)};
    }
    if (ec == error::service_stopped) {
        return {status_code::unavailable, std::move(message)};
    }
    return {status_code::internal, std::move(message)};
}

rpc_status to_status(database::result_code code, std::string_view what) {
    auto message = std::string(what);
    if (code == database::result_code::key_not_found) {
        return {status_code::not_found, message + ": not found"};
    }
    return {status_code::internal, message + ": storage error " + std::to_string(static_cast<int>(code))};
}

result<hash_digest> to_hash(std::string const& bytes, std::string_view field) {
    hash_digest hash;
    if (bytes.size() != hash.size()) {
        return std::unexpected(rpc_status{status_code::invalid_argument,
            std::string(field) + " must be exactly 32 bytes (internal byte order), got " +
            std::to_string(bytes.size())});
    }
    std::copy(bytes.begin(), bytes.end(), hash.begin());
    return hash;
}

chain_reader make_chain_reader(blockchain::block_chain& chain) {
    return make_chain_reader<blockchain::block_chain>(chain);
}

handlers make_chain_handlers(chain_reader reader, v1::Network network,
    ::asio::any_io_executor chain_executor) {

    auto const shared = std::make_shared<chain_reader const>(std::move(reader));
    handlers out;

    out.get_chain_info = [shared, network, chain_executor](v1::GetChainInfoRequest const&)
            -> ::asio::awaitable<result<v1::GetChainInfoResponse>> {
        auto const tip = co_await on_chain(chain_executor, read_tip(shared));
        if ( ! tip) {
            co_return std::unexpected(tip.error());
        }
        v1::GetChainInfoResponse response;
        response.set_network(network);
        response.set_block_height(tip->block_height);
        response.set_header_height(tip->header_height);
        response.set_best_block_hash(std::string(
            reinterpret_cast<char const*>(tip->best_block_hash.data()), tip->best_block_hash.size()));
        co_return response;
    };

    out.get_block = [shared, chain_executor](v1::GetBlockRequest const& request)
            -> ::asio::awaitable<result<v1::GetBlockResponse>> {
        auto const hash = to_hash(request.hash(), "hash");
        if ( ! hash) {
            co_return std::unexpected(hash.error());
        }
        auto const found = co_await on_chain(chain_executor, read_block(shared, *hash));
        if ( ! found) {
            co_return std::unexpected(found.error());
        }
        // The stored wire bytes, copied into the message on the transport thread so
        // a large block does not occupy the chain executor.
        v1::GetBlockResponse response;
        response.set_raw_block(std::string(
            reinterpret_cast<char const*>(found->raw->data()), found->raw->size()));
        response.set_height(found->height);
        co_return response;
    };

    out.get_transaction = [shared, chain_executor](v1::GetTransactionRequest const& request)
            -> ::asio::awaitable<result<v1::GetTransactionResponse>> {
        auto const hash = to_hash(request.hash(), "hash");
        if ( ! hash) {
            co_return std::unexpected(hash.error());
        }
        auto const found = co_await on_chain(chain_executor, read_transaction(shared, *hash));
        if ( ! found) {
            co_return std::unexpected(found.error());
        }
        auto const wire = to_data_chunk(
            static_cast<domain::chain::transaction const&>(*found->transaction), /*wire*/ true);
        v1::GetTransactionResponse response;
        response.set_raw_transaction(std::string(reinterpret_cast<char const*>(wire.data()), wire.size()));
        if (found->confirmed_at) {
            auto* confirmation = response.mutable_confirmation();
            confirmation->set_block_height(found->confirmed_at->first);
            confirmation->set_index_in_block(found->confirmed_at->second);
        }
        co_return response;
    };

    return out;
}

} // namespace kth::node::grpc_rpc
