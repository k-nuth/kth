// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// ============================================================================
// DEPRECATED - This file is scheduled for removal
// ============================================================================
// This file is part of the legacy P2P implementation that is being replaced
// by modern C++23 coroutines and Asio. See doc/asio.md for migration details.
//
// DO NOT USE THIS FILE IN NEW CODE.
// Replacement: Use p2p_node.hpp, peer_session.hpp, protocols_coro.hpp
// ============================================================================

#include <kth/infrastructure/deprecated/utility/work.hpp>

#include <memory>
#include <string>
#include <utility>


namespace kth {

work::work(threadpool& pool, std::string const& name)
    : name_(name)
    , executor_(pool.executor())
    , strand_(::asio::make_strand(executor_))
    , sequence_(executor_)
{}

} // namespace kth
