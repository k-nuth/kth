// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_session.hpp>

#include <algorithm>
#include <utility>

#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/jd_messages.hpp>
#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

constexpr uint32_t channel_request_id = 1;
constexpr uint16_t extensions_request_id = 1;
constexpr uint32_t jd_flag_declare_tx_data = 1u << 0;

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{framed_extension_type<Message>(),
        Message::message_type, static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

template <typename Message>
std::optional<Message> parse(byte_span payload) {
    byte_reader source(payload);
    auto message = Message::from_data(source);
    if ( ! message || ! source.is_exhausted()) return std::nullopt;
    return std::move(*message);
}

bool nonzero(hash_digest const& target) {
    return std::any_of(target.begin(), target.end(), [](uint8_t byte) { return byte != 0; });
}

std::string printable_error_code(std::string const& code) {
    // Upstream text reaches the log. Keep it on one bounded ASCII line.
    auto text = code.substr(0, 64);
    for (auto& character : text) {
        auto const byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7e) character = '?';
    }
    return text;
}

} // namespace

jd_session::jd_session(jd_peer peer, std::string host, uint16_t port,
    std::string user_identity, bool full_template)
    : peer_(peer)
    , host_(std::move(host))
    , port_(port)
    , identity_(std::move(user_identity))
    , full_template_(full_template)
{}

data_chunk jd_session::start() {
    if (state_ != state::idle) {
        (void)fail("upstream session already started");
        return {};
    }
    if (host_.empty() || host_.size() > b0_255_max || port_ == 0
        || identity_.empty() || identity_.size() > b0_255_max) {
        (void)fail("invalid upstream session settings");
        return {};
    }
    setup_connection setup;
    setup.protocol = peer_ == jd_peer::mining
        ? setup_connection::protocol_mining : setup_connection::protocol_job_declaration;
    setup.min_version = 2;
    setup.max_version = 2;
    // The downstream engine permits version rolling, so a fixed-version pool
    // cannot account for the same work without changing its search space.
    setup.flags = peer_ == jd_peer::mining
        ? mining_flag_requires_work_selection | mining_flag_requires_version_rolling
        : full_template_ ? jd_flag_declare_tx_data : 0;
    setup.endpoint_host = host_;
    setup.endpoint_port = port_;
    setup.vendor = "Knuth";
    state_ = state::awaiting_setup;
    return frame(setup);
}

bool jd_session::ready() const {
    return state_ == state::ready;
}

bool jd_session::payout_outputs() const {
    return payout_outputs_;
}

bool jd_session::awaiting_extensions() const {
    return state_ == state::awaiting_extensions;
}

bool jd_session::abandon_extensions() {
    if (state_ != state::awaiting_extensions) return false;
    payout_outputs_ = false;
    state_ = state::ready;
    return true;
}

std::optional<open_extended_mining_channel_success> const& jd_session::channel() const {
    return channel_;
}

jd_session::result jd_session::fail(std::string error) {
    state_ = state::closed;
    channel_.reset();
    error_ = std::move(error);
    return {{}, true, false, error_};
}

jd_session::result jd_session::process_setup(message_header const& header, byte_span payload) {
    if (header.channel_msg()) return fail("invalid upstream setup frame");
    if (header.msg_type == setup_connection_error::message_type) {
        auto const error = parse<setup_connection_error>(payload);
        if ( ! error) return fail("malformed upstream setup error");
        return fail("upstream refused setup (" + printable_error_code(error->error_code) + ")");
    }
    if (header.msg_type != setup_connection_success::message_type) {
        return fail("unexpected upstream setup message");
    }
    auto const success = parse<setup_connection_success>(payload);
    if ( ! success || success->used_version != 2) return fail("invalid upstream setup version");
    uint32_t const allowed = peer_ == jd_peer::mining ? mining_flag_requires_extended_channels : 0;
    if ((success->flags & ~allowed) != 0) return fail("unsupported upstream setup flags");
    if (peer_ == jd_peer::declaration) {
        // Negotiation has to finish, or time out, before any Job Declaration
        // message. Silence is not a pool refusal: the caller abandons the wait.
        request_extensions request;
        request.request_id = extensions_request_id;
        request.requested_extensions.push_back(requested_payout_outputs_extension);
        state_ = state::awaiting_extensions;
        payout_outputs_ = false;
        return {frame(request), false, false, {}};
    }
    open_extended_mining_channel request;
    request.request_id = channel_request_id;
    request.user_identity = identity_;
    request.max_target.fill(0xff);
    request.min_extranonce_size = min_extranonce_size;
    state_ = state::awaiting_channel;
    return {frame(request), false, false, {}};
}

jd_session::result jd_session::process_extensions(message_header const& header, byte_span payload) {
    if (header.extension() != extensions_negotiation_type) {
        // A base message before negotiation finishes is out of order. Any other
        // extension is not this request's answer and must not end the wait.
        if (header.extension() == 0 || header.channel_msg()) {
            return fail("unexpected message during extension negotiation");
        }
        return {{}, false, false, {}};
    }
    if (header.channel_msg()) return fail("invalid extension negotiation frame");
    if (header.msg_type == request_extensions_success::message_type) {
        auto const success = parse<request_extensions_success>(payload);
        if ( ! success || success->request_id != extensions_request_id) {
            return fail("invalid extension negotiation success");
        }
        payout_outputs_ = std::find(success->supported_extensions.begin(),
            success->supported_extensions.end(), requested_payout_outputs_extension)
            != success->supported_extensions.end();
        state_ = state::ready;
        return {{}, false, true, {}};
    }
    if (header.msg_type == request_extensions_error::message_type) {
        auto const error = parse<request_extensions_error>(payload);
        if ( ! error || error->request_id != extensions_request_id) {
            return fail("invalid extension negotiation error");
        }
        // An extension this node does not implement, and the pool requires it.
        if ( ! error->required_extensions.empty()) return fail("upstream requires an unsupported extension");
        payout_outputs_ = false;
        state_ = state::ready;
        return {{}, false, true, {}};
    }
    return fail("unexpected extension negotiation message");
}

jd_session::result jd_session::process_channel(message_header const& header, byte_span payload) {
    if (header.channel_msg()) return fail("invalid upstream channel frame");
    if (header.msg_type == open_mining_channel_error::message_type) {
        auto const error = parse<open_mining_channel_error>(payload);
        if ( ! error || error->request_id != channel_request_id) {
            return fail("invalid upstream channel error");
        }
        return fail("upstream refused extended channel (" + printable_error_code(error->error_code) + ")");
    }
    if (header.msg_type != open_extended_mining_channel_success::message_type) {
        return fail("unexpected upstream channel message");
    }
    auto success = parse<open_extended_mining_channel_success>(payload);
    if ( ! success || success->request_id != channel_request_id || ! nonzero(success->target)
        || success->channel_id == success->group_channel_id
        || success->extranonce_size < min_extranonce_size
        || size_t{success->extranonce_size} + success->extranonce_prefix.size() > 32) {
        return fail("invalid upstream extended channel");
    }
    channel_ = std::move(*success);
    state_ = state::ready;
    return {{}, false, true, {}};
}

bool jd_session::matches_channel(uint32_t id) const {
    return id == channel_->channel_id || id == channel_->group_channel_id;
}

jd_session::result jd_session::process_mining(message_header const& header, byte_span payload) {
    // SetGroupChannel (type 0x25) has the channel bit clear. The check below
    // would close a regrouping of this extended channel (sv2-spec 5.3.22).
    if ( ! header.channel_msg() && header.msg_type == set_group_channel::message_type) {
        auto const message = parse<set_group_channel>(payload);
        if ( ! message) return fail("invalid upstream group channel");
        if (std::find(message->channel_ids.begin(), message->channel_ids.end(),
                channel_->channel_id) != message->channel_ids.end()) {
            channel_->group_channel_id = message->group_channel_id;
        }
        return {{}, false, true, {}};
    }
    if ( ! header.channel_msg()) return fail("invalid upstream mining frame");
    switch (header.msg_type) {
        case set_target::message_type: {
            auto const message = parse<set_target>(payload);
            if ( ! message || ! matches_channel(message->channel_id) || ! nonzero(message->target)) {
                return fail("invalid upstream target");
            }
            channel_->target = message->target;
            break;
        }
        case set_extranonce_prefix::message_type: {
            auto message = parse<set_extranonce_prefix>(payload);
            if ( ! message || ! matches_channel(message->channel_id)) {
                return fail("invalid upstream extranonce prefix");
            }
            // The protocol requires prefixes addressed to a group to be ignored.
            if (message->channel_id != channel_->channel_id) break;
            if (size_t{channel_->extranonce_size} + message->extranonce_prefix.size() > 32) {
                return fail("upstream extranonce no longer fits");
            }
            channel_->extranonce_prefix = std::move(message->extranonce_prefix);
            break;
        }
        case new_extended_mining_job::message_type: {
            auto const message = parse<new_extended_mining_job>(payload);
            if ( ! message || ! matches_channel(message->channel_id)) {
                return fail("invalid upstream job");
            }
            // Work selection always uses the node's own template. Pool jobs
            // carry no selection or activation state into our custom work.
            break;
        }
        case mining_set_new_prev_hash::message_type: {
            auto const message = parse<mining_set_new_prev_hash>(payload);
            if ( ! message || ! matches_channel(message->channel_id)) {
                return fail("invalid upstream previous hash");
            }
            break;
        }
        case close_channel::message_type: {
            auto const message = parse<close_channel>(payload);
            if ( ! message || ! matches_channel(message->channel_id)) {
                return fail("invalid upstream channel close");
            }
            return fail("upstream closed extended channel");
        }
        default:
            return fail("unexpected upstream mining message");
    }
    return {{}, false, true, {}};
}

jd_session::result jd_session::process(message_header const& header, byte_span payload) {
    if (state_ == state::closed) return {{}, true, false, error_};
    if (state_ == state::idle) return fail("upstream session has not started");
    if (header.msg_length != payload.size() || payload.size() > max_payload_size) {
        return fail("invalid upstream frame length");
    }
    if (state_ == state::awaiting_extensions) return process_extensions(header, payload);
    if (header.extension() != 0) return {{}, false, ready(), {}};
    if (state_ == state::awaiting_setup) return process_setup(header, payload);
    if (state_ == state::awaiting_channel) return process_channel(header, payload);
    if (peer_ == jd_peer::mining) return process_mining(header, payload);
    return fail("unexpected upstream declaration message");
}

} // namespace kth::node::sv2
