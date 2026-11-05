// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/mining_session.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/chain/output.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/mining_messages.hpp>
#include <kth/node/sv2/template_provider.hpp>

namespace kth::node::sv2 {

namespace {

constexpr uint16_t protocol_version = 2;

template <typename Message>
uint16_t channel_extension() {
    // SetupConnection messages are shared with Template Distribution and do
    // not carry the Mining Protocol channel_message constant.
    if constexpr (requires { Message::channel_message; }) {
        return Message::channel_message ? message_header::channel_msg_flag : 0;
    } else {
        return 0;
    }
}

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{channel_extension<Message>(), Message::message_type,
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

bool printable_ascii(std::string_view text) {
    for (unsigned char const byte : text) {
        if (byte < 0x20 || byte > 0x7e) return false;
    }
    return true;
}

template <typename Message>
void append_frame(data_chunk& bytes, Message const& message) {
    auto framed = frame(message);
    bytes.insert(bytes.end(), framed.begin(), framed.end());
}

} // namespace

mining_session::mining_session(uint64_t connection_salt, domain::chain::script payout,
    std::shared_ptr<uint64_t> pool_allocations)
    : engine_(connection_salt, std::move(pool_allocations))
    , payout_(std::move(payout))
{}

mining_session::result mining_session::close() {
    state_ = state::closed;
    return {{}, true, false, {}, {}};
}

bool mining_session::ready() const {
    return state_ == state::ready;
}

bool mining_session::needs_template() const {
    return ready() && engine_.channel_count() > 0 && ! template_prev_;
}

domain::chain::output::list mining_session::coinbase_outputs(uint64_t value) const {
    // This connection pays one output. The engine takes a list so a later
    // payout split can pass several without another job builder.
    return {domain::chain::output(value, payout_, std::nullopt)};
}

data_chunk mining_session::job_frames(mining::work_job const& job) const {
    data_chunk bytes;
    // The prefix applies to jobs sent after this message (sv2-spec 5.3.10).
    if (job.extranonce_prefix_update) {
        append_frame(bytes, set_extranonce_prefix{job.channel_id, *job.extranonce_prefix_update});
    }
    if (job.extended) {
        new_extended_mining_job message;
        message.channel_id = job.channel_id;
        message.job_id = job.job_id;
        message.version = job.version;
        message.version_rolling_allowed = job.version_rolling_allowed;
        message.merkle_path = job.merkle_path;
        message.coinbase_tx_prefix = job.coinbase_tx_prefix;
        message.coinbase_tx_suffix = job.coinbase_tx_suffix;
        append_frame(bytes, message);
    } else {
        new_mining_job message;
        message.channel_id = job.channel_id;
        message.job_id = job.job_id;
        message.version = job.version;
        message.merkle_root = job.merkle_root;
        append_frame(bytes, message);
    }
    // ntime stays unset above: the first job on a channel is a future job.
    // SetNewPrevHash is what activates it (sv2-spec 5.3.15 and 5.3.17).
    append_frame(bytes, mining_set_new_prev_hash{job.channel_id, job.job_id,
        job.prev_hash, job.ntime_start, job.nbits});
    return bytes;
}

mining_session::result mining_session::finish_open(mining::open_request const& request) {
    auto const opened = engine_.open(request);
    if (opened.status == mining::open_status::protocol_error) return close();
    if (opened.status != mining::open_status::opened) {
        return {frame(open_mining_channel_error{request.request_id, opened.error_code}),
            false, false, {}, {}};
    }

    result out;
    if (request.kind == mining::channel_kind::extended) {
        out.response = frame(open_extended_mining_channel_success{request.request_id,
            opened.channel_id, opened.target, opened.extranonce_size,
            opened.extranonce_prefix, opened.group_channel_id});
    } else {
        out.response = frame(open_standard_mining_channel_success{request.request_id,
            opened.channel_id, opened.target, opened.extranonce_prefix,
            opened.group_channel_id});
    }
    if (current_ && template_prev_) {
        auto const job = engine_.on_channel(opened.channel_id, *current_,
            coinbase_outputs(current_->coinbase_value));
        if ( ! job) return close();
        auto framed = job_frames(*job);
        out.response.insert(out.response.end(), framed.begin(), framed.end());
    } else {
        // During a chain transition no template is available yet. Hold the
        // success so the first message after it is always the future job.
        pending_.push_back({opened.channel_id, opened.group_channel_id, std::move(out.response)});
        out.response.clear();
        out.wants_template = true;
    }
    return out;
}

mining_session::result mining_session::finish_share(uint8_t message_type, byte_span payload) {
    mining::share_request share;
    share.extended = message_type == submit_shares_extended::message_type;
    if (share.extended) {
        auto message = parse<submit_shares_extended>(payload);
        if ( ! message) return close();
        share.channel_id = message->channel_id;
        share.sequence_number = message->sequence_number;
        share.job_id = message->job_id;
        share.nonce = message->nonce;
        share.ntime = message->ntime;
        share.version = message->version;
        share.extranonce = std::move(message->extranonce);
    } else {
        auto const message = parse<submit_shares_standard>(payload);
        if ( ! message) return close();
        share.channel_id = message->channel_id;
        share.sequence_number = message->sequence_number;
        share.job_id = message->job_id;
        share.nonce = message->nonce;
        share.ntime = message->ntime;
        share.version = message->version;
    }
    auto const outcome = engine_.submit(share);
    if (outcome.status == mining::share_status::rejected) {
        return {frame(submit_shares_error{outcome.channel_id, outcome.sequence_number,
            outcome.error_code}), false, false, {}, {}};
    }
    result out;
    out.response = frame(submit_shares_success{outcome.channel_id, outcome.sequence_number,
        outcome.accepted_count, outcome.shares_sum});
    out.solution = outcome.block;
    out.pool = outcome.pool;
    if (outcome.renew) {
        // A pooled channel cannot change the declared generation. Roll the
        // bytes that identify it inside the upstream extranonce instead.
        engine_.bump_pool_roll(share.channel_id);
        // Renew before duplicate history fills, so even a block found at the
        // limit still goes through the found-block path. Accepted work keeps
        // its validated template: note_tip stales jobs before clearing it.
        // Renewal changes only a fixed-width generation in the coinbase.
        auto const& current = current_.value();
        auto const job = engine_.on_channel(share.channel_id, current,
            coinbase_outputs(current.coinbase_value));
        if ( ! job) {
            out.close = true;
            return out;
        }
        auto framed = job_frames(*job);
        out.response.insert(out.response.end(), framed.begin(), framed.end());
    }
    return out;
}

void mining_session::note_tip(hash_digest const& tip) {
    if (template_prev_ && *template_prev_ != tip) {
        engine_.stale_all();
        template_prev_.reset();
        current_.reset();
    }
}

mining_session::result mining_session::emit(std::vector<mining::work_job> const& jobs,
    blockchain::mining_template value) {
    // An open channel must receive its first future job before any other
    // channel messages. Do not leave it waiting if this template cannot fit.
    if (jobs.size() != engine_.channel_count()) return close();
    template_prev_ = value.previous_block_hash;
    current_ = std::move(value);
    result out;
    for (auto const& job : jobs) {
        auto const pending = std::find_if(pending_.begin(), pending_.end(),
            [&](pending_channel const& channel) { return channel.id == job.channel_id; });
        if (pending != pending_.end()) {
            out.response.insert(out.response.end(), pending->response.begin(), pending->response.end());
        }
        auto framed = job_frames(job);
        out.response.insert(out.response.end(), framed.begin(), framed.end());
    }
    pending_.clear();
    return out;
}

mining_session::result mining_session::apply(blockchain::mining_template value) {
    if (state_ != state::ready) return close();
    auto const outputs = coinbase_outputs(value.coinbase_value);
    auto const jobs = engine_.on_template(value, outputs);
    return emit(jobs, std::move(value));
}

mining_session::result mining_session::apply_pool(mining::declared_work const& work) {
    if (state_ != state::ready) return close();
    mining::pool_binding binding;
    binding.prefix = work.upstream_extranonce_prefix;
    binding.extranonce_size = work.upstream_extranonce_size;
    binding.generation = work.generation;
    binding.fixed_script = work.fixed_script;
    binding.script_prefix = work.coinbase_prefix;
    binding.coinbase_version = work.coinbase_version;
    binding.coinbase_input_sequence = work.coinbase_input_sequence;
    binding.coinbase_locktime = work.coinbase_locktime;
    auto const jobs = engine_.on_pool_template(work.tmpl, work.outputs,
        coinbase_outputs(work.tmpl.coinbase_value), binding);
    if ( ! jobs) return close();
    return emit(*jobs, work.tmpl);
}

mining_session::result mining_session::process(message_header const& header, byte_span payload) {
    if (state_ == state::closed
        || header.msg_length > template_provider_session::max_payload_size
        || header.msg_length != payload.size()) {
        return close();
    }

    if (state_ == state::awaiting_setup) {
        if (header.extension_type != 0 || header.msg_type != setup_connection::message_type) {
            return close();
        }
        auto const setup = parse<setup_connection>(payload);
        if ( ! setup || setup->protocol != setup_connection::protocol_mining) return close();

        setup_connection_error error;
        error.flags = setup->flags & ~mining_supported_client_flags;
        if (setup->min_version > protocol_version || setup->max_version < protocol_version) {
            error.error_code = "protocol-version-mismatch";
        } else if (error.flags != 0) {
            // sv2-spec 3.6.3: the error carries every flag this server lacks.
            // Work selection is one of them; custom jobs are a later protocol.
            error.error_code = "unsupported-feature-flags";
        }
        if ( ! error.error_code.empty()) {
            state_ = state::closed;
            return {frame(error), true, false, {}, {}};
        }
        state_ = state::ready;
        standard_only_ = (setup->flags & mining_flag_requires_standard_jobs) != 0;
        // Success flags are zero: version rolling is allowed and standard
        // channels are accepted (sv2-spec 5.3.1).
        return {frame(setup_connection_success{protocol_version, 0}), false, false, {}, {}};
    }

    if (header.extension() != 0) return {};
    if (header.channel_msg() != (
            header.msg_type == update_channel::message_type
            || header.msg_type == close_channel::message_type
            || header.msg_type == submit_shares_standard::message_type
            || header.msg_type == submit_shares_extended::message_type)) {
        return close();
    }

    switch (header.msg_type) {
        case open_standard_mining_channel::message_type: {
            auto const message = parse<open_standard_mining_channel>(payload);
            if ( ! message) return close();
            mining::open_request request;
            request.request_id = message->request_id;
            request.nominal_hash_rate = message->nominal_hash_rate;
            request.max_target = message->max_target;
            request.kind = mining::channel_kind::standard;
            return finish_open(request);
        }
        case open_extended_mining_channel::message_type: {
            auto const message = parse<open_extended_mining_channel>(payload);
            if ( ! message) return close();
            mining::open_request request;
            request.request_id = message->request_id;
            request.nominal_hash_rate = message->nominal_hash_rate;
            request.max_target = message->max_target;
            request.min_extranonce_size = message->min_extranonce_size;
            request.kind = mining::channel_kind::extended;
            request.standard_only = standard_only_;
            return finish_open(request);
        }
        case update_channel::message_type: {
            auto const message = parse<update_channel>(payload);
            if ( ! message) return close();
            // A client cannot update a channel before it has been opened.
            // In particular its initial target must agree with its first job.
            if (std::any_of(pending_.begin(), pending_.end(),
                    [&](pending_channel const& channel) { return channel.id == message->channel_id; })) {
                return close();
            }
            auto const updated = engine_.update(message->channel_id,
                message->nominal_hash_rate, message->max_target);
            if (updated.protocol_error) return close();
            if (updated.unknown) {
                return {frame(update_channel_error{message->channel_id,
                    std::string(mining::error_invalid_channel_id)}), false, false, {}, {}};
            }
            if ( ! updated.target) return {};
            return {frame(set_target{updated.channel_id, *updated.target}), false, false, {}, {}};
        }
        case close_channel::message_type: {
            auto const message = parse<close_channel>(payload);
            if ( ! message || ! printable_ascii(message->reason_code)) return close();
            // No error is defined for an unknown id. The next message still runs.
            (void)engine_.close(message->channel_id);
            auto const removed = std::remove_if(pending_.begin(), pending_.end(),
                [&](pending_channel const& channel) {
                    return channel.id == message->channel_id || channel.group_id == message->channel_id;
                });
            pending_.erase(removed, pending_.end());
            return {};
        }
        case submit_shares_standard::message_type:
        case submit_shares_extended::message_type:
            return finish_share(header.msg_type, payload);
        default:
            return close();
    }
}

} // namespace kth::node::sv2
