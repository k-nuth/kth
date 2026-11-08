// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_SESSION_HPP
#define KTH_NODE_SV2_JD_SESSION_HPP

#include <cstdint>
#include <optional>
#include <string>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/mining/work.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/mining_messages.hpp>

namespace kth::node::sv2 {

enum class jd_peer { mining, declaration };

// One upstream's setup and channel state. Socket ownership and deadlines stay
// with the connection; this state only consumes complete authenticated frames.
struct jd_session {
    static constexpr uint32_t max_payload_size = 1024 * 1024;
    static constexpr uint16_t min_extranonce_size = mining::jd_min_extranonce_size;

    struct result {
        data_chunk response;
        bool close = false;
        bool ready = false;
        std::string error;
        bool incompatible = false;
    };

    jd_session(jd_peer peer, std::string host, uint16_t port,
        std::string user_identity, bool full_template);

    // Single use. Empty means the session could not be started.
    [[nodiscard]] data_chunk start();
    [[nodiscard]] result process(message_header const& header, byte_span payload);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::optional<open_extended_mining_channel_success> const& channel() const;

    // A Mining session sends RequestExtensions for Worker-Specific Hashrate
    // Tracking (0x0002) after SetupConnection.Success and opens the channel
    // once it is answered. Call before start() to open it without asking: the
    // previous connection to this pool got no answer.
    void skip_extensions();
    // RequestExtensions is unanswered and no channel request has been sent.
    [[nodiscard]] bool awaiting_extensions() const;
    // The pool granted 0x0002: each SubmitSharesExtended carries the worker.
    [[nodiscard]] bool worker_identities() const;

    // The downstream channels' summed nominal hash rate (sv2-spec 5.3.2), zero
    // until a miner connects. The channel request carries the value noted last.
    void note_hash_rate(float value);
    // UpdateChannel once the channel is open and the noted rate is not the one
    // the pool last received. It repeats the max_target the channel opened with.
    [[nodiscard]] std::optional<update_channel> hash_rate_update();

private:
    enum class state { idle, awaiting_setup, awaiting_extensions, awaiting_channel, ready, closed };

    [[nodiscard]] result fail(std::string error, bool incompatible = false);
    [[nodiscard]] result process_setup(message_header const& header, byte_span payload);
    [[nodiscard]] result process_extensions(message_header const& header, byte_span payload);
    [[nodiscard]] result open_channel();
    [[nodiscard]] data_chunk request_worker_identities();
    [[nodiscard]] result process_channel(message_header const& header, byte_span payload);
    [[nodiscard]] result process_mining(message_header const& header, byte_span payload);
    [[nodiscard]] bool matches_channel(uint32_t id) const;

    jd_peer peer_;
    std::string host_;
    uint16_t port_;
    std::string identity_;
    bool full_template_;
    state state_ = state::idle;
    std::string error_;
    std::optional<open_extended_mining_channel_success> channel_;
    float hash_rate_ = 0;
    // Sent in the channel request or the last UpdateChannel.
    float reported_hash_rate_ = 0;
    // Cleared only by skip_extensions(). A declaration session never asks.
    bool negotiate_extensions_ = true;
    // A RequestExtensions is outstanding. Only the latest request_id answers it.
    bool extensions_pending_ = false;
    uint16_t extensions_request_ = 0;
    bool worker_identities_ = false;
};

} // namespace kth::node::sv2

#endif
