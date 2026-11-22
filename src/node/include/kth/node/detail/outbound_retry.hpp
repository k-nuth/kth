// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_OUTBOUND_RETRY_HPP_
#define KTH_NODE_DETAIL_OUTBOUND_RETRY_HPP_

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <boost/unordered/concurrent_flat_map.hpp>

#include <asio/ip/address.hpp>

#include <kth/infrastructure.hpp>

namespace kth::node::detail {

// When an address may be dialled again.
//
// An address that could not be reached waits a fixed time. One that was
// reached and whose session then ended at once, or was closed because the
// peer served nothing, waits longer each time that happens in a row: a peer that drops this node as soon as it is asked for
// something keeps doing so, and dialling it again every half minute spends an
// outbound slot on a handshake that leads nowhere.
//
// Nothing here is an offence. No score is touched and no address is banned;
// the address is only left alone for a while.
struct outbound_retry {
    using clock = std::chrono::steady_clock;
    using address = ::asio::ip::address;

    static constexpr std::chrono::seconds base_cooldown{30};
    static constexpr std::chrono::seconds max_cooldown{30 * 60};
    // A session at least this long counts as one that worked.
    static constexpr std::chrono::seconds short_session{5};
    // How long after its cooldown an address is still remembered, so that the
    // next short session finds the count of the ones before it.
    static constexpr std::chrono::seconds memory{60 * 60};

    // The wait after the `consecutive`-th short session in a row (from 1).
    [[nodiscard]]
    static constexpr std::chrono::seconds cooldown_after(uint32_t consecutive) noexcept {
        auto wait = base_cooldown;
        for (uint32_t i = 1; i < consecutive && wait < max_cooldown; ++i) {
            wait *= 2;
        }
        return std::min(wait, max_cooldown);
    }

    [[nodiscard]]
    bool cooling(address const& ip, clock::time_point now) const {
        bool result = false;
        entries_.cvisit(ip, [&](auto const& entry) { result = entry.second.until > now; });
        return result;
    }

    // The address did not answer, or refused.
    void connect_failed(address const& ip, clock::time_point now) {
        entries_.insert_or_visit({ip, entry{now + base_cooldown, 0}},
            [&](auto& existing) { existing.second.until = std::max(existing.second.until, now + base_cooldown); });
    }

    // An outbound session with the address ended after `lasted`.
    void session_ended(address const& ip, clock::duration lasted, clock::time_point now) {
        if (lasted >= short_session) {
            entries_.erase(ip);
            return;
        }
        session_unproductive(ip, now);
    }

    // An outbound session with the address was of no use, however long it
    // lasted: counted as a short one.
    void session_unproductive(address const& ip, clock::time_point now) {
        entries_.insert_or_visit({ip, entry{now + base_cooldown, 1}}, [&](auto& existing) {
            ++existing.second.short_sessions;
            existing.second.until = now + cooldown_after(existing.second.short_sessions);
        });
    }

    // How many short sessions in a row the address has had.
    [[nodiscard]]
    uint32_t short_sessions(address const& ip) const {
        uint32_t result = 0;
        entries_.cvisit(ip, [&](auto const& entry) { result = entry.second.short_sessions; });
        return result;
    }

    void forget_expired(clock::time_point now) {
        entries_.erase_if([&](auto const& entry) {
            return entry.second.until + (entry.second.short_sessions == 0 ? clock::duration::zero() : clock::duration(memory)) <= now;
        });
    }

private:
    struct entry {
        clock::time_point until;
        uint32_t short_sessions;
    };

    boost::concurrent_flat_map<address, entry, salted_ip_hasher> entries_;
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_OUTBOUND_RETRY_HPP_
