// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Consumer of the installed rpc=capnproto package: public headers, the generated
// schema header and the exported (KND_API) symbols must all be usable.

#include <cstdio>
#include <exception>   // asio/thread_pool.hpp relies on it being included first
#include <memory>
#include <tuple>

#include <asio/thread_pool.hpp>
#include <capnp/message.h>

#include <kth/node/capnp_rpc/reader.hpp>
#include <kth/node/capnp_rpc/server.hpp>
#include <kth/rpc/v1/chain.capnp.h>

namespace {

struct empty_reader final : kth::node::capnp_rpc::reader {
    ::asio::awaitable<kth::node::capnp_rpc::result<kth::node::capnp_rpc::chain_info>> get_chain_info() override {
        co_return std::unexpected(kth::node::capnp_rpc::failure{kth::node::capnp_rpc::status::unavailable, ""});
    }
    ::asio::awaitable<kth::node::capnp_rpc::result<kth::node::capnp_rpc::block_data>> get_block(kth::hash_digest const&) override {
        co_return std::unexpected(kth::node::capnp_rpc::failure{kth::node::capnp_rpc::status::unavailable, ""});
    }
    ::asio::awaitable<kth::node::capnp_rpc::result<kth::node::capnp_rpc::transaction_data>> get_transaction(kth::hash_digest const&) override {
        co_return std::unexpected(kth::node::capnp_rpc::failure{kth::node::capnp_rpc::status::unavailable, ""});
    }
};

} // namespace

int main() {
    // Generated schema code links.
    capnp::MallocMessageBuilder message;
    auto request = message.initRoot<kth::rpc::v1::BlockResult>();
    request.initError().setCode(kth::rpc::v1::ErrorCode::NOT_FOUND);

    // Exported server class: construct, stop before run, destroy.
    asio::thread_pool pool(1);
    kth::node::rpc_settings settings;
    empty_reader reader;
    kth::node::capnp_rpc::server server(settings, reader, pool.get_executor());
    server.stop();

    // Exported free function.
    auto const tx = std::make_shared<kth::domain::message::transaction const>();
    auto const mapped = kth::node::capnp_rpc::to_transaction_data(std::make_tuple(tx, std::size_t{1}, std::size_t{2}));
    pool.join();
    return mapped.confirmed && mapped.confirmed->index == 1 && mapped.confirmed->block_height == 2 ? 0 : 1;
}
