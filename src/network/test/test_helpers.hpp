// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_TEST_HELPERS_HPP
#define KTH_NETWORK_TEST_HELPERS_HPP

// Include the central test helpers from domain (which includes infrastructure)
#include "../../domain/test/test_helpers.hpp"

#include <chrono>

#include <asio/io_context.hpp>

#include <kth/network.hpp>

namespace kth::test {

/// Drain an io_context until all pending work completes or timeout is reached.
/// This ensures coroutines spawned with detached have time to complete and
/// release their resources, preventing memory leaks in tests.
/// @param ctx The io_context to drain
/// @param drain_time Time to run the context for cleanup
inline
void drain_context(::asio::io_context& ctx, std::chrono::milliseconds drain_time = std::chrono::milliseconds(50)) {
    ctx.restart();
    ctx.run_for(drain_time);
}

} // namespace kth::test

#endif // KTH_NETWORK_TEST_HELPERS_HPP
