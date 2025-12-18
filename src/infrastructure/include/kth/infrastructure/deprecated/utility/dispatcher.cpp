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

#include <kth/infrastructure/deprecated/utility/dispatcher.hpp>

#include <memory>
#include <string>

#if ! defined(__EMSCRIPTEN__)

#include <kth/infrastructure/deprecated/utility/work.hpp>

#endif // ! defined(__EMSCRIPTEN__)

namespace kth {

#if ! defined(__EMSCRIPTEN__)

dispatcher::dispatcher(threadpool& pool, std::string const& name)
    : heap_(std::make_shared<work>(pool, name)), pool_(pool)
{}

#else

dispatcher::dispatcher(threadpool&, std::string const&) {}

#endif // ! defined(__EMSCRIPTEN__)

////size_t dispatcher::ordered_backlog()
////{
////    return heap_->ordered_backlog();
////}
////
////size_t dispatcher::unordered_backlog()
////{
////    return heap_->unordered_backlog();
////}
////
////size_t dispatcher::concurrent_backlog()
////{
////    return heap_->concurrent_backlog();
////}
////
////size_t dispatcher::combined_backlog()
////{
////    return heap_->combined_backlog();
////}

} // namespace kth
