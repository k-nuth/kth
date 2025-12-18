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

#ifndef KTH_INFRASTRUCTURE_SEQUENCER_HPP
#define KTH_INFRASTRUCTURE_SEQUENCER_HPP

#include <functional>
#include <memory>
#include <queue>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>
#include <kth/infrastructure/utility/enable_shared_from_base.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

class sequencer : public enable_shared_from_base<sequencer> {
public:
    using ptr = std::shared_ptr<sequencer>;
    using action = std::function<void()>;
    using executor_type = ::asio::any_io_executor;

    explicit
    sequencer(executor_type executor);

    ~sequencer();

    void lock(action&& handler);
    void unlock();

private:
    executor_type executor_;

    // These are protected by mutex.
    bool executing_;
    std::queue<action> actions_;
    mutable shared_mutex mutex_;
};

} // namespace kth

#endif
