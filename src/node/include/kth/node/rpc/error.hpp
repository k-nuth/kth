// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_ERROR_HPP
#define KTH_NODE_RPC_ERROR_HPP

#include <string>
#include <utility>

#include <kth/infrastructure/error.hpp>

namespace kth::node::rpc {

// JSON-RPC error codes are a distinct, EXTERNAL protocol namespace: the
// negative-thousands values are fixed by JSON-RPC / Bitcoin Core / BCHN and are
// what miner and wallet tooling matches on the wire. They deliberately do not
// live in the system-wide `kth::code` enum (which stays an internal concern);
// internal failures are bridged in via from_code() below.
enum class error_code : int {
    invalid_request  = -32600,
    method_not_found = -32601,
    invalid_params   = -32602,
    internal_error   = -32603,
    parse_error      = -32700,
    // Application-level (Bitcoin RPC_MISC_ERROR family).
    misc_error       = -1,
};

// A JSON-RPC error carried as data (not thrown): handlers return
// std::expected<std::string, rpc_error>, so the error path stays exception-free.
struct rpc_error {
    int code;
    std::string message;
};

// Build an rpc_error from a well-known JSON-RPC code.
inline rpc_error make_error(error_code code, std::string message) {
    return {static_cast<int>(code), std::move(message)};
}

// Bridge a system error (kth::code) to a JSON-RPC error at the RPC boundary: an
// internal failure surfaces as internal_error carrying the system message.
inline rpc_error from_code(kth::code const& ec) {
    return {static_cast<int>(error_code::internal_error), ec.message()};
}

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_ERROR_HPP
