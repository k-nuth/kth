// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_CLIENT_HPP
#define KTH_NODE_SV2_JD_CLIENT_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include <kth/infrastructure/utility/broadcaster.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/sv2/jd_connection.hpp>
#include <kth/node/sv2/jd_declaration.hpp>
#include <kth/node/sv2/jd_failover.hpp>
#include <kth/node/sv2/jd_session.hpp>
#include <kth/node/sv2/jd_settings.hpp>

namespace kth::blockchain { struct block_chain; }

namespace kth::node::sv2 {

// One published template for downstream miners. `generation` advances only
// when the coinbase they should mine changes. Readers load the pointer and
// never wait on an upstream socket.
struct jd_notice {
    uint64_t generation = 0;
    bool pool = false;
    hash_digest previous{};
    mining::declared_work pooled{};
    blockchain::mining_template solo{};
};

// Own each Mining/JDS pair together. Only the active pair publishes payouts;
// one higher-priority pair may recover without disturbing downstream work.
struct jd_client {
    // settings have passed validate_jd_settings and solo payout validation.
    jd_client(sv2_settings const& settings, ::asio::any_io_executor executor,
        blockchain::block_chain* chain = nullptr);

    ::asio::awaitable<void> run();
    void stop();

    // Both protocol setups and the extended channel, or a terminal error.
    // Local listener readiness never waits for this result.
    [[nodiscard]] ::asio::awaitable<void> wait_until_ready();

    // Empty until an allocation has been published, or until fallback republishes
    // the same template for the solo payout. Safe to call off this client's strand.
    [[nodiscard]] boost::shared_ptr<jd_notice const> notice() const;
    [[nodiscard]] boost::shared_ptr<jd_status const> status() const;

    using publication_channel = broadcaster<int>::channel_type;

    // One buffered wake-up per subscriber, including the current publication.
    // Read notice() for the latest work; a slow miner never holds up the JDS.
    [[nodiscard]] std::shared_ptr<publication_channel> subscribe_publications();
    // Cancel a pending receive before unsubscribing, just as for chain events.
    void unsubscribe_publications(std::shared_ptr<publication_channel> const& channel);

    // Queue a share for the pool and, in full-template mode, for the JDS.
    // Returns without waiting for either socket. The block is organized by the caller.
    void observe_share(mining::pool_proof proof);

private:
    struct attempt;
    ::asio::awaitable<void> run_owned();
    ::asio::awaitable<void> run_attempt(std::shared_ptr<attempt> value);
    ::asio::awaitable<void> serve(attempt& value, jd_connection& connection,
        jd_session& session, jd_peer peer);
    ::asio::awaitable<void> declare_templates(attempt& value);
    void start_attempt(size_t index, bool probe);
    void fail(attempt& value, jd_failure kind, std::string reason);
    void update_status();
    void mark_ready(attempt& value);
    bool recover_at_boundary();
    bool dispatch(attempt& value, jd_declaration::result result);
    bool share_response(attempt& value, message_header const& header, byte_span payload);
    void publish_pool(attempt& value);
    void publish_solo(attempt const& value);
    void note_share(attempt& value, mining::pool_proof const& proof);
    void forward_share(attempt& value, mining::pool_proof const& proof);
    void remember_submitted(attempt& value, uint32_t sequence, uint32_t generation,
        hash_digest const& previous, std::chrono::steady_clock::time_point sent);
    // False when the JDS queue refuses the frame and this attempt is stopping.
    [[nodiscard]] bool push_block(attempt& value, mining::pool_proof const& proof);
    [[nodiscard]] bool flush_pending_solutions(attempt& value);
    void flush_cached(attempt& value);
    // Drop shares and unpushed block proofs from a declaration being replaced.
    void retire_cached(attempt& value);

    ::asio::strand<::asio::any_io_executor> owner_;
    sv2_settings const settings_;
    std::vector<sv2_settings::jd_upstream> const upstreams_;
    jd_failover policy_;
    std::shared_ptr<attempt> active_;
    std::shared_ptr<attempt> probe_;
    blockchain::block_chain* const chain_;
    std::chrono::seconds const response_timeout_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> ready_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> wake_;
    boost::atomic_shared_ptr<jd_notice> notice_;
    boost::atomic_shared_ptr<jd_status> status_;
    broadcaster<int> publications_;
    std::atomic_bool stopped_{false};
    bool published_ = false;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_CLIENT_HPP
