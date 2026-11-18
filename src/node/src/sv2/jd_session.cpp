// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_session.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include <spdlog/spdlog.h>

#include <kth/domain/version.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/extensions.hpp>
#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

constexpr uint32_t channel_request_id = 1;
constexpr uint32_t jd_flag_declare_tx_data = 1u << 0;

// The channel asks for no target ceiling. Each UpdateChannel repeats it, so a
// hash rate change never also asks the pool for harder shares (sv2-spec 5.3.7).
hash_digest open_max_target() {
    hash_digest target;
    target.fill(0xff);
    return target;
}

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{framed_extension_type<Message>(), Message::message_type,
        static_cast<uint32_t>(payload.size())});
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
    std::string user_identity, bool full_template, bool worker_names)
    : peer_(peer)
    , host_(std::move(host))
    , port_(port)
    , identity_(std::move(user_identity))
    , full_template_(full_template)
    , worker_names_(peer == jd_peer::mining && worker_names)
    , negotiate_extensions_(worker_names_)
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
    // sv2-spec 3.6.1: vendor names the developer and its version, and
    // hardware_version the package in use. A node has no telemetry device id.
    setup.vendor = std::format("{} {}", kth::client_name, kth::version);
    setup.hardware_version = std::format("{} {} full node", kth::client_name, kth::currency_name);
    state_ = state::awaiting_setup;
    return frame(setup);
}

bool jd_session::ready() const {
    return state_ == state::ready;
}

std::optional<open_extended_mining_channel_success> const& jd_session::channel() const {
    return channel_;
}

void jd_session::skip_extensions() {
    negotiate_extensions_ = false;
}

bool jd_session::awaiting_extensions() const {
    return state_ == state::awaiting_extensions;
}

bool jd_session::worker_identities() const {
    return worker_identities_;
}

void jd_session::note_hash_rate(float value) {
    hash_rate_ = value;
}

std::optional<update_channel> jd_session::hash_rate_update() {
    if ( ! ready() || ! channel_ || hash_rate_ == reported_hash_rate_) return std::nullopt;
    reported_hash_rate_ = hash_rate_;
    return update_channel{channel_->channel_id, hash_rate_, open_max_target()};
}

jd_session::result jd_session::fail(std::string error, bool incompatible) {
    state_ = state::closed;
    channel_.reset();
    error_ = std::move(error);
    return {{}, true, false, error_, incompatible};
}

jd_session::result jd_session::process_setup(message_header const& header, byte_span payload) {
    if (header.channel_msg()) return fail("invalid upstream setup frame");
    if (header.msg_type == setup_connection_error::message_type) {
        auto const error = parse<setup_connection_error>(payload);
        if ( ! error) return fail("malformed upstream setup error");
        auto reason = "upstream refused setup (" + printable_error_code(error->error_code) + ")";
        // SetupConnection.Error names the flags behind the refusal. A JDS that
        // wants DECLARE_TX_DATA accepts only full-template declarations.
        if (peer_ == jd_peer::declaration && ! full_template_
            && (error->flags & jd_flag_declare_tx_data) != 0) {
            reason += "; this JDS requires full-template (sv2.jd.mode = full-template)";
        }
        return fail(std::move(reason), true);
    }
    if (header.msg_type != setup_connection_success::message_type) {
        return fail("unexpected upstream setup message");
    }
    auto const success = parse<setup_connection_success>(payload);
    if ( ! success) return fail("malformed upstream setup success");
    if (success->used_version != 2) return fail("invalid upstream setup version", true);
    uint32_t const allowed = peer_ == jd_peer::mining ? mining_flag_requires_extended_channels : 0;
    if ((success->flags & ~allowed) != 0) return fail("unsupported upstream setup flags", true);
    if (peer_ == jd_peer::declaration) {
        state_ = state::ready;
        return {{}, false, true, {}};
    }
    if ( ! negotiate_extensions_) return open_channel();
    // Extension 0x0001, section 4: the answer has to arrive before the channel
    // request. Silence is not a refusal; the caller replaces this connection
    // with one that does not ask.
    state_ = state::awaiting_extensions;
    return {request_worker_identities(), false, false, {}};
}

data_chunk jd_session::request_worker_identities() {
    request_extensions request;
    request.request_id = ++extensions_request_;
    request.requested_extensions.push_back(worker_hashrate_tracking_extension);
    extensions_pending_ = true;
    return frame(request);
}

jd_session::result jd_session::process_extensions(message_header const& header, byte_span payload) {
    if (header.channel_msg()) return fail("invalid extension negotiation frame");
    uint16_t answered = 0;
    bool granted = false;
    bool required = false;
    if (header.msg_type == request_extensions_success::message_type) {
        auto const success = parse<request_extensions_success>(payload);
        if ( ! success) return fail("invalid extension negotiation success");
        answered = success->request_id;
        // Only what the pool lists may be used (extension 0x0001, section 1.1).
        granted = std::find(success->supported_extensions.begin(), success->supported_extensions.end(),
            worker_hashrate_tracking_extension) != success->supported_extensions.end();
    } else if (header.msg_type == request_extensions_error::message_type) {
        auto const error = parse<request_extensions_error>(payload);
        if ( ! error) return fail("invalid extension negotiation error");
        answered = error->request_id;
        required = ! error->required_extensions.empty();
    } else {
        return fail("unexpected extension negotiation message");
    }
    if (answered != extensions_request_) {
        // A later ChannelEndpointChanged asked again, so only that answer counts.
        if (state_ == state::ready) return {{}, false, true, {}};
        return fail("invalid extension negotiation reply");
    }
    // The pool requires an extension this node does not implement, and
    // disconnects a client that goes on without it.
    if (required) return fail("upstream requires an unsupported extension", true);
    extensions_pending_ = false;
    worker_identities_ = granted;
    if (state_ == state::awaiting_extensions) return open_channel();
    return {{}, false, true, {}};
}

jd_session::result jd_session::open_channel() {
    open_extended_mining_channel request;
    request.request_id = channel_request_id;
    request.user_identity = identity_;
    // Miners that connected before the pool answered setup are already counted.
    request.nominal_hash_rate = hash_rate_;
    request.max_target = open_max_target();
    request.min_extranonce_size = min_extranonce_size;
    reported_hash_rate_ = hash_rate_;
    state_ = state::awaiting_channel;
    return {frame(request), false, false, {}};
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
        case channel_endpoint_changed::message_type: {
            // A proxy in front of the pool moved this channel, which keeps its
            // work. sv2-spec 3.6.4: extension state resets and negotiation
            // begins again, so shares go without the worker until the new
            // answer grants it. A connection that never asked has none.
            auto const message = parse<channel_endpoint_changed>(payload);
            if ( ! message || ! matches_channel(message->channel_id)) {
                return fail("invalid upstream channel endpoint change");
            }
            if ( ! negotiate_extensions_) break;
            worker_identities_ = false;
            return {request_worker_identities(), false, true, {}};
        }
        case update_channel_error::message_type: {
            auto const message = parse<update_channel_error>(payload);
            // Only this channel id is ever updated. An error naming its group
            // or another id answers nothing this client sent.
            if ( ! message || message->channel_id != channel_->channel_id) {
                return fail("invalid upstream channel update error");
            }
            // The pool keeps the channel as it was, so jobs and shares go on
            // (sv2-spec 5.3.8). The refused sum is not sent again.
            spdlog::warn("[sv2 jd] Pool refused the channel hash rate update ({}); mining continues",
                printable_error_code(message->error_code));
            break;
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
    if (peer_ == jd_peer::mining && header.extension() == extensions_negotiation_type) {
        if (extensions_pending_) return process_extensions(header, payload);
        // Nothing was asked, so this answers nothing. Extension 0x0001,
        // section 4: a pool disconnects a client without the extensions it
        // requires. An entry that is not the operator's own never sends worker
        // names, so such a pool is incompatible with it.
        if ( ! header.channel_msg() && header.msg_type == request_extensions_error::message_type) {
            auto const error = parse<request_extensions_error>(payload);
            if (error && ! error->required_extensions.empty()) {
                return fail("upstream requires an extension this connection did not negotiate",
                    ! worker_names_);
            }
        }
    }
    if (header.extension() != 0) return {{}, false, ready(), {}};
    if (state_ == state::awaiting_setup) return process_setup(header, payload);
    if (state_ == state::awaiting_extensions) {
        return fail("unexpected upstream message during extension negotiation");
    }
    if (state_ == state::awaiting_channel) return process_channel(header, payload);
    if (peer_ == jd_peer::mining) return process_mining(header, payload);
    return fail("unexpected upstream declaration message");
}

} // namespace kth::node::sv2
