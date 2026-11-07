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
    };

    jd_session(jd_peer peer, std::string host, uint16_t port,
        std::string user_identity, bool full_template);

    // Single use. Empty means the session could not be started.
    [[nodiscard]] data_chunk start();
    [[nodiscard]] result process(message_header const& header, byte_span payload);
    [[nodiscard]] bool ready() const;
    // True only after RequestExtensions.Success lists Requested Payout Outputs.
    [[nodiscard]] bool payout_outputs() const;
    [[nodiscard]] bool awaiting_extensions() const;
    // RequestExtensions went unanswered. Return to idle so start() sends
    // SetupConnection again, and leave negotiation off: the next success is
    // ready and does not send RequestExtensions. Returns false unless that
    // wait was still open, so a mining timeout is unchanged.
    bool restart_without_extensions();
    [[nodiscard]] std::optional<open_extended_mining_channel_success> const& channel() const;

private:
    enum class state {
        idle, awaiting_setup, awaiting_extensions, awaiting_channel, ready, closed
    };

    [[nodiscard]] result fail(std::string error);
    [[nodiscard]] result process_setup(message_header const& header, byte_span payload);
    [[nodiscard]] result process_extensions(message_header const& header, byte_span payload);
    [[nodiscard]] result process_channel(message_header const& header, byte_span payload);
    [[nodiscard]] result process_mining(message_header const& header, byte_span payload);
    [[nodiscard]] bool matches_channel(uint32_t id) const;

    jd_peer peer_;
    std::string host_;
    uint16_t port_;
    std::string identity_;
    bool full_template_;
    state state_ = state::idle;
    // The first connection always asks. Cleared only for the one reconnect
    // that proceeds without extensions.
    bool negotiate_extensions_ = true;
    bool payout_outputs_ = false;
    std::string error_;
    std::optional<open_extended_mining_channel_success> channel_;
};

} // namespace kth::node::sv2

#endif
