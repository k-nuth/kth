// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_declaration.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/node/sv2/primitives.hpp>

namespace kth::node::sv2 {

namespace {

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{Message::channel_message
        ? message_header::channel_msg_flag : uint16_t{0}, Message::message_type,
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

// Request id of a reply this client understands, when the frame parses.
// Empty when the frame is not one of those replies or does not parse: the
// caller then applies the ordinary state checks.
std::optional<uint32_t> reply_request_id(jd_peer peer, message_header const& header,
    byte_span payload) {
    if (peer == jd_peer::declaration && ! header.channel_msg()) {
        switch (header.msg_type) {
            case allocate_mining_job_token_success::message_type:
                if (auto message = parse<allocate_mining_job_token_success>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            case declare_mining_job_success::message_type:
                if (auto message = parse<declare_mining_job_success>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            case declare_mining_job_error::message_type:
                if (auto message = parse<declare_mining_job_error>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            case provide_missing_transactions::message_type:
                if (auto message = parse<provide_missing_transactions>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            default:
                break;
        }
    }
    if (peer == jd_peer::mining && header.channel_msg()) {
        switch (header.msg_type) {
            case set_custom_mining_job_success::message_type:
                if (auto message = parse<set_custom_mining_job_success>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            case set_custom_mining_job_error::message_type:
                if (auto message = parse<set_custom_mining_job_error>(payload)) {
                    return message->request_id;
                }
                return std::nullopt;
            default:
                break;
        }
    }
    return std::nullopt;
}

std::optional<domain::chain::output::list> pool_outputs(byte_span bytes, uint64_t revenue) {
    byte_reader source(bytes);
    auto const count = source.read_variable_little_endian();
    // A serialized output needs at least a value and a script length. Bound
    // the count before any allocation, even for a short malicious response.
    if ( ! count || *count == 0 || *count > 256 || *count > source.remaining_size() / 9) {
        return std::nullopt;
    }
    domain::chain::output::list outputs;
    outputs.reserve(static_cast<size_t>(*count));
    for (size_t i = 0; i < *count; ++i) {
        auto output = domain::chain::output::from_data(source, true);
        if ( ! output || output->value() != 0 || output->token_data()) return std::nullopt;
        outputs.push_back(std::move(*output));
    }
    if ( ! source.is_exhausted()) return std::nullopt;
    outputs.front().set_value(revenue);
    return outputs;
}

} // namespace

jd_declaration::jd_declaration(std::string identity, bool full_template)
    : identity_(std::move(identity))
    , full_template_(full_template)
{}

bool jd_declaration::pending() const {
    return state_ == state::allocating || state_ == state::declaring || state_ == state::installing;
}

std::optional<mining::declared_work> const& jd_declaration::work() const {
    return work_;
}

bool jd_declaration::has_template() const {
    return has_template_;
}

blockchain::mining_template const& jd_declaration::template_value() const {
    return template_;
}

bool jd_declaration::can_push_solution() const {
    return full_template_ && (state_ == state::installing || state_ == state::accepted);
}

bool jd_declaration::full_template() const {
    return full_template_;
}

void jd_declaration::supersede() {
    if ( ! pending()) return;
    // start() will issue the next id on this same object. Remember this one
    // so a reply that arrives after that start is not applied to the new work.
    if (retired_request_ids_.size() == max_retired_request_ids) {
        retired_request_ids_.pop_front();
    }
    retired_request_ids_.push_back(request_id_);
    state_ = state::idle;
    work_.reset();
    transactions_.clear();
}

bool jd_declaration::retired(uint32_t id) const {
    return std::find(retired_request_ids_.begin(), retired_request_ids_.end(), id)
        != retired_request_ids_.end();
}

void jd_declaration::note_target(hash_digest const& target) {
    // A custom job's target is fixed when it is sent with nTime. Updates
    // received while obtaining its token still apply to that future job.
    if (state_ == state::allocating || state_ == state::declaring) {
        channel_.target = target;
        if (work_) work_->upstream_target = target;
    }
}

jd_declaration::result jd_declaration::fail(std::string reason) {
    state_ = state::failed;
    work_.reset();
    transactions_.clear();
    // template_ stays. Fallback republishes that selection with the solo payout.
    result out;
    out.error = std::move(reason);
    return out;
}

jd_declaration::result jd_declaration::start(blockchain::mining_template value,
    open_extended_mining_channel_success const& channel) {
    if (pending() || state_ == state::failed) return fail("declaration is not idle");
    if (identity_.empty() || identity_.size() > b0_255_max
        || request_id_ == std::numeric_limits<uint32_t>::max()) {
        return fail("invalid declaration identity or exhausted request ids");
    }
    if (channel.channel_id == channel.group_channel_id
        || channel.extranonce_size < jd_session::min_extranonce_size
        || channel.extranonce_prefix.size() + size_t{channel.extranonce_size} > 32
        || std::all_of(channel.target.begin(), channel.target.end(), [](uint8_t x) { return x == 0; })) {
        return fail("invalid declaration channel");
    }
    if (full_template_ && value.selection.txs.size() > b0_64k_max) {
        // Full-template puts every txid in DeclareMiningJob.wtxid_list, which
        // is SEQ0_64K[U256] (sv2-spec 6.4.4). Coinbase-only sends only the
        // merkle path, so a Bitcoin Cash block of any transaction count fits.
        return fail("full-template transaction list exceeds the SEQ0_64K limit of 65535");
    }
    transactions_.clear();
    size_t retained = 0;
    for (auto const& tx : value.selection.txs) {
        if ( ! tx) return fail("null declaration transaction");
        if (full_template_) {
            domain::chain::transaction const& transaction = *tx;
            auto const raw_size = transaction.serialized_size(true);
            // The whole template stays local. A ProvideMissingTransactions
            // reply is checked later, and only for the transactions named in it.
            // Bitcoin Cash's block size limit is adaptive (ABLA), so the bound is
            // this template's own consensus size limit, not a fixed number.
            if (raw_size > value.size_limit || retained > value.size_limit - raw_size) {
                return fail("retained template transactions exceed the template's consensus block size limit");
            }
            retained += raw_size;
            // These are the same BCH network bytes the local Template Provider
            // returns. Keep them once so missing-transaction replies use them intact.
            transactions_.push_back(to_data_chunk(transaction, true));
        }
    }
    ++request_id_;
    channel_ = channel;
    template_ = std::move(value);
    has_template_ = true;
    // The replacement is a new declaration. It stays unaccepted until
    // SetCustomMiningJob.Success, whose job id may be zero.
    work_.reset();
    state_ = state::allocating;
    result out;
    out.declaration = frame(allocate_mining_job_token{identity_, request_id_});
    out.progress = true;
    return out;
}

jd_declaration::result jd_declaration::install(data_chunk token) {
    set_custom_mining_job message;
    message.channel_id = channel_.channel_id;
    message.request_id = request_id_;
    message.mining_job_token = std::move(token);
    message.version = template_.version;
    message.prev_hash = template_.previous_block_hash;
    message.ntime_start = template_.current_time;
    message.nbits = template_.bits;
    message.coinbase_tx_version = 1;
    message.coinbase_prefix = work_->coinbase_prefix;
    message.coinbase_tx_input_sequence = 0xffffffffu;
    message.coinbase_tx_outputs = work_->coinbase_tx_outputs;
    message.coinbase_tx_locktime = 0;
    message.merkle_path = work_->merkle_path;
    state_ = state::installing;
    result out;
    out.mining = frame(message);
    out.progress = true;
    return out;
}

jd_declaration::result jd_declaration::process(jd_peer peer,
    message_header const& header, byte_span payload) {
    // Inbound cap. Received JD messages are small. Outbound frames are checked
    // against jd_frame_max_payload when they are built.
    if (header.msg_length != payload.size() || payload.size() > jd_message_max_payload) {
        return fail("invalid declaration frame length");
    }
    if (header.extension() != 0) return {};
    // A replaced declaration's reply can arrive after the next start().
    // Ignore it and leave the declaration now in flight alone.
    if (auto const id = reply_request_id(peer, header, payload); id && retired(*id)) {
        spdlog::debug("[sv2 jd] ignoring reply type {} for retired request {}",
            static_cast<unsigned>(header.msg_type), *id);
        return {};
    }
    if (state_ == state::failed || ! pending()) return fail("unexpected declaration response");
    if (state_ == state::allocating && peer == jd_peer::declaration && ! header.channel_msg()
        && header.msg_type == allocate_mining_job_token_success::message_type) {
        auto const allocation = parse<allocate_mining_job_token_success>(payload);
        if ( ! allocation || allocation->request_id != request_id_ || allocation->mining_job_token.empty()) {
            return fail("invalid token allocation response");
        }
        auto const outputs = pool_outputs(allocation->coinbase_tx_outputs, template_.coinbase_value);
        if ( ! outputs) return fail("invalid pool coinbase outputs");
        work_ = mining::make_declared_work(template_, *outputs, request_id_,
            channel_.extranonce_prefix, channel_.extranonce_size);
        if ( ! work_) return fail("declared coinbase or template exceeds mining limits");
        work_->upstream_channel_id = channel_.channel_id;
        work_->upstream_target = channel_.target;
        // This coinbase is not accepted yet. Success is the only place that
        // records the pool job id, and that id may be zero.
        work_->upstream_accepted = false;
        if ( ! full_template_) return install(allocation->mining_job_token);

        declare_mining_job message;
        message.request_id = request_id_;
        message.mining_job_token = allocation->mining_job_token;
        message.version = template_.version;
        message.coinbase_tx_prefix = work_->coinbase_tx_prefix;
        message.coinbase_tx_suffix = work_->coinbase_tx_suffix;
        // BCH has no SegWit: the spec's wtxid list carries ordinary transaction
        // ids in the Template Provider's CTOR order, with no witness commitment.
        for (auto const& tx : template_.selection.txs) message.wtxid_list.push_back(tx->hash());
        if (message.serialized_size() > jd_frame_max_payload) {
            return fail("DeclareMiningJob exceeds the SV2 frame maximum");
        }
        state_ = state::declaring;
        result out;
        out.declaration = frame(message);
        out.progress = true;
        return out;
    }
    if (state_ == state::declaring && peer == jd_peer::declaration && ! header.channel_msg()) {
        if (header.msg_type == declare_mining_job_error::message_type) {
            auto const message = parse<declare_mining_job_error>(payload);
            if ( ! message || message->request_id != request_id_) return fail("invalid declaration refusal");
            return fail("JDS refused the local template: " + message->error_code);
        }
        if (header.msg_type == declare_mining_job_success::message_type) {
            auto const message = parse<declare_mining_job_success>(payload);
            if ( ! message || message->request_id != request_id_ || message->new_mining_job_token.empty()) {
                return fail("invalid declaration success");
            }
            return install(message->new_mining_job_token);
        }
        if (header.msg_type == provide_missing_transactions::message_type) {
            auto const message = parse<provide_missing_transactions>(payload);
            if ( ! message || message->request_id != request_id_) {
                return fail("invalid missing transaction request");
            }
            provide_missing_transactions_success response;
            response.request_id = request_id_;
            std::vector<bool> supplied(transactions_.size(), false);
            for (auto const index : message->unknown_tx_position_list) {
                if (index >= transactions_.size() || supplied[index]) {
                    return fail("invalid missing transaction position");
                }
                supplied[index] = true;
                response.transaction_list.push_back(transactions_[index]);
            }
            // Only the named transactions are on the wire. The retained
            // template may be as large as the block's ABLA limit; one message
            // may not exceed the U24 frame.
            if (response.serialized_size() > jd_frame_max_payload) {
                return fail("ProvideMissingTransactions.Success exceeds the SV2 frame maximum");
            }
            result out;
            out.declaration = frame(response);
            // Supplying data does not renew the declaration's response deadline.
            return out;
        }
    }
    if (state_ == state::installing && peer == jd_peer::mining && header.channel_msg()) {
        if (header.msg_type == set_custom_mining_job_error::message_type) {
            auto const message = parse<set_custom_mining_job_error>(payload);
            if ( ! message || message->request_id != request_id_ || message->channel_id != channel_.channel_id) {
                return fail("invalid custom job refusal");
            }
            return fail("pool refused the local custom job: " + message->error_code);
        }
        if (header.msg_type == set_custom_mining_job_success::message_type) {
            auto const message = parse<set_custom_mining_job_success>(payload);
            if ( ! message || message->request_id != request_id_ || message->channel_id != channel_.channel_id) {
                return fail("invalid custom job success");
            }
            work_->upstream_job_id = message->job_id;
            work_->upstream_accepted = true;
            state_ = state::accepted;
            transactions_.clear();
            result out;
            out.progress = true;
            out.accepted = true;
            return out;
        }
    }
    return fail("unexpected declaration message or direction");
}

} // namespace kth::node::sv2
