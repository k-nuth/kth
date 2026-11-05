// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_client.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <boost/smart_ptr/make_shared.hpp>

#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/detail/scope_action.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/jd_messages.hpp>
#include <kth/node/sv2/mining_messages.hpp>
#include <kth/node/sv2/noise.hpp>

namespace kth::node::sv2 {

namespace {

jd_endpoint endpoint(std::string const& address) {
    auto value = parse_jd_endpoint(address);
    if ( ! value) throw std::invalid_argument("Invalid Job Declaration endpoint");
    return *value;
}

hash_digest authority(std::string const& text) {
    auto value = noise_parse_authority_key(text);
    if ( ! value) throw std::invalid_argument("Invalid Job Declaration authority key");
    return *value;
}

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

// jd_session.cpp keeps the same helper in an anonymous namespace, so this
// translation unit cannot call it. Upstream text stays on one ASCII line.
std::string printable_error_code(std::string const& code) {
    auto text = code.substr(0, 64);
    for (auto& character : text) {
        auto const byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7e) character = '?';
    }
    return text;
}

// Shares found before the pool names the custom job. Past this, drop rather
// than let a slow Success pin every downstream proof.
constexpr size_t max_cached_shares = 32;
constexpr size_t max_pending_shares = 1024;

// Shares are forwarded continuously, not only in the cached burst, so the
// record has to outlast a pool whose replies lag. Otherwise a refusal of
// current work would age out and escape sv2-spec 6.2. The engine's share
// history bound is reused. A reply for an older sequence is ignored, the same
// as a sequence this client never sent.
constexpr size_t max_tracked_shares = mining::max_remembered_shares;

// One declaration is accepted or replaced before a run of blocks. Two covers
// a pair found on the same coinbase before DeclareMiningJob.Success.
constexpr size_t max_pending_solutions = 2;

} // namespace

struct jd_client::attempt {
    struct submitted_share {
        uint32_t sequence;
        hash_digest previous;
        std::chrono::steady_clock::time_point sent;
    };

    attempt(size_t index, sv2_settings::jd_upstream const& upstream,
        sv2_settings const& settings, ::asio::any_io_executor executor)
        : index(index)
        , mining(executor, endpoint(upstream.mining_address).host,
            endpoint(upstream.mining_address).port, authority(upstream.mining_authority_public_key),
            std::chrono::seconds(settings.handshake_timeout_seconds))
        , declaration(executor, endpoint(upstream.jds_address).host,
            endpoint(upstream.jds_address).port, authority(upstream.jds_authority_public_key),
            std::chrono::seconds(settings.handshake_timeout_seconds))
        , mining_session(jd_peer::mining, endpoint(upstream.mining_address).host,
            endpoint(upstream.mining_address).port, upstream.user_identity, upstream.mode == "full-template")
        , declaration_session(jd_peer::declaration, endpoint(upstream.jds_address).host,
            endpoint(upstream.jds_address).port, upstream.user_identity, upstream.mode == "full-template")
        , jobs(upstream.user_identity, upstream.mode == "full-template")
        , health(settings.jd)
        , declaration_wake(executor)
    {}

    void wake_declarations() {
        declaration_changed = true;
        declaration_wake.cancel();
    }

    void stop() {
        stopped = true;
        // A DeclareMiningJob.Success already queued must not push a block
        // after this attempt has failed or the client has stopped.
        pending_solutions.clear();
        wake_declarations();
        mining.stop();
        declaration.stop();
    }

    size_t const index;
    jd_connection mining;
    jd_connection declaration;
    jd_session mining_session;
    jd_session declaration_session;
    jd_declaration jobs;
    jd_health_window health;
    ::asio::steady_timer declaration_wake;
    std::optional<std::chrono::steady_clock::time_point> job_sent;
    std::optional<std::chrono::steady_clock::time_point> job_deadline;
    std::vector<mining::pool_proof> cached;
    std::deque<submitted_share> submitted;
    // sequence and declared_work::generation of each share this attempt sent.
    // The deque is capped so a slow pool cannot pin every share. previous and
    // sent stay with the sequence: a current-tip refusal still needs the
    // share's parent, and the health window still needs the send time.
    // Success does not drop an entry. The reply can arrive after the ack that
    // removed it from `submitted`, and after a later declaration replaced it.
    struct tracked_share {
        uint32_t sequence = 0;
        uint32_t generation = 0;
        hash_digest previous{};
        std::chrono::steady_clock::time_point sent{};
        // The share also met the network target, so its block ends this job.
        bool block = false;
    };
    std::deque<tracked_share> tracked;
    // Full-template blocks found before DeclareMiningJob.Success. Pushed once
    // can_push_solution() is true for that generation. Two covers a pair on
    // the same coinbase; a later one is dropped rather than queued forever.
    std::vector<mining::pool_proof> pending_solutions;
    uint32_t sequence = 0;
    uint64_t work_id = 0;
    bool ready = false;
    bool healthy = false;
    bool pool_live = false;
    bool declaration_changed = false;
    bool stopped = false;
    bool done = false;
};

jd_client::jd_client(sv2_settings const& settings, ::asio::any_io_executor executor,
    blockchain::block_chain* chain)
    : owner_(::asio::make_strand(executor))
    , settings_(settings)
    , upstreams_(jd_upstreams(settings.jd))
    , policy_(upstreams_.size(), settings.jd)
    , chain_(chain)
    , response_timeout_(settings.jd.response_timeout_seconds)
    , ready_(owner_, 1)
    , wake_(owner_, 1)
    , publications_(owner_, 1)
{
    // Check even dormant entries before any sockets are started.
    for (auto const& upstream : upstreams_) {
        (void)endpoint(upstream.mining_address);
        (void)endpoint(upstream.jds_address);
        (void)authority(upstream.mining_authority_public_key);
        (void)authority(upstream.jds_authority_public_key);
    }
    update_status();
    for (size_t index = 0; index < upstreams_.size(); ++index) {
        auto const& upstream = upstreams_[index];
        spdlog::info("[sv2 jd] Entry {} available: Mining {}; JDS {}; no previous error",
            index + 1, upstream.mining_address, upstream.jds_address);
    }
}

void jd_client::update_status() {
    status_.store(boost::make_shared<jd_status>(policy_.snapshot()));
}

void jd_client::fail(attempt& value, jd_failure kind, std::string reason) {
    if (value.stopped) return;
    if (active_.get() == &value) publish_solo(value);
    value.stop();
    auto const now = std::chrono::steady_clock::now();
    policy_.fail(value.index, kind, std::move(reason), now);
    update_status();
    auto const& entry = policy_.entries()[value.index];
    spdlog::warn("[sv2 jd] Entry {} {}: {}; retry hold {} seconds", value.index + 1,
        jd_health_name(entry.health), entry.last_error,
        std::chrono::duration_cast<std::chrono::seconds>(entry.retry_at - now).count());
}

void jd_client::mark_ready(attempt& value) {
    if (value.ready || ! value.mining_session.ready() || ! value.declaration_session.ready()) return;
    value.ready = true;
    value.wake_declarations();
    if ( ! chain_) {
        value.healthy = true;
        policy_.healthy(value.index, std::chrono::steady_clock::now());
    }
    if (active_.get() == &value && ! published_) {
        published_ = true;
        (void)ready_.try_send(std::error_code{}, 1);
    }
    update_status();
    (void)wake_.try_send(std::error_code{}, 1);
    spdlog::info("[sv2 jd] Entry {} both upstreams ready{}", value.index + 1,
        active_.get() == &value ? "; active" : "; probing");
}

::asio::awaitable<void> jd_client::serve(attempt& value, jd_connection& connection,
    jd_session& session, jd_peer peer) {
    using namespace ::asio::experimental::awaitable_operators;
    ::asio::steady_timer deadline(owner_);
    deadline.expires_at(std::chrono::steady_clock::time_point::max());
    bool done = false;
    auto sent = std::chrono::steady_clock::now();
    auto connected = [&] {
        if (stopped_ || value.stopped) return false;
        sent = std::chrono::steady_clock::now();
        deadline.expires_after(response_timeout_);
        return connection.send(session.start());
    };
    auto received = [&](data_chunk bytes) {
        if (stopped_ || value.stopped) return false;
        byte_reader source(bytes);
        auto const header = message_header::from_data(source);
        if ( ! header) return false;
        auto const payload = source.read_bytes(source.remaining_size());
        if ( ! payload) return false;
        // Share replies name one submission. Success is health-sampled.
        // An error is attributed in share_response; a stale or unknown
        // sequence must not end this attempt (sv2-spec 6.2).
        if (session.ready() && peer == jd_peer::mining && header->channel_msg()
            && header->extension() == 0
            && (header->msg_type == submit_shares_success::message_type
                || header->msg_type == submit_shares_error::message_type)) {
            return share_response(value, *header, *payload);
        }
        if (session.ready() && header->extension() == 0
            && (peer == jd_peer::declaration
                || header->msg_type == set_custom_mining_job_success::message_type
                || header->msg_type == set_custom_mining_job_error::message_type)) {
            return dispatch(value, value.jobs.process(peer, *header, *payload));
        }
        bool const was_ready = session.ready();
        auto const previous_prefix = value.mining_session.channel()
            ? value.mining_session.channel()->extranonce_prefix : data_chunk{};
        auto result = session.process(*header, *payload);
        if (result.close) {
            fail(value, result.incompatible ? jd_failure::incompatible : jd_failure::transient,
                std::move(result.error));
            return false;
        }
        if ( ! was_ready && header->extension() == 0
            && std::chrono::steady_clock::now() - sent
                > std::chrono::milliseconds(settings_.jd.max_latency_milliseconds)) {
            fail(value, jd_failure::transient, "upstream setup latency exceeded threshold");
            return false;
        }
        if (session.ready() && peer == jd_peer::mining && (value.jobs.pending() || value.jobs.work())
            && previous_prefix != value.mining_session.channel()->extranonce_prefix) {
            fail(value, jd_failure::transient, "pool changed the extranonce prefix of declared work");
            return false;
        }
        if (session.ready() && peer == jd_peer::mining) value.jobs.note_target(value.mining_session.channel()->target);
        if ( ! result.response.empty()) {
            sent = std::chrono::steady_clock::now();
            deadline.expires_after(response_timeout_);
            if ( ! connection.send(std::move(result.response))) return false;
        }
        if (session.ready()) {
            deadline.cancel();
            mark_ready(value);
        }
        return true;
    };
    auto exchange = [&]() -> ::asio::awaitable<void> {
        detail::scope_action finish([&] { done = true; deadline.cancel(); }, true);
        auto reason = co_await connection.run(connected, received);
        if ( ! value.stopped && ! stopped_) {
            fail(value, connection.authentication_failed() ? jd_failure::authority : jd_failure::transient,
                std::move(reason));
        }
    };
    auto response_deadline = [&]() -> ::asio::awaitable<void> {
        while ( ! done && ! session.ready()) {
            auto [error] = co_await deadline.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (done || session.ready()) co_return;
            if (error == ::asio::error::operation_aborted) continue;
            fail(value, jd_failure::transient, "setup response deadline expired");
            co_return;
        }
    };
    co_await (exchange() && response_deadline());
}

::asio::awaitable<void> jd_client::run_attempt(std::shared_ptr<attempt> value) {
    using namespace ::asio::experimental::awaitable_operators;
    detail::scope_action finish([&] { value->stop(); }, true);
    try {
        co_await (serve(*value, value->mining, value->mining_session, jd_peer::mining)
            && serve(*value, value->declaration, value->declaration_session, jd_peer::declaration)
            && declare_templates(*value));
    } catch (std::exception const& e) {
        if ( ! stopped_) fail(*value, jd_failure::transient, e.what());
    }
}

bool jd_client::recover_at_boundary() {
    if ( ! probe_ || probe_->stopped || ! probe_->healthy
        || ! policy_.switch_at_boundary(std::chrono::steady_clock::now(), true)) return false;
    if (active_) {
        publish_solo(*active_);
        active_->stop();
    }
    active_.swap(probe_);
    publish_pool(*active_);
    update_status();
    spdlog::info("[sv2 jd] Entry {} recovered; active at job boundary", active_->index + 1);
    return true;
}

void jd_client::start_attempt(size_t index, bool probe) {
    auto value = std::make_shared<attempt>(index, upstreams_[index], settings_, owner_);
    if (probe) probe_ = value;
    else active_ = value;
    ::asio::co_spawn(owner_, run_attempt(value), [this, value](std::exception_ptr error) {
        if (error && ! stopped_) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { fail(*value, jd_failure::transient, e.what()); }
        }
        value->done = true;
        (void)wake_.try_send(std::error_code{}, 1);
    });
    update_status();
    spdlog::info("[sv2 jd] Entry {} {}", index + 1, probe ? "probing" : "connecting; active");
}

::asio::awaitable<void> jd_client::run_owned() {
    using namespace ::asio::experimental::awaitable_operators;

    // Subscribe before the first snapshot so a completed transition cannot
    // leave the declaration loop asleep on an older tip. The channel's
    // executor is this strand: a completion, including the close notice, is
    // posted there. Chain stop closes subscribers and then stops the chain
    // executor, so a receive posted on that executor never resumes.
    auto tips = chain_
        ? chain_->subscribe_blockchain(::asio::any_io_executor(owner_))
        : nullptr;
    // Invoked only on owner_: by the schedule's exit, which stop() wakes,
    // and by the cleanup.
    auto const end_tips = [tips] {
        if (tips) {
            tips->cancel();
            tips->close();
        }
    };
    detail::scope_action cleanup([&] {
        end_tips();
        if (chain_) chain_->unsubscribe_blockchain(tips);
    }, true);
    if (chain_ && ! tips) {
        stop();
        co_return;
    }
    auto receive_tips = [&]() -> ::asio::awaitable<void> {
        if ( ! tips) co_return;
        while ( ! stopped_) {
            auto [error, height, incoming, outgoing] = co_await tips->async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (stopped_) co_return;
            if (error || chain_->stopped()) {
                stop();
                co_return;
            }
            if (active_) active_->wake_declarations();
            if (probe_) probe_->wake_declarations();
            (void)wake_.try_send(std::error_code{}, 1);
        }
    };
    auto schedule = [&]() -> ::asio::awaitable<void> {
        // Cancel the pending tip receive before closing and joining it.
        detail::scope_action finish(end_tips, true);
        ::asio::steady_timer deadline(owner_);
        std::exception_ptr failure;
        std::optional<hash_digest> previous;
        auto next_probe = std::chrono::steady_clock::time_point{};
        bool solo = false;
        try {
            while ( ! stopped_) {
                if (active_ && active_->done) active_.reset();
                if (probe_ && probe_->done) probe_.reset();
                auto const now = std::chrono::steady_clock::now();
                bool boundary = ! chain_;
                if (chain_) {
                    auto const view = chain_->chain_view();
                    if (view) {
                        boundary = previous && *previous != view->tip_hash;
                        previous = view->tip_hash;
                    }
                }
                // Observe boundaries even while every entry is held on solo.
                if (boundary && recover_at_boundary()) solo = false;
                if ( ! active_ && ! solo) {
                    if (auto const index = policy_.begin(now)) start_attempt(*index, false);
                    else {
                        solo = true;
                        if ( ! published_) {
                            published_ = true;
                            (void)ready_.try_send(std::make_error_code(std::errc::connection_aborted), 0);
                        }
                        spdlog::info("[sv2 jd] Active entry: solo; all configured entries unavailable");
                    }
                }
                if (active_ && probe_ && active_->index < probe_->index && ! probe_->stopped) {
                    // Join an obsolete candidate before admitting another pair.
                    policy_.release_probe();
                    probe_->stop();
                    update_status();
                }
                auto wake_at = std::chrono::steady_clock::time_point::max();
                if ((solo || (active_ && ! active_->stopped)) && ! probe_) {
                    if (auto const index = policy_.begin_probe(now)) {
                        next_probe = now + std::chrono::seconds(settings_.jd.probe_interval_seconds);
                        start_attempt(*index, true);
                    } else {
                        auto const end = policy_.active().value_or(upstreams_.size());
                        for (size_t index = 0; index < end; ++index) {
                            auto const& entry = policy_.entries()[index];
                            if (entry.health == jd_health::disabled) continue;
                            wake_at = std::min(wake_at, std::max(next_probe, entry.retry_at));
                        }
                    }
                }
                // Without a chain, readiness supplies the test-only boundary.
                // With a chain, recovery still waits for a connected-tip event.
                if ( ! chain_ && probe_ && ! probe_->stopped && probe_->healthy) {
                    auto const& entry = policy_.entries()[probe_->index];
                    if (entry.healthy_since) {
                        wake_at = std::min(wake_at, *entry.healthy_since
                            + std::chrono::seconds(settings_.jd.recovery_hold_seconds));
                    }
                }
                deadline.expires_at(wake_at);
                (void)co_await (deadline.async_wait(::asio::as_tuple(::asio::use_awaitable))
                    || wake_.async_receive(::asio::as_tuple(::asio::use_awaitable)));
            }
        } catch (...) {
            failure = std::current_exception();
        }
        if (active_) { publish_solo(*active_); active_->stop(); }
        if (probe_) probe_->stop();
        co_await ::asio::this_coro::reset_cancellation_state(::asio::disable_cancellation());
        while ((active_ && ! active_->done) || (probe_ && ! probe_->done)) {
            (void)co_await wake_.async_receive(::asio::as_tuple(::asio::use_awaitable));
        }
        if (failure) std::rethrow_exception(failure);
    };
    co_await (schedule() && receive_tips());
}

bool jd_client::dispatch(attempt& value, jd_declaration::result result) {
    if ( ! result.error.empty()) {
        fail(value, jd_failure::transient, std::move(result.error));
        return false;
    }
    auto const now = std::chrono::steady_clock::now();
    if (result.progress) {
        if (value.job_sent && now - *value.job_sent
            > std::chrono::milliseconds(settings_.jd.max_latency_milliseconds)) {
            fail(value, jd_failure::transient, "upstream declaration latency exceeded threshold");
            return false;
        }
        if (value.jobs.pending()) {
            value.job_sent = now;
            value.job_deadline = now + response_timeout_;
        } else {
            value.job_sent.reset();
            value.job_deadline.reset();
        }
        value.wake_declarations();
    }
    if (( ! result.declaration.empty() && ! value.declaration.send(std::move(result.declaration)))
        || ( ! result.mining.empty() && ! value.mining.send(std::move(result.mining)))) {
        fail(value, jd_failure::transient, "upstream output queue refused declaration");
        return false;
    }
    if (active_.get() == &value && value.jobs.work()) publish_pool(value);
    if (result.accepted) {
        if ( ! value.healthy) {
            value.healthy = true;
            policy_.healthy(value.index, now);
            update_status();
            (void)wake_.try_send(std::error_code{}, 1);
        }
        spdlog::info("[sv2 jd] Entry {} accepted local template as job {}", value.index + 1,
            value.jobs.work()->upstream_job_id);
        flush_cached(value);
    }
    // installing is the first state where PushSolution is defined. Full-template
    // enters it at DeclareMiningJob.Success, before SetCustomMiningJob.Success.
    if (value.jobs.can_push_solution() && ! flush_pending_solutions(value)) return false;
    return true;
}

::asio::awaitable<void> jd_client::declare_templates(attempt& value) {
    // Each pair keeps one outstanding declaration and fetches only the latest
    // connected tip after a reply. Neither upstream can delay solo jobs.
    std::optional<hash_digest> previous;
    while ( ! stopped_ && ! value.stopped) {
        value.declaration_changed = false;
        auto const now = std::chrono::steady_clock::now();
        if ((value.job_deadline && now >= *value.job_deadline)
            || ( ! value.submitted.empty() && now - value.submitted.front().sent >= response_timeout_)) {
            fail(value, jd_failure::transient, "upstream message response deadline expired");
            co_return;
        }
        if (chain_ && value.ready && value.jobs.pending()) {
            auto const view = chain_->chain_view();
            if (view && value.jobs.has_template()
                && view->tip_hash != value.jobs.template_value().previous_block_hash) {
                value.jobs.supersede();
                value.job_sent.reset();
                value.job_deadline.reset();
                // That coinbase will not be accepted. Shares and early blocks
                // mined on it must not be sent for the declaration that replaces it.
                retire_cached(value);
            }
        }
        if (chain_ && value.ready && ! value.jobs.pending()) {
            auto const view = chain_->chain_view();
            if (view && previous != view->tip_hash && value.mining_session.channel()) {
                // Reserve the largest token output vector before selection.
                // Each pair declares only the latest local connected template.
                auto tmpl = co_await chain_->fetch_mining_template(248 + uint64_t{0xffff}, 0);
                if (stopped_ || value.stopped) co_return;
                auto const latest = chain_->chain_view();
                // The tip can move while the template is built. Do not declare
                // a parent the chain has already left, and do not start a
                // second declaration over one that completed during the fetch.
                if (tmpl && latest && latest->tip_hash == tmpl->previous_block_hash
                    && ! value.jobs.pending() && value.mining_session.channel()) {
                    previous = tmpl->previous_block_hash;
                    value.work_id = 0;
                    // start() drops the previous declared coinbase before its
                    // token exists. A cached share or early block from that
                    // coinbase must not be flushed under the next job.
                    retire_cached(value);
                    if ( ! dispatch(value, value.jobs.start(std::move(*tmpl), *value.mining_session.channel()))) co_return;
                }
            }
        }
        // A tip or reply can arrive while the template fetch is suspended.
        // Remember that wake even when there was no timer wait to cancel.
        if (value.declaration_changed) continue;
        auto wake_at = value.job_deadline.value_or(std::chrono::steady_clock::time_point::max());
        if ( ! value.submitted.empty()) {
            wake_at = std::min(wake_at, value.submitted.front().sent + response_timeout_);
        }
        value.declaration_wake.expires_at(wake_at);
        (void)co_await value.declaration_wake.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }
}

bool jd_client::share_response(attempt& value, message_header const& header, byte_span payload) {
    auto const acknowledgement = header.msg_type == submit_shares_success::message_type;
    auto const kind = acknowledgement ? "acknowledgement" : "refusal";
    auto refuse = [&](char const* detail) {
        fail(value, jd_failure::transient,
            std::string("invalid upstream share ") + kind + ": " + detail);
        return false;
    };
    // The noise frame is one SV2 message. A length that does not match the
    // header was not parsed, whatever from_data would have done with the bytes.
    if (header.msg_length != payload.size()) return refuse("invalid share reply length");
    auto const upstream = value.mining_session.channel()
        ? std::optional<uint32_t>{value.mining_session.channel()->channel_id} : std::nullopt;
    auto const now = std::chrono::steady_clock::now();
    if (acknowledgement) {
        auto const message = parse<submit_shares_success>(payload);
        if ( ! message) return refuse("malformed share success");
        if ( ! upstream || message->channel_id != *upstream) {
            return refuse("share success for another channel");
        }
        auto const last = std::find_if(value.submitted.begin(), value.submitted.end(),
            [&](auto const& share) { return share.sequence == message->last_sequence_number; });
        // The count has to name the same prefix the health window is about to
        // sample. A gap would score the wrong shares, or none.
        if (last == value.submitted.end()
            || message->new_submits_accepted_count
                != static_cast<size_t>(last - value.submitted.begin()) + 1) {
            return refuse("share success does not match the outstanding submits");
        }
        auto const count = message->new_submits_accepted_count;
        for (uint32_t i = 0; i < count; ++i) {
            auto const latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - value.submitted.front().sent);
            if (auto const reason = value.health.sample(false, false, latency)) {
                fail(value, jd_failure::transient, *reason);
                return false;
            }
            value.submitted.pop_front();
        }
        value.wake_declarations();
        return true;
    }
    if (header.msg_type != submit_shares_error::message_type) return refuse("unexpected share reply");
    auto const message = parse<submit_shares_error>(payload);
    if ( ! message) return refuse("malformed share error");
    if ( ! upstream || message->channel_id != *upstream) return refuse("share error for another channel");
    auto const code = printable_error_code(message->error_code);
    auto const found = std::find_if(value.tracked.begin(), value.tracked.end(),
        [&](attempt::tracked_share const& share) {
            return share.sequence == message->sequence_number;
        });
    // Never sent, or already pushed out of the bounded record.
    if (found == value.tracked.end()) {
        spdlog::debug("[sv2 jd] ignoring share error for unknown sequence {}",
            message->sequence_number);
        return true;
    }
    auto const generation = found->generation;
    auto const previous = found->previous;
    auto const sent = found->sent;
    auto const block = found->block;
    auto const outstanding = std::find_if(value.submitted.begin(), value.submitted.end(),
        [&](auto const& share) { return share.sequence == message->sequence_number; });
    auto consume = [&] {
        value.tracked.erase(found);
        if (outstanding != value.submitted.end()) value.submitted.erase(outstanding);
        value.wake_declarations();
    };
    auto const& work = value.jobs.work();
    // The declaration this share was mined on has been replaced, or it is not
    // the one the pool acknowledged. An honest race, not a refusal of current work.
    if ( ! work || ! work->upstream_accepted || generation != work->generation) {
        spdlog::debug("[sv2 jd] stale share error {} for sequence {} on a replaced declaration",
            code, message->sequence_number);
        // The gap includes the replacement, so it is not the pool's response
        // time for the job it is paying now.
        if (auto const reason = value.health.sample(true, true, std::chrono::milliseconds::zero())) {
            fail(value, jd_failure::transient, *reason);
            return false;
        }
        consume();
        return true;
    }
    auto const view = chain_ ? chain_->chain_view() : nullptr;
    // The share that found a block moves the tip. The pool can reach that tip
    // before this node does, from the PushSolution or from the network, and
    // answer the share as an old job. That is the stale race below, not a
    // refusal of current work.
    if ( ! block && ( ! view || view->tip_hash == previous)) {
        fail(value, jd_failure::dishonest,
            "pool refused a valid share on an accepted job (" + code + ")");
        return false;
    }
    if (block) {
        spdlog::debug("[sv2 jd] share error {} for the block found by sequence {} counts as stale",
            code, message->sequence_number);
    }
    bool const stale = message->error_code == "stale-share"
        || message->error_code == "invalid-job-id";
    auto const latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - sent);
    if (auto const reason = value.health.sample(stale, true, latency)) {
        fail(value, jd_failure::transient, *reason);
        return false;
    }
    consume();
    return true;
}

::asio::awaitable<void> jd_client::run() {
    detail::scope_action finish([this] { ready_.cancel(); ready_.close(); }, true);
    co_await ::asio::co_spawn(owner_, run_owned(), ::asio::use_awaitable);
}

void jd_client::stop() {
    stopped_ = true;
    // Early blocks are flushed only on this strand. Drop them before a
    // DeclareMiningJob.Success already in hand can push one after stop.
    // The schedule still ends the tip wait; this is not a second cancel hook.
    auto drop_blocks = [this] {
        if (active_) active_->pending_solutions.clear();
        if (probe_) probe_->pending_solutions.clear();
    };
    if (owner_.running_in_this_thread()) drop_blocks();
    else ::asio::post(owner_, drop_blocks);
    (void)wake_.try_send(std::error_code{}, 1);
    (void)ready_.try_send(std::make_error_code(std::errc::operation_canceled), 0);
}

boost::shared_ptr<jd_notice const> jd_client::notice() const {
    return notice_.load();
}

boost::shared_ptr<jd_status const> jd_client::status() const {
    return status_.load();
}

std::shared_ptr<jd_client::publication_channel> jd_client::subscribe_publications() {
    auto channel = publications_.subscribe();
    (void)channel->try_send(std::error_code{}, 1);
    return channel;
}

void jd_client::unsubscribe_publications(std::shared_ptr<publication_channel> const& channel) {
    publications_.unsubscribe(channel);
}

void jd_client::observe_share(mining::pool_proof proof) {
    ::asio::post(owner_, [this, proof = std::move(proof)] {
        if (stopped_ || ! active_ || active_->stopped) return;
        note_share(*active_, proof);
    });
}

void jd_client::publish_pool(attempt& value) {
    if (stopped_ || value.stopped) return;
    auto const& work = value.jobs.work();
    if ( ! work) return;
    if (chain_) {
        auto const view = chain_->chain_view();
        if ( ! view || view->tip_hash != work->tmpl.previous_block_hash) return;
    }
    auto const current = notice_.load();
    if (value.work_id != 0 && current && current->pool && current->pooled.work_id == value.work_id) return;
    auto next = boost::make_shared<jd_notice>();
    next->pool = true;
    next->previous = work->tmpl.previous_block_hash;
    next->pooled = *work;
    next->generation = (current ? current->generation : 0) + 1;
    next->pooled.work_id = next->generation;
    value.work_id = next->generation;
    value.pool_live = true;
    notice_.store(next);
    publications_.publish(1);
}

void jd_client::publish_solo(attempt const& value) {
    auto const current = notice_.load();
    if ( ! value.pool_live || ! current || ! current->pool) return;
    auto next = boost::make_shared<jd_notice>();
    next->solo = current->pooled.tmpl;
    next->previous = next->solo.previous_block_hash;
    next->generation = current->generation + 1;
    notice_.store(next);
    publications_.publish(1);
    spdlog::info("[sv2 jd] Active payout: solo");
}

void jd_client::note_share(attempt& value, mining::pool_proof const& proof) {
    // A proof already posted by a downstream connection can arrive after a
    // switch. Its publication id must still name this exact declared job.
    if (value.work_id == 0 || proof.work_id != value.work_id) return;
    auto const& work = value.jobs.work();
    // generation is the declared coinbase, not the downstream publication id.
    // A share mined on another declaration must not be cached or submitted
    // against the work installed now.
    bool const current = work && proof.generation == work->generation;
    // Spec 6.4.9: PushSolution is defined once the JDS has accepted the
    // declaration (DeclareMiningJob.Success), which is full-template only.
    // A block found earlier is held until that reply. Coinbase-only still
    // connects the block locally and never pushes. The full extranonce is
    // the pool prefix plus the nested miner bytes. BCH has no witness bytes.
    if (proof.block && proof.full_extranonce.size() <= 32 && current) {
        if (value.jobs.can_push_solution()) {
            if ( ! push_block(value, proof)) return;
        } else if (value.jobs.full_template()
            && value.pending_solutions.size() < max_pending_solutions) {
            // Only this coinbase can become the job the JDS accepts.
            value.pending_solutions.push_back(proof);
        }
    }
    if ( ! current || to_uint256(proof.header_hash) > to_uint256(work->upstream_target)) return;
    // SetCustomMiningJob.Success may assign job id zero. That is still accepted.
    if ( ! work->upstream_accepted) {
        if (value.cached.size() < max_cached_shares) value.cached.push_back(proof);
        return;
    }
    forward_share(value, proof);
}

void jd_client::forward_share(attempt& value, mining::pool_proof const& proof) {
    auto const& work = value.jobs.work();
    if ( ! work || ! work->upstream_accepted || proof.generation != work->generation
        || proof.work_id != value.work_id
        || proof.upstream_extranonce.size() != work->upstream_extranonce_size) {
        return;
    }
    if (value.submitted.size() == max_pending_shares) {
        fail(value, jd_failure::transient, "upstream share acknowledgement history exhausted");
        return;
    }
    submit_shares_extended message;
    message.channel_id = work->upstream_channel_id;
    message.sequence_number = value.sequence++;
    message.job_id = work->upstream_job_id;
    message.nonce = proof.nonce;
    message.ntime = proof.ntime;
    message.version = proof.version;
    message.extranonce = proof.upstream_extranonce;
    if ( ! value.mining.send(frame(message))) {
        fail(value, jd_failure::transient, "pool output queue refused a share");
        return;
    }
    auto const sent = std::chrono::steady_clock::now();
    value.submitted.push_back({message.sequence_number, proof.prev_hash, sent});
    remember_submitted(value, message.sequence_number, proof.generation, proof.prev_hash, sent,
        proof.block);
    value.wake_declarations();
}

void jd_client::remember_submitted(attempt& value, uint32_t sequence, uint32_t generation,
    hash_digest const& previous, std::chrono::steady_clock::time_point sent, bool block) {
    if (value.tracked.size() >= max_tracked_shares) value.tracked.pop_front();
    value.tracked.push_back(attempt::tracked_share{sequence, generation, previous, sent, block});
}

bool jd_client::push_block(attempt& value, mining::pool_proof const& proof) {
    if (stopped_ || value.stopped) return false;
    push_solution message;
    message.extranonce = proof.full_extranonce;
    message.prev_hash = proof.prev_hash;
    message.nonce = proof.nonce;
    message.ntime = proof.ntime;
    message.nbits = proof.bits;
    message.version = proof.version;
    if ( ! value.declaration.send(frame(message))) {
        fail(value, jd_failure::transient, "JDS output queue refused PushSolution");
        return false;
    }
    return true;
}

bool jd_client::flush_pending_solutions(attempt& value) {
    if ( ! value.jobs.can_push_solution()) return true;
    auto const& work = value.jobs.work();
    std::vector<mining::pool_proof> pending;
    pending.swap(value.pending_solutions);
    if ( ! work) return true;
    for (auto const& proof : pending) {
        // Replaced while the proof sat here: not the job the JDS just accepted.
        if (proof.generation != work->generation) continue;
        if ( ! push_block(value, proof)) return false;
    }
    return true;
}

void jd_client::flush_cached(attempt& value) {
    auto const& work = value.jobs.work();
    if ( ! work || ! work->upstream_accepted) return;
    std::vector<mining::pool_proof> pending;
    pending.swap(value.cached);
    for (auto const& proof : pending) {
        if (value.stopped) break;
        // Replacement also drops the cache. This still refuses a proof that
        // was queued for an older coinbase and noted after the new work existed.
        if (proof.generation != work->generation) continue;
        if (to_uint256(proof.header_hash) <= to_uint256(work->upstream_target)) {
            forward_share(value, proof);
        }
    }
}

void jd_client::retire_cached(attempt& value) {
    value.cached.clear();
    value.pending_solutions.clear();
}

::asio::awaitable<void> jd_client::wait_until_ready() {
    (void)co_await ready_.async_receive(::asio::use_awaitable);
}

} // namespace kth::node::sv2
