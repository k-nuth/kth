// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_STATUS_HPP
#define KTH_NETWORK_STATUS_HPP

#include <cstddef>
#include <asio/awaitable.hpp>

namespace kth::network {

struct status {
    bool active;
    size_t connections;
};

// An awaited query, not permission to access peer state. Implementations obtain
// the count on its owner executor; callers suspend until the reply arrives.
// Admission belongs to the caller: RPC admits at most 64 connections, each
// with one in-flight request. No additional polling task or queue is needed.
// The owner must outlive requests and keep its executor running until they join.
class status_source {
public:
    virtual ~status_source() = default;
    [[nodiscard]] virtual ::asio::awaitable<status> read() const = 0;
};

} // namespace kth::network

#endif // KTH_NETWORK_STATUS_HPP
