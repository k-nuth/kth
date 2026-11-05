// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_client.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <boost/smart_ptr/make_shared.hpp>

#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
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

jd_client::jd_client(sv2_settings const& settings, ::asio::any_io_executor executor,
    blockchain::block_chain* chain)
    : owner_(::asio::make_strand(executor))
    , mining_(owner_, endpoint(settings.jd.mining_address).host,
        endpoint(settings.jd.mining_address).port,
        authority(settings.jd.mining_authority_public_key),
        std::chrono::seconds(settings.handshake_timeout_seconds))
    , jds_host_(endpoint(settings.jd.jds_address).host)
    , jds_port_(endpoint(settings.jd.jds_address).port)
    , jds_authority_(authority(settings.jd.jds_authority_public_key))
    , jds_handshake_timeout_(std::chrono::seconds(settings.handshake_timeout_seconds))
    , declaration_(owner_, jds_host_, jds_port_, jds_authority_, jds_handshake_timeout_)
    , jds_replacement_()
    , declaration_io_(&declaration_)
    , mining_session_(jd_peer::mining, endpoint(settings.jd.mining_address).host,
        endpoint(settings.jd.mining_address).port, settings.jd.user_identity,
        settings.jd.mode == "full-template")
    , declaration_session_(jd_peer::declaration, jds_host_, jds_port_,
        settings.jd.user_identity, settings.jd.mode == "full-template")
    , jobs_(settings.jd.user_identity, settings.jd.mode == "full-template")
    , chain_(chain)
    , response_timeout_(settings.jd.response_timeout_seconds)
    , declaration_wake_(owner_)
    , ready_(owner_, 1)
    , publications_(owner_, 1)
{}

::asio::awaitable<void> jd_client::serve(jd_connection& connection,
    jd_session& session, char const* name, bool* negotiation_timeout) {
    using namespace ::asio::experimental::awaitable_operators;

    ::asio::steady_timer deadline(owner_);
    deadline.expires_at(std::chrono::steady_clock::time_point::max());
    bool done = false;
    bool timed_out = false;
    bool replace_jds = false;
    std::string failure;
    auto connected = [&] {
        if (stopped_) return false;
        deadline.expires_after(response_timeout_);
        return connection.send(session.start());
    };
    auto received = [&](data_chunk bytes) {
        if (stopped_) return false;
        byte_reader source(bytes);
        auto const header = message_header::from_data(source);
        if ( ! header) return false;
        auto const payload = source.read_bytes(source.remaining_size());
        if ( ! payload) return false;
        auto const peer = &session == &mining_session_ ? jd_peer::mining : jd_peer::declaration;
        // Share replies name one submission. Success leaves the job in place.
        // An error falls back only for a share on the accepted declaration
        // (sv2-spec 6.2); a stale or unknown sequence must not end JD mining.
        if (session.ready() && peer == jd_peer::mining && header->channel_msg()
            && header->extension() == 0
            && (header->msg_type == submit_shares_success::message_type
                || header->msg_type == submit_shares_error::message_type)) {
            // feed() delivers exactly the U24 payload_size bytes, and that U24
            // is the msg_length just parsed. A short share body fails in share_reply.
            if (auto reason = share_reply(*header, *payload)) {
                failure = std::move(*reason);
                return false;
            }
            return true;
        }
        bool const payout = header->extension() == requested_payout_outputs_extension
            && peer == jd_peer::declaration;
        if (session.ready() && ((header->extension() == 0
                && (peer == jd_peer::declaration
                    || header->msg_type == set_custom_mining_job_success::message_type
                    || header->msg_type == set_custom_mining_job_error::message_type))
            || payout)) {
            return dispatch(jobs_.process(peer, *header, *payload));
        }
        auto const previous_prefix = mining_session_.channel()
            ? mining_session_.channel()->extranonce_prefix : data_chunk{};
        auto result = session.process(*header, *payload);
        if (peer == jd_peer::declaration && session.ready()) {
            jobs_.use_payout_outputs(session.payout_outputs());
        }
        if (result.close) {
            failure = result.error;
            return false;
        }
        if (session.ready() && peer == jd_peer::mining && (jobs_.pending() || jobs_.work())
            && previous_prefix != mining_session_.channel()->extranonce_prefix) {
            failure = "pool changed the extranonce prefix of declared work";
            return false;
        }
        if (session.ready() && peer == jd_peer::mining) jobs_.note_target(mining_session_.channel()->target);
        if ( ! result.response.empty()) {
            // The next request gets a fresh deadline. Extensions and partial
            // input cannot extend the wait for the response already owed.
            deadline.expires_after(response_timeout_);
            if ( ! connection.send(std::move(result.response))) return false;
        }
        if (session.ready()) {
            deadline.cancel();
            if ( ! published_ && mining_session_.ready() && declaration_session_.ready()) {
                published_ = true;
                (void)ready_.try_send(std::error_code{}, 1);
                spdlog::info("[sv2 jd] Both upstreams ready; starting local declarations");
                wake_declarations();
            }
        }
        return true;
    };
    auto exchange = [&]() -> ::asio::awaitable<void> {
        detail::scope_action finish([&] {
            done = true;
            deadline.cancel();
        }, true);
        try {
            auto reason = co_await connection.run(connected, received);
            if (failure.empty()) failure = std::move(reason);
        } catch (std::exception const& e) {
            failure = e.what();
        }
        // This close is the negotiation timeout, not a failed attempt. stop()
        // would shut the pool before the replacement connection exists.
        if (replace_jds) co_return;
        // Stopping either side consumes this attempt. There is no other
        // reconnect and no alternate pool, and no upstream can stall solo work.
        stop();
    };
    auto response_deadline = [&]() -> ::asio::awaitable<void> {
        while ( ! done && ! session.ready()) {
            auto [error] = co_await deadline.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (done || session.ready()) co_return;
            if (error == ::asio::error::operation_aborted) continue;
            // Unanswered RequestExtensions must not be followed by a Job
            // Declaration frame on this socket. Close only it. A null pointer
            // is the replacement connection, which never negotiates, or the
            // pool: both stay on the terminal path below.
            if (negotiation_timeout && &session == &declaration_session_
                && session.awaiting_extensions()) {
                replace_jds = true;
                connection.stop();
                co_return;
            }
            timed_out = true;
            connection.stop();
            co_return;
        }
    };
    co_await (exchange() && response_deadline());
    if (replace_jds) {
        *negotiation_timeout = true;
        co_return;
    }
    if ( ! published_) {
        published_ = true;
        (void)ready_.try_send(std::make_error_code(std::errc::connection_aborted), 0);
    }
    spdlog::warn("[sv2 jd] {}: {}; continuing solo mining", name,
        timed_out ? "setup response deadline expired" : failure);
}

::asio::awaitable<void> jd_client::serve_declaration() {
    // The first connection negotiates. Silence returns here with the pool still
    // up and that run() already joined, so the JDS has seen the close.
    bool negotiation_timeout = false;
    co_await serve(declaration_, declaration_session_, "JDS", &negotiation_timeout);
    if ( ! negotiation_timeout || stopped_) co_return;
    // serve() sets negotiation_timeout only after awaiting_extensions() was
    // true, then returns without another session message. The restart holds.
    KTH_CONTRACT(declaration_session_.restart_without_extensions());
    // Same endpoint as the connection that just closed. run() is one-shot, so
    // this is a new object rather than a second run of declaration_.
    jds_replacement_ = std::make_unique<jd_connection>(owner_, jds_host_, jds_port_,
        jds_authority_, jds_handshake_timeout_);
    declaration_io_.store(jds_replacement_.get());
    // A pool stop() in this gap already ran, or runs after the store and closes
    // the new socket. Either way the handshake must not proceed.
    if (stopped_) {
        jds_replacement_->stop();
        co_return;
    }
    spdlog::info("[sv2 jd] No RequestExtensions answer within sv2.jd.response_timeout_seconds; replacing the JDS connection");
    co_await serve(*jds_replacement_, declaration_session_, "JDS");
}

bool jd_client::send_declaration(data_chunk frame) {
    return declaration_io_.load()->send(std::move(frame));
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
    // The hook is stored on the client, so it has to own the channel.
    // Invoked only on owner_.
    auto const end_tips = [tips] {
        if (tips) {
            tips->cancel();
            tips->close();
        }
    };
    detail::scope_action cleanup([&] {
        end_tips();
        cancel_tips_ = {};
        if (chain_) chain_->unsubscribe_blockchain(tips);
    }, true);
    cancel_tips_ = end_tips;
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
            wake_declarations();
        }
    };
    auto declarations = [&]() -> ::asio::awaitable<void> {
        // Drop the subscription before the join observes this child finishing.
        detail::scope_action finish(end_tips, true);
        co_await declare_templates();
    };
    co_await (serve(mining_, mining_session_, "pool")
        && serve_declaration()
        && declarations() && receive_tips());
}

void jd_client::wake_declarations() {
    declaration_changed_ = true;
    declaration_wake_.cancel();
}

bool jd_client::dispatch(jd_declaration::result result) {
    if ( ! result.error.empty()) {
        spdlog::warn("[sv2 jd] {}; continuing solo mining", result.error);
        stop();
        return false;
    }
    if (result.progress) {
        if (jobs_.pending()) job_deadline_ = std::chrono::steady_clock::now() + response_timeout_;
        else job_deadline_.reset();
        wake_declarations();
    }
    if (( ! result.declaration.empty() && ! send_declaration(std::move(result.declaration)))
        || ( ! result.mining.empty() && ! mining_.send(std::move(result.mining)))) {
        spdlog::warn("[sv2 jd] Upstream output queue refused declaration; continuing solo mining");
        stop();
        return false;
    }
    // Publish after the frame is queued. A full queue must not show downstream
    // miners a coinbase the pool never received.
    if (jobs_.work()) publish_pool();
    if (result.accepted) {
        spdlog::info("[sv2 jd] Pool accepted local template as job {}",
            jobs_.work()->upstream_job_id);
        flush_cached();
    }
    // installing is the first state where PushSolution is defined. Full-template
    // enters it at DeclareMiningJob.Success, before SetCustomMiningJob.Success.
    if (jobs_.can_push_solution() && ! flush_pending_solutions()) return false;
    return true;
}

::asio::awaitable<void> jd_client::declare_templates() {
    // This loop has no downstream await and only one outstanding declaration.
    // A slow JDS cannot pin a queue of old templates or delay solo jobs. After
    // the response, fetch only the latest connected tip's own selection.
    std::optional<hash_digest> previous;
    while ( ! stopped_) {
        declaration_changed_ = false;
        if (job_deadline_ && std::chrono::steady_clock::now() >= *job_deadline_) {
            spdlog::warn("[sv2 jd] Declaration response deadline expired; continuing solo mining");
            stop();
            co_return;
        }
        if (chain_ && mining_session_.ready() && declaration_session_.ready() && jobs_.pending()) {
            auto const view = chain_->chain_view();
            if (view && jobs_.has_template()
                && view->tip_hash != jobs_.template_value().previous_block_hash) {
                jobs_.supersede();
                job_deadline_.reset();
                // That coinbase will not be accepted. Shares mined on it must
                // not stay cached for the declaration that replaces it.
                retire_cached();
            }
        }
        if (chain_ && mining_session_.ready() && declaration_session_.ready() && ! jobs_.pending()) {
            auto const view = chain_->chain_view();
            if (view && previous != view->tip_hash && mining_session_.channel()) {
                // Allocation outputs fit B0_64K. Reserve their maximum size
                // before selecting transactions; the builder checks the exact
                // completed coinbase against the returned block limit.
                auto value = co_await chain_->fetch_mining_template(248 + uint64_t{0xffff}, 0);
                if (stopped_) co_return;
                auto const latest = chain_->chain_view();
                // The tip can move while the template is built. Do not declare
                // a parent the chain has already left, and do not start a
                // second declaration over one that completed during the fetch.
                if (value && latest && latest->tip_hash == value->previous_block_hash
                    && ! jobs_.pending() && mining_session_.channel()) {
                    previous = value->previous_block_hash;
                    // start() drops the previous declared coinbase before its
                    // token exists. A cached share from that coinbase must not
                    // be flushed under the next job id.
                    retire_cached();
                    if ( ! dispatch(jobs_.start(std::move(*value), *mining_session_.channel()))) co_return;
                }
            }
        }
        // A tip or reply can arrive while the template fetch is suspended.
        // Remember that wake even when there was no timer wait to cancel.
        if (declaration_changed_) continue;
        declaration_wake_.expires_at(job_deadline_.value_or(
            std::chrono::steady_clock::time_point::max()));
        (void)co_await declaration_wake_.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }
}

::asio::awaitable<void> jd_client::run() {
    detail::scope_action finish([this] {
        ready_.cancel();
        ready_.close();
    }, true);
    co_await ::asio::co_spawn(owner_, run_owned(), ::asio::use_awaitable);
}

void jd_client::stop() {
    bool const first = ! stopped_.exchange(true);
    if (first) {
        // Declaration state is only touched on this strand. A stop from the
        // listener posts the payout switch; one from this strand does it now,
        // before run() can return and destroy the client. The same posting
        // drops a block held for DeclareMiningJob.Success, so fallback cannot
        // push it afterwards.
        if (owner_.running_in_this_thread()) {
            pending_solutions_.clear();
            wake_declarations();
            if (cancel_tips_) cancel_tips_();
            if (pool_live_) publish_solo();
        } else {
            ::asio::post(owner_, [this] {
                pending_solutions_.clear();
                wake_declarations();
                if (cancel_tips_) cancel_tips_();
                if (pool_live_) publish_solo();
            });
        }
    }
    mining_.stop();
    // declaration_io_ is declaration_ until the replacement is published.
    // A pool stop() after that publish has to close the new socket too.
    auto* const declaration = declaration_io_.load();
    declaration->stop();
    if (declaration != &declaration_) declaration_.stop();
    (void)ready_.try_send(std::make_error_code(std::errc::operation_canceled), 0);
    ready_.cancel();
    ready_.close();
}

boost::shared_ptr<jd_notice const> jd_client::notice() const {
    return notice_.load();
}

std::shared_ptr<jd_client::publication_channel> jd_client::subscribe_publications() {
    auto channel = publications_.subscribe();
    // Registration and this wake-up cover a publication before the first wait.
    (void)channel->try_send(std::error_code{}, 1);
    return channel;
}

void jd_client::unsubscribe_publications(std::shared_ptr<publication_channel> const& channel) {
    publications_.unsubscribe(channel);
}

void jd_client::observe_share(mining::pool_proof proof) {
    ::asio::post(owner_, [this, proof = std::move(proof)] {
        if (stopped_) return;
        note_share(proof);
    });
}

void jd_client::publish_pool() {
    if (stopped_) return;
    auto const& work = jobs_.work();
    if ( ! work) return;
    auto const current = notice_.load();
    if (current && current->pool && current->pooled.generation == work->generation
        && current->pooled.coinbase_tx_prefix == work->coinbase_tx_prefix
        && current->pooled.coinbase_tx_suffix == work->coinbase_tx_suffix
        && current->pooled.tmpl.previous_block_hash == work->tmpl.previous_block_hash
        && current->pooled.upstream_extranonce_prefix == work->upstream_extranonce_prefix
        && current->pooled.upstream_extranonce_size == work->upstream_extranonce_size) {
        return;
    }
    auto next = boost::make_shared<jd_notice>();
    next->pool = true;
    next->previous = work->tmpl.previous_block_hash;
    next->pooled = *work;
    next->generation = (current ? current->generation : 0) + 1;
    notice_.store(next);
    pool_live_ = true;
    // A full slot already asks the consumer to load the latest notice.
    publications_.publish(1);
}

void jd_client::publish_solo() {
    if ( ! pool_live_) return;
    pool_live_ = false;
    if ( ! jobs_.has_template()) return;
    auto const current = notice_.load();
    auto next = boost::make_shared<jd_notice>();
    next->pool = false;
    next->solo = jobs_.template_value();
    next->previous = next->solo.previous_block_hash;
    next->generation = (current ? current->generation : 0) + 1;
    notice_.store(next);
    publications_.publish(1);
    spdlog::info("[sv2 jd] Downstream work pays the solo address again");
}

void jd_client::note_share(mining::pool_proof const& proof) {
    // Spec 6.4.9: PushSolution is defined once the JDS has accepted the
    // declaration (DeclareMiningJob.Success), which is full-template only.
    // A block found earlier is held until that reply. Coinbase-only still
    // connects the block locally and never pushes. The full extranonce is
    // the pool prefix plus the nested miner bytes. BCH has no witness bytes.
    if (proof.block && proof.full_extranonce.size() <= 32) {
        if (jobs_.can_push_solution()) {
            if ( ! push_block(proof)) return;
        } else if (jobs_.full_template()) {
            auto const& declared = jobs_.work();
            // Only this coinbase can become the job the JDS accepts. A proof
            // from another generation would be dropped at the flush.
            if (declared && proof.generation == declared->generation
                && pending_solutions_.size() < max_pending_solutions) {
                pending_solutions_.push_back(proof);
            }
        }
    }
    auto const& work = jobs_.work();
    // A share mined on another declaration's coinbase must not be cached or
    // submitted against the work installed now. generation is the declared
    // coinbase, not the downstream job id.
    if ( ! work || proof.generation != work->generation
        || to_uint256(proof.header_hash) > to_uint256(work->upstream_target)) return;
    if ( ! work->upstream_accepted) {
        if (cached_.size() < max_cached_shares) cached_.push_back(proof);
        return;
    }
    forward_share(proof);
}

void jd_client::forward_share(mining::pool_proof const& proof) {
    auto const& work = jobs_.work();
    if ( ! work || ! work->upstream_accepted || proof.generation != work->generation
        || proof.upstream_extranonce.size() != work->upstream_extranonce_size) {
        return;
    }
    submit_shares_extended message;
    message.channel_id = work->upstream_channel_id;
    message.sequence_number = share_sequence_++;
    message.job_id = work->upstream_job_id;
    message.nonce = proof.nonce;
    message.ntime = proof.ntime;
    message.version = proof.version;
    message.extranonce = proof.upstream_extranonce;
    if ( ! mining_.send(frame(message))) {
        spdlog::warn("[sv2 jd] Pool output queue refused a share; continuing solo mining");
        stop();
        return;
    }
    remember_submitted(message.sequence_number, proof.generation, proof.block);
}

void jd_client::remember_submitted(uint32_t sequence, uint32_t generation, bool block) {
    if (submitted_.size() >= max_tracked_shares) submitted_.pop_front();
    submitted_.push_back(submitted_share{sequence, generation, block});
}

std::optional<std::string> jd_client::share_reply(message_header const& header,
    byte_span payload) {
    auto const upstream = mining_session_.channel()
        ? std::optional<uint32_t>{mining_session_.channel()->channel_id} : std::nullopt;
    if (header.msg_type == submit_shares_success::message_type) {
        auto const message = parse<submit_shares_success>(payload);
        if ( ! message) return "malformed share success";
        if ( ! upstream || message->channel_id != *upstream) {
            return "share success for another channel";
        }
        return std::nullopt;
    }
    // The mining handler calls this only for Success or Error. Success returned above.
    auto const message = parse<submit_shares_error>(payload);
    if ( ! message) return "malformed share error";
    if ( ! upstream || message->channel_id != *upstream) return "share error for another channel";
    auto const code = printable_error_code(message->error_code);
    auto const found = std::find_if(submitted_.begin(), submitted_.end(),
        [&](submitted_share const& share) { return share.sequence == message->sequence_number; });
    // Never sent, or already pushed out of the bounded record.
    if (found == submitted_.end()) {
        spdlog::debug("[sv2 jd] ignoring share error for unknown sequence {}",
            message->sequence_number);
        return std::nullopt;
    }
    // The share that found a block moves the tip. The pool can reach that tip
    // first, from this node's PushSolution or from the network, and then
    // answer the share as an old job. An honest race, not a refused share.
    if (found->block) {
        spdlog::debug("[sv2 jd] ignoring share error {} for the block found by sequence {}",
            code, message->sequence_number);
        return std::nullopt;
    }
    auto const& work = jobs_.work();
    // The declaration this share was mined on has been replaced or is not the
    // one the pool acknowledged. An honest race, not a refusal of current work.
    if ( ! work || ! work->upstream_accepted || found->generation != work->generation) {
        spdlog::debug("[sv2 jd] ignoring stale share error {} for sequence {}",
            code, message->sequence_number);
        return std::nullopt;
    }
    return "pool refused a valid share on an accepted job (" + code + ")";
}

bool jd_client::push_block(mining::pool_proof const& proof) {
    if (stopped_) return false;
    push_solution message;
    message.extranonce = proof.full_extranonce;
    message.prev_hash = proof.prev_hash;
    message.nonce = proof.nonce;
    message.ntime = proof.ntime;
    message.nbits = proof.bits;
    message.version = proof.version;
    if ( ! send_declaration(frame(message))) {
        spdlog::warn("[sv2 jd] JDS output queue refused PushSolution; continuing solo mining");
        stop();
        return false;
    }
    return true;
}

bool jd_client::flush_pending_solutions() {
    if ( ! jobs_.can_push_solution()) return true;
    auto const& work = jobs_.work();
    std::vector<mining::pool_proof> pending;
    pending.swap(pending_solutions_);
    if ( ! work) return true;
    for (auto const& proof : pending) {
        // Replaced while the proof sat here: not the job the JDS just accepted.
        if (proof.generation != work->generation) continue;
        if ( ! push_block(proof)) return false;
    }
    return true;
}

void jd_client::flush_cached() {
    auto const& work = jobs_.work();
    if ( ! work || ! work->upstream_accepted) return;
    std::vector<mining::pool_proof> pending;
    pending.swap(cached_);
    for (auto const& proof : pending) {
        // Replacement also drops the cache. This still refuses a proof that
        // was queued for an older coinbase and noted after the new work existed.
        if (proof.generation != work->generation) continue;
        if (to_uint256(proof.header_hash) <= to_uint256(work->upstream_target)) {
            forward_share(proof);
        }
    }
}

void jd_client::retire_cached() {
    cached_.clear();
    pending_solutions_.clear();
}

::asio::awaitable<void> jd_client::wait_until_ready() {
    (void)co_await ready_.async_receive(::asio::use_awaitable);
}

} // namespace kth::node::sv2
