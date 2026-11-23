// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/blockchain/detail/` is excluded from the install rules, so nothing here is
// part of the surface a consumer can reach. `block_chain` names this type as a
// friend and nothing else.

#ifndef KTH_BLOCKCHAIN_DETAIL_BLOCK_CHAIN_INTERNAL_HPP_
#define KTH_BLOCKCHAIN_DETAIL_BLOCK_CHAIN_INTERNAL_HPP_

#include <cstdint>
#include <mutex>

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::blockchain::detail {

/// The two pieces of block_chain state the node owns rather than the chain: the
/// hydration phase, which the start path ends once the header index exists, and
/// the header-persistence cursor, which the single writer moves.
///
/// Both are invariants, not settings. Public on an installed header they would be
/// something a consumer could break from outside -- ending hydration mid-run, or
/// claiming a range durable that nobody wrote. Here they are reachable by the
/// node's own persistence path and by the controls, and by nothing else.
struct block_chain_internal {
    static void end_hydration(block_chain& chain) {
        chain.end_hydration();
    }

    [[nodiscard]]
    static std::mutex& persist_mutex(block_chain& chain) {
        return chain.header_persist_mutex();
    }

    static void set_persisted_through(block_chain& chain, uint32_t height) {
        chain.set_headers_persisted_through(height);
    }

    /// The cursor, read WITHOUT the lock. Only for a caller already inside the
    /// critical section -- taking it again there would deadlock.
    [[nodiscard]]
    static uint32_t persisted_through_locked(block_chain const& chain) {
        return chain.headers_persisted_through();
    }

    /// The cursor, read under the lock. For anyone outside the critical section,
    /// which is every reader that is not the writer itself: the value is a
    /// uint32_t the writer mutates, so an unsynchronised read of it is a race
    /// whatever is done with the answer.
    [[nodiscard]]
    static uint32_t persisted_through(block_chain& chain) {
        std::lock_guard<std::mutex> const guard(chain.header_persist_mutex());
        return chain.headers_persisted_through();
    }
};

/// Make the ABLA lookup fail with something other than `key_not_found`.
///
/// The distinction under test cannot be produced otherwise: an absent row is
/// easy to arrange and is the ordinary case, while a read that FAILS needs a
/// broken store. Without it, "absence" and "failure" cannot be told apart by a
/// control, and flattening the two is the defect being guarded against.
void fail_abla_lookup(bool enabled);

/// Whether the ABLA lookup is currently made to fail.
[[nodiscard]] bool abla_lookup_faulted();

} // namespace kth::blockchain::detail

#endif // KTH_BLOCKCHAIN_DETAIL_BLOCK_CHAIN_INTERNAL_HPP_
