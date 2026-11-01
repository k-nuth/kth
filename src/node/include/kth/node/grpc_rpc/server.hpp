// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_GRPC_RPC_SERVER_HPP
#define KTH_NODE_GRPC_RPC_SERVER_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <kth/domain/config/network.hpp>
#include <kth/node/define.hpp>
#include <kth/node/grpc_rpc/transport.hpp>
#include <kth/node/settings.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::grpc_rpc {

/// Save `content` at `path` readable by the owner only, or fail without leaving
/// a partial or world-readable secret behind. The content goes to a new,
/// exclusively created temporary file in the same directory (never following a
/// link), is restricted to the owner BEFORE the secret is written, is written,
/// flushed and synced with every step checked, and then atomically replaces
/// `path`. A symbolic link already at `path` is replaced, not followed. On
/// Windows the owner-only mode bits do not exist; the file inherits the ACL of
/// its directory.
[[nodiscard]] KND_API std::expected<void, std::string> write_private_file(
    std::filesystem::path const& path, std::string_view content);

/// The node's gRPC server: node.proto served over a transport, answered from the
/// chain. Started when rpc.enabled is true and the build selected rpc=grpc.
///
/// Lifecycle (driven by full_node): start() binds and serves; stop() only asks
/// and never blocks; join() waits for every call to finish and must run before
/// the chain is stopped and closed, because in-flight calls still read it.
class KND_API server {
public:
    /// The settings are copied. Credentials come from rpc.user / rpc.password; when
    /// both are empty a random one is generated and written to ./.cookie (owner
    /// read/write only) as `__cookie__:<token>`, which clients send as the basic
    /// auth pair. The chain must outlive join(). Failing to save the cookie
    /// privately is a start() failure.
    server(rpc_settings const& settings, blockchain::block_chain& chain,
        domain::config::network network,
        std::filesystem::path cookie_path = ".cookie");

    [[nodiscard]]
    std::expected<std::uint16_t, std::string> start();
    void stop();
    void join();

private:
    rpc_settings settings_;
    std::filesystem::path cookie_path_;
    transport transport_;
};

} // namespace kth::node::grpc_rpc

#endif // KTH_NODE_GRPC_RPC_SERVER_HPP
