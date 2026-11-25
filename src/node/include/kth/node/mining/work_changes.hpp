// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_MINING_WORK_CHANGES_HPP
#define KTH_NODE_MINING_WORK_CHANGES_HPP

#include <deque>
#include <memory>
#include <variant>

#include <asio/awaitable.hpp>
#include <asio/experimental/channel_select.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::mining {

// A channel that only wakes: nothing is read from what it carries.
using doorbell = ::asio::experimental::select_channel<std::monostate>;

// Tells each part of the node that serves mining work that the work changed:
// a new connected tip, or a mempool that is no longer the one a template was
// selected from.
//
// The chain rings one doorbell for its tip and one for its mempool, and a
// doorbell has a single consumer. This is that consumer. Each subscriber gets
// a doorbell of its own, rung on every change of either, so two servers never
// take each other's wake-up. A ring says that something moved, not what: the
// subscriber reads the chain again. Rings nobody has read yet are one ring.
struct work_changes {
    // A doorbell for one subscriber, its only consumer. Every subscription is
    // made before run() starts; the set is not touched after that.
    [[nodiscard]]
    doorbell& subscribe();

    // Pass the chain's rings on to every subscriber until stop().
    [[nodiscard]]
    ::asio::awaitable<void> run(blockchain::block_chain& chain);

    // Ends run(), also one that has not started yet. From any thread.
    void stop();

private:
    std::deque<doorbell> subscribers_;
    doorbell ends_{1};
};

// One subscriber's wake-up for everything of its own that waits for the mining
// work to change: the requests a server holds, or its connections. Closing a
// doorbell wakes everyone waiting on it at once, so each change closes the
// current one and puts a new one in its place. It belongs to its subscriber's
// strand: watched and rung from there only.
struct work_bell {
    // The doorbell the next change closes. A waiter takes it before reading
    // the state its wait depends on, so a change after that read finds it.
    [[nodiscard]]
    std::shared_ptr<doorbell> watch();

    // Wake every waiter that watches. With none watching there is no one to
    // wake and the doorbell stays.
    void ring();

private:
    std::shared_ptr<doorbell> current_ = std::make_shared<doorbell>(1);
    bool watched_ = false;
};

// Ring `bell` each time `changes` rings, until `ends` is closed. `changes` is
// a subscription to the node's work_changes, and this is its one consumer. It
// runs where `bell` belongs: the subscriber's strand.
[[nodiscard]]
::asio::awaitable<void> ring_on_work_changes(doorbell& changes, work_bell& bell,
    doorbell& ends);

} // namespace kth::node::mining

#endif // KTH_NODE_MINING_WORK_CHANGES_HPP
