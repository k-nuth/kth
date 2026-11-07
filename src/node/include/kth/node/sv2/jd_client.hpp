// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_CLIENT_HPP
#define KTH_NODE_SV2_JD_CLIENT_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
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

// Own both upstreams together. A refusal or disconnect closes the other one;
// the listener and its solo mining sessions have independent lifetimes.
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
    ::asio::awaitable<void> run_owned();
    ::asio::awaitable<void> serve(jd_connection& connection, jd_session& session,
        char const* name);
    ::asio::awaitable<void> declare_templates();
    void wake_declarations();
    bool dispatch(jd_declaration::result result);
    void publish_pool();
    void publish_solo();
    void note_share(mining::pool_proof const& proof);
    void forward_share(mining::pool_proof const& proof);
    void remember_submitted(uint32_t sequence, uint32_t generation, bool block);
    // A share reply names one submission. Success changes nothing. An error
    // falls back only when that submission is still the accepted declaration
    // (sv2-spec 6.2). nullopt leaves the mining connection up; a string closes it.
    [[nodiscard]] std::optional<std::string> share_reply(message_header const& header,
        byte_span payload);
    // False when the JDS queue refuses the frame and this client is stopping.
    [[nodiscard]] bool push_block(mining::pool_proof const& proof);
    [[nodiscard]] bool flush_pending_solutions();
    void flush_cached();
    // Drop shares and unpushed block proofs from a declaration being replaced.
    void retire_cached();

    ::asio::strand<::asio::any_io_executor> owner_;
    jd_connection mining_;
    jd_connection declaration_;
    jd_session mining_session_;
    jd_session declaration_session_;
    jd_declaration jobs_;
    blockchain::block_chain* const chain_;
    std::chrono::seconds const response_timeout_;
    std::optional<std::chrono::steady_clock::time_point> job_deadline_;
    ::asio::steady_timer declaration_wake_;
    // Installed and cleared by run_owned(), and invoked only on owner_.
    // Client stop has to finish a parked receive. The channel is created on
    // owner_, so the close notice is posted there rather than on the chain
    // executor that stop() then shuts down.
    std::function<void()> cancel_tips_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> ready_;
    boost::atomic_shared_ptr<jd_notice> notice_;
    broadcaster<int> publications_;
    // Proofs found before SetCustomMiningJob.Success. Each proof's generation
    // is the declaration it was mined on; a replacement drops the vector.
    std::vector<mining::pool_proof> cached_;
    // sequence_number and declared_work::generation of each SubmitSharesExtended
    // this channel sent. Bounded so a slow pool cannot pin every share. A reply
    // for a sequence that has aged out is ignored, same as one never sent.
    struct submitted_share {
        uint32_t sequence = 0;
        uint32_t generation = 0;
        // The share also met the network target, so its block ends this job.
        bool block = false;
    };
    std::deque<submitted_share> submitted_;
    // Full-template blocks found before DeclareMiningJob.Success. Sent once
    // can_push_solution() is true for that generation. Two covers a pair found
    // on the same coinbase; a later one is dropped rather than queued forever.
    std::vector<mining::pool_proof> pending_solutions_;
    uint32_t share_sequence_ = 0;
    std::atomic_bool stopped_{false};
    bool published_ = false;
    bool pool_live_ = false;
    bool declaration_changed_ = false;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_CLIENT_HPP
