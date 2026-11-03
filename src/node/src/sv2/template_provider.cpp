// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/template_provider.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/ip/address.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/detail/scope_action.hpp>
#include <kth/node/mining/block_submit.hpp>
#include <kth/node/mining/work.hpp>
#include <kth/node/sv2/connection.hpp>
#include <kth/node/sv2/mining_session.hpp>

namespace kth::node::sv2 {

namespace {

// The only protocol version defined so far (sv2-spec 3.6.1).
constexpr uint16_t protocol_version = 2;

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{0, Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

// A payload is valid only if it is exactly one message, with no bytes left.
template <typename Message>
std::optional<Message> parse(byte_span payload) {
    byte_reader source(payload);
    auto message = Message::from_data(source);
    if ( ! message || ! source.is_exhausted()) {
        return std::nullopt;
    }
    return std::move(*message);
}

hash_digest merkle_parent(hash_digest const& left, hash_digest const& right) {
    return bitcoin_hash(build_chunk({left, right}));
}

// Certificate timestamps are U32 seconds (sv2-spec 4.5.2).
std::optional<uint32_t> unix_time() {
    auto const now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < 0 || now > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    return static_cast<uint32_t>(now);
}

bool mining_setup(message_header const& header, byte_span payload) {
    if (header.extension_type != 0 || header.msg_type != setup_connection::message_type
        || header.msg_length != payload.size()) {
        return false;
    }
    byte_reader source(payload);
    auto const message = setup_connection::from_data(source);
    return message && source.is_exhausted()
        && message->protocol == setup_connection::protocol_mining;
}

std::vector<hash_digest> coinbase_merkle_path(std::vector<transaction_const_ptr> const& txs) {
    // The unknown coinbase occupies index zero. Its hash does not affect any
    // sibling on its branch, so a placeholder gives the same path.
    std::vector<hash_digest> hashes{null_hash};
    for (auto const& tx : txs) hashes.push_back(tx->hash());
    std::vector<hash_digest> path;
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) hashes.push_back(hashes.back());
        path.push_back(hashes[1]);
        auto const pairs = hashes.size() / 2;
        for (size_t i = 0; i < pairs; ++i) {
            hashes[i] = merkle_parent(hashes[2 * i], hashes[2 * i + 1]);
        }
        hashes.resize(pairs);
    }
    return path;
}

} // namespace

void template_provider_session::set_tip(hash_digest const& previous) {
    if (previous_ && *previous_ != previous) {
        stale_through_ = next_id_ - 1;
        templates_.clear();
    }
    previous_ = previous;
}

template_provider_session::entry const*
template_provider_session::find_template(uint64_t id) const {
    auto const it = std::find_if(templates_.begin(), templates_.end(),
        [id](auto const& item) { return item.id == id; });
    return it == templates_.end() ? nullptr : &*it;
}

bool template_provider_session::stale(uint64_t id) const {
    return id != 0 && id <= stale_through_;
}

template_provider_session::result
template_provider_session::set_template(blockchain::mining_template value) {
    if (state_ != state::ready || ! constraints_) return close();

    auto const size_reserve = coinbase_fixed_reserve
        + uint64_t{constraints_->coinbase_output_max_additional_size};
    auto const target = domain::chain::compact::from_compact(value.bits);
    auto const height = infrastructure::machine::number::from_int(
        static_cast<int64_t>(value.height));
    if (size_reserve > value.size_limit
        || constraints_->coinbase_output_max_additional_sigops > value.sigchecks_limit
        || ! target || ! height) {
        spdlog::warn("[sv2] Cannot provide a template within the client's coinbase constraints");
        return close();
    }

    new_template message;
    message.template_id = next_id_;
    message.future_template = true;
    message.version = value.version;
    message.coinbase_tx_version = 1;
    message.coinbase_prefix = domain::machine::operation(height->data()).to_data();
    message.coinbase_tx_input_sequence = 0xffffffffu;
    message.coinbase_tx_value_remaining = value.coinbase_value;
    message.coinbase_tx_locktime = 0;
    message.merkle_path = coinbase_merkle_path(value.selection.txs);

    // BCH blocks may exceed TDP's frame or transaction-count limits. Do not
    // advertise work whose transaction data cannot be returned intact.
    uint64_t data_size = 8 + 2 + 2;
    for (auto const& tx : value.selection.txs) {
        domain::chain::transaction const& transaction = *tx;
        data_size += 3 + transaction.serialized_size(true);
    }
    if (message.coinbase_prefix.size() > 8 || data_size > u24_max
        || value.selection.txs.size() > 0xffffu) {
        spdlog::warn("[sv2] Template exceeds the Template Distribution message limits");
        return close();
    }

    hash_digest target_bytes{};
    auto big = target->big();
    for (auto& byte : target_bytes) {
        byte = static_cast<uint8_t>(big & 0xffu);
        big >>= 8;
    }
    auto bytes = frame(message);
    auto const activation = frame(set_new_prev_hash{message.template_id,
        value.previous_block_hash, value.current_time, value.bits, target_bytes});
    bytes.insert(bytes.end(), activation.begin(), activation.end());

    set_tip(value.previous_block_hash);
    templates_.push_back({next_id_++, std::move(value), std::move(message.merkle_path)});
    if (templates_.size() > max_templates) templates_.pop_front();
    return {std::move(bytes), false};
}

template_provider_session::result template_provider_session::close() {
    state_ = state::closed;
    return {{}, true};
}

template_provider_session::result template_provider_session::process(
    message_header const& header, byte_span payload) {
    if (state_ == state::closed || header.msg_length > max_payload_size
        || header.msg_length != payload.size()) {
        return close();
    }

    // SetupConnection must be the client's first message (sv2-spec 3.6.1).
    if (state_ == state::awaiting_setup) {
        if (header.extension_type != 0 || header.msg_type != setup_connection::message_type) {
            return close();
        }
        auto const setup = parse<setup_connection>(payload);
        if ( ! setup) {
            return close();
        }

        setup_connection_error error;
        if (setup->protocol != setup_connection::protocol_template_distribution) {
            error.error_code = "unsupported-protocol";
        } else if (setup->min_version > protocol_version || setup->max_version < protocol_version) {
            error.error_code = "protocol-version-mismatch";
        } else if (setup->flags != 0) {
            // No Template Distribution flags are defined (sv2-spec 7.1), so
            // every flag the client requires is one this server lacks.
            error.flags = setup->flags;
            error.error_code = "unsupported-feature-flags";
        }
        if ( ! error.error_code.empty()) {
            state_ = state::closed;
            return {frame(error), true};
        }
        state_ = state::ready;
        return {frame(setup_connection_success{protocol_version, 0}), false};
    }

    // Messages of an extension this server does not implement are discarded
    // (sv2-spec 3.2.1). Template Distribution messages never set channel_msg,
    // so on extension 0 that bit marks a malformed frame.
    if (header.extension() != 0) {
        return {};
    }
    if (header.channel_msg()) {
        return close();
    }

    // The client must send its constraints right after setup (sv2-spec 07):
    // every template sent to it depends on them.
    if ( ! constraints_ && header.msg_type != coinbase_output_constraints::message_type) {
        return close();
    }

    switch (header.msg_type) {
        case coinbase_output_constraints::message_type: {
            auto const constraints = parse<coinbase_output_constraints>(payload);
            if ( ! constraints) {
                return close();
            }
            constraints_ = *constraints;
            return {{}, false, true};
        }
        case request_transaction_data::message_type: {
            auto const request = parse<request_transaction_data>(payload);
            if ( ! request) {
                return close();
            }
            auto const* item = find_template(request->template_id);
            if ( ! item) {
                return {frame(request_transaction_data_error{request->template_id,
                    stale(request->template_id) ? "stale-template-id" : "template-id-not-found"}), false};
            }
            request_transaction_data_success reply;
            reply.template_id = item->id;
            for (auto const& tx : item->value.selection.txs) {
                domain::chain::transaction const& transaction = *tx;
                reply.transaction_list.push_back(to_data_chunk(transaction, true));
            }
            return {frame(reply), false};
        }
        case submit_solution::message_type: {
            auto const solution = parse<submit_solution>(payload);
            if ( ! solution) {
                return close();
            }
            auto const* item = find_template(solution->template_id);
            if ( ! item) {
                spdlog::debug("[sv2] Ignoring a solution for {} template {}",
                    stale(solution->template_id) ? "stale" : "unknown", solution->template_id);
                return {};
            }
            auto const coinbase = parse<domain::chain::transaction>(solution->coinbase_tx);
            if ( ! coinbase) {
                spdlog::debug("[sv2] Ignoring an undecodable coinbase for template {}", item->id);
                return {};
            }
            auto root = coinbase->hash();
            for (auto const& sibling : item->merkle_path) root = merkle_parent(root, sibling);
            domain::chain::header const solved(solution->version,
                item->value.previous_block_hash, root, solution->header_timestamp,
                item->value.bits, solution->header_nonce);
            result out;
            out.solution = std::make_shared<domain::message::block>(
                mining::assemble_block(solved, *coinbase, item->value.selection.txs));
            return out;
        }
        default:
            // A message only a server sends, or one the protocol does not define.
            return close();
    }
}

template_provider::template_provider(sv2_settings const& settings,
    ::asio::any_io_executor executor, blockchain::block_chain& chain,
    domain::config::network network)
    : settings_(settings)
    , network_(network)
    , chain_(chain)
    , owner_(::asio::make_strand(executor))
    , stop_requests_(owner_, 1)
    , listening_(owner_, 1)
{}

::asio::awaitable<void> template_provider::serve_connection(::asio::ip::tcp::socket& socket) {
    using namespace ::asio::experimental::awaitable_operators;

    // Taken before the first await so two connections cannot share a salt.
    auto const connection_salt = next_salt_++;
    std::optional<noise_transport> transport;
    if (credentials_) {
        ::asio::steady_timer deadline(owner_);
        deadline.expires_after(std::chrono::seconds(settings_.handshake_timeout_seconds));
        auto exchange = [&]() -> ::asio::awaitable<bool> {
            byte_array<64> first{};
            auto [read_error, count] = co_await ::asio::async_read(socket,
                ::asio::buffer(first), ::asio::as_tuple(::asio::use_awaitable));
            if (read_error) co_return false;
            auto const now = unix_time();
            auto const certificate = now ? credentials_->certificate_at(*now) : std::nullopt;
            if ( ! certificate) {
                spdlog::error("[sv2] Noise certificate is not valid now; renew it and restart");
                co_return false;
            }
            noise_responder responder(noise_keypair::generate(), credentials_->static_key,
                *certificate);
            auto const second = responder.respond(first);
            if ( ! second) co_return false;
            auto [write_error, written] = co_await ::asio::async_write(socket,
                ::asio::buffer(*second), ::asio::as_tuple(::asio::use_awaitable));
            if (write_error) co_return false;
            transport.emplace(*responder.keys(), false, template_provider_session::max_payload_size);
            co_return true;
        };
        // Join the cancelled operation before its buffers leave this scope.
        // The deadline covers both flights; it is never extended by input.
        auto const outcome = co_await (exchange()
            || deadline.async_wait(::asio::as_tuple(::asio::use_awaitable)));
        if (outcome.index() != 0 || ! std::get<0>(outcome)) co_return;
    }

    template_provider_session session;
    auto tips = chain_.subscribe_blockchain();
    if ( ! tips) co_return;

    enum class event_kind { frame, tip, end };
    struct event {
        event_kind kind = event_kind::end;
        message_header header;
        data_chunk payload;
    };
    // Only dispatch touches session state or writes to the socket. Producers
    // queue complete frames and tip wake-ups, so a tip cannot cancel a partial
    // read or interleave its messages with a transaction-data response.
    ::asio::experimental::channel<void(std::error_code, event)> inbox(owner_, 8);
    // Closing a channel does not wake an operation already waiting on it in
    // the pinned Asio; cancelling first does.
    auto const end_inputs = [&] {
        std::error_code ignored;
        socket.close(ignored);
        inbox.cancel();
        inbox.close();
        tips->cancel();
        tips->close();
    };
    detail::scope_action cleanup([&] {
        end_inputs();
        chain_.unsubscribe_blockchain(tips);
    }, true);

    auto read_frames = [&]() -> ::asio::awaitable<void> {
        std::array<uint8_t, message_header::size> header_bytes{};
        std::array<uint8_t, 8192> encrypted{};
        for (;;) {
            if (transport) {
                auto [read_error, size] = co_await socket.async_read_some(
                    ::asio::buffer(encrypted), ::asio::as_tuple(::asio::use_awaitable));
                if (read_error) break;
                auto frames = transport->feed(byte_span(encrypted).first(size));
                if ( ! frames) break;
                for (auto const& bytes : *frames) {
                    auto const header = from_data_chunk<message_header>(
                        byte_span(bytes).first(message_header::size));
                    data_chunk payload(bytes.begin() + message_header::size, bytes.end());
                    auto [error] = co_await inbox.async_send(std::error_code{},
                        event{event_kind::frame, *header, std::move(payload)},
                        ::asio::as_tuple(::asio::use_awaitable));
                    if (error) co_return;
                }
                continue;
            }
            auto [header_error, header_size] = co_await ::asio::async_read(socket,
                ::asio::buffer(header_bytes), ::asio::as_tuple(::asio::use_awaitable));
            if (header_error) break;
            auto const header = from_data_chunk<message_header>(header_bytes);
            if ( ! header || header->msg_length > template_provider_session::max_payload_size) break;

            data_chunk payload(header->msg_length);
            auto [payload_error, payload_size] = co_await ::asio::async_read(socket,
                ::asio::buffer(payload), ::asio::as_tuple(::asio::use_awaitable));
            if (payload_error) break;
            auto [error] = co_await inbox.async_send(std::error_code{},
                event{event_kind::frame, *header, std::move(payload)},
                ::asio::as_tuple(::asio::use_awaitable));
            if (error) co_return;
        }
        // EOF follows the already queued frames. A client can half-close its
        // send side and still receive the answer to its last request.
        (void)co_await inbox.async_send(std::error_code{}, event{},
            ::asio::as_tuple(::asio::use_awaitable));
    };
    auto receive_tips = [&]() -> ::asio::awaitable<void> {
        for (;;) {
            auto [error, height, incoming, outgoing] = co_await tips->async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (error) {
                inbox.cancel();
                inbox.close();
                co_return;
            }
            auto [send_error] = co_await inbox.async_send(std::error_code{},
                event{event_kind::tip, {}, {}}, ::asio::as_tuple(::asio::use_awaitable));
            if (send_error) co_return;
        }
    };
    std::optional<mining_session> miner;
    bool decided = false;
    std::optional<hash_digest> sent_previous;
    auto next_template = [&]() -> ::asio::awaitable<template_provider_session::result> {
        auto const& constraints = *session.constraints();
        auto value = co_await chain_.fetch_mining_template(
            template_provider_session::coinbase_fixed_reserve
                + uint64_t{constraints.coinbase_output_max_additional_size},
            constraints.coinbase_output_max_additional_sigops);
        if ( ! value) {
            // A node that has a new header but not yet its block, or is in a
            // chain transition, has no template for a moment; the tip wake-up
            // sent once the block connects asks again. The client is kept: a
            // stopping node stops this provider, which ends the connection.
            spdlog::info("[sv2] No template yet ({}); waiting for the next tip",
                value.error().message());
            co_return template_provider_session::result{};
        }
        sent_previous = value->previous_block_hash;
        co_return session.set_template(std::move(*value));
    };
    auto refresh_mining = [&]() -> ::asio::awaitable<template_provider_session::result> {
        // Outputs are outside the fixed coinbase reserve. One payout output is
        // what this connection will actually place in the coinbase.
        domain::chain::output const payout_output(0, *payout_, std::nullopt);
        auto const reserve = template_provider_session::coinbase_fixed_reserve
            + uint64_t{payout_output.serialized_size(true)};
        auto value = co_await chain_.fetch_mining_template(reserve, 0);
        if ( ! value) {
            spdlog::info("[sv2] No template yet ({}); waiting for the next tip",
                value.error().message());
            co_return template_provider_session::result{};
        }
        auto produced = miner->apply(std::move(*value));
        template_provider_session::result out;
        out.response = std::move(produced.response);
        out.close = produced.close;
        out.solution = produced.solution;
        co_return out;
    };
    auto dispatch = [&]() -> ::asio::awaitable<void> {
        // Close both producers even when parsing, allocation or a chain call
        // throws. The group below joins them before any captured locals go.
        detail::scope_action finish(end_inputs, true);
        for (;;) {
            auto [error, input] = co_await inbox.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (error || input.kind == event_kind::end) co_return;
            auto const view = chain_.chain_view();
            if (view) {
                if (miner) miner->note_tip(view->tip_hash);
                else session.set_tip(view->tip_hash);
            }

            template_provider_session::result result;
            if (input.kind == event_kind::tip) {
                if (miner) {
                    if ( ! miner->needs_template()) continue;
                    result = co_await refresh_mining();
                } else {
                    if ( ! session.constraints() || (view && sent_previous == view->tip_hash)) continue;
                    result = co_await next_template();
                }
            } else {
                if ( ! decided && payout_ && mining_setup(input.header, input.payload)) {
                    miner.emplace(connection_salt, *payout_);
                }
                decided = true;
                if (miner) {
                    auto produced = miner->process(input.header, input.payload);
                    result.response = std::move(produced.response);
                    result.close = produced.close;
                    result.solution = produced.solution;
                    if (produced.wants_template) {
                        auto const jobs = co_await refresh_mining();
                        result.response.insert(result.response.end(),
                            jobs.response.begin(), jobs.response.end());
                        result.close = result.close || jobs.close;
                        if (jobs.solution) result.solution = jobs.solution;
                    }
                } else {
                    result = session.process(input.header, input.payload);
                    if (result.constraints_changed) result = co_await next_template();
                }
            }
            if (result.solution) {
                // A miner that stops reading must not delay connecting a block
                // while its share acknowledgement waits for socket space.
                auto const outcome = co_await chain_.organize(result.solution);
                spdlog::info("[sv2] Submitted block {}: {}",
                    encode_hash(result.solution->hash()), outcome ? outcome.message() : "accepted");
            }
            if ( ! result.response.empty()) {
                if (transport) {
                    data_chunk encrypted;
                    byte_span remaining(result.response);
                    while ( ! remaining.empty()) {
                        auto const header = from_data_chunk<message_header>(
                            remaining.first(message_header::size));
                        auto const size = message_header::size + header->msg_length;
                        auto bytes = transport->encrypt_frame(remaining.first(size));
                        if ( ! bytes) co_return;
                        encrypted.insert(encrypted.end(), bytes->begin(), bytes->end());
                        remaining = remaining.subspan(size);
                    }
                    result.response = std::move(encrypted);
                }
                auto [write_error, written] = co_await ::asio::async_write(socket,
                    ::asio::buffer(result.response), ::asio::as_tuple(::asio::use_awaitable));
                if (write_error) co_return;
            }
            if (result.close) co_return;
        }
    };
    co_await (read_frames() && receive_tips() && dispatch());
}

void template_provider::stop() {
    // Startup may have a waiter even if run() will never be entered.
    // This channel is concurrent; socket shutdown still belongs to owner_.
    listening_.close();
    (void)stop_requests_.try_send(std::error_code{}, 1);
}

::asio::awaitable<void> template_provider::run() {
    // Also wake a startup waiter if cancellation prevents run_owned starting.
    detail::scope_action close_startup([this] { listening_.close(); }, true);
    co_await ::asio::co_spawn(owner_, run_owned(), ::asio::use_awaitable);
}

::asio::awaitable<uint16_t> template_provider::wait_until_listening() {
    co_return co_await listening_.async_receive(::asio::use_awaitable);
}

::asio::awaitable<void> template_provider::run_owned() {
    using ::asio::ip::tcp;
    using namespace ::asio::experimental::awaitable_operators;

    std::error_code ec;
    auto const address = ::asio::ip::make_address(settings_.bind, ec);
    if (ec) {
        spdlog::error("[sv2] invalid bind address '{}': {}", settings_.bind, ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    if (settings_.mining.enabled) {
        std::string error;
        auto const payout = mining::payout_from_address(settings_.mining.address, network_, error);
        if ( ! payout) {
            spdlog::error("[sv2] {}", error);
            (void)listening_.try_send(std::make_error_code(std::errc::invalid_argument), uint16_t{});
            co_return;
        }
        payout_ = payout->script;
    }

    if (settings_.noise) {
        credentials_.reset();
        auto const now = unix_time();
        std::string error;
        if (now) credentials_ = load_noise_credentials(settings_, *now, error);
        if ( ! credentials_) {
            spdlog::error("[sv2] Cannot start Noise listener: {}",
                error.empty() ? "clock outside certificate timestamp range" : error);
            (void)listening_.try_send(std::make_error_code(std::errc::invalid_argument), uint16_t{});
            co_return;
        }
        spdlog::info("[sv2] Noise authority public key: {}",
            noise_authority_key(credentials_->authority_public_key));
    }

    tcp::endpoint const endpoint(address, settings_.port);
    tcp::acceptor acceptor(owner_);
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("[sv2] failed to open acceptor: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("[sv2] failed to bind {}:{}: {}", settings_.bind, settings_.port,
            ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.listen(tcp::socket::max_listen_connections, ec);
    if (ec) {
        spdlog::error("[sv2] failed to listen: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    auto const bound = acceptor.local_endpoint(ec);
    if (ec) {
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    (void)listening_.try_send(std::error_code{}, bound.port());
    spdlog::info("[sv2] {} listening on {}:{} ({})",
        settings_.mining.enabled ? "Template Provider and Mining Protocol" : "Template Provider",
        settings_.bind, bound.port(), settings_.noise ? "Noise" : "plaintext");

    // All mutable listener/connection state has one owner. At most 64 children
    // are admitted, and every completion is consumed before this scope exits.
    constexpr std::size_t capacity = 64;
    ::asio::experimental::channel<void(std::error_code, std::exception_ptr)> ended(owner_, capacity);
    std::unordered_set<std::shared_ptr<tcp::socket>> connections;
    std::size_t pending = 0;
    bool stopping = false;
    ::asio::steady_timer accept_retry(owner_);
    std::exception_ptr failure;
    auto close_owned = [&] {
        stopping = true;
        std::error_code ignored;
        acceptor.close(ignored);
        try {
            accept_retry.cancel();
        } catch (...) {
            // A timer-service error must not bypass closing or joining children.
            if ( ! failure) failure = std::current_exception();
        }
        for (auto const& socket : connections) {
            socket->close(ignored);
        }
    };
    auto reap = [&]() -> ::asio::awaitable<void> {
        auto error = co_await ended.async_receive(::asio::use_awaitable);
        --pending;
        if (error && ! failure) failure = error;
    };
    auto accept_connections = [&]() -> ::asio::awaitable<void> {
        try {
            while ( ! stopping) {
                if (pending == capacity) {
                    co_await reap();
                    continue;
                }
                tcp::socket socket(owner_);
                auto [aec] = co_await acceptor.async_accept(socket,
                    ::asio::as_tuple(::asio::use_awaitable));
                if (aec) {
                    if (stopping || aec == ::asio::error::operation_aborted) break;
                    spdlog::warn("[sv2] accept failed: {}; retrying", aec.message());
                    // A resource shortage is not the end of the service. This
                    // owner-local deadline avoids a busy loop and is cancelled
                    // by the same stop event that closes the listener.
                    accept_retry.expires_after(std::chrono::milliseconds(100));
                    (void)co_await accept_retry.async_wait(::asio::as_tuple(::asio::use_awaitable));
                    continue;
                }
                if (stopping) {
                    std::error_code ignored;
                    socket.close(ignored);
                    break;
                }
                auto child = std::make_shared<tcp::socket>(std::move(socket));
                connections.insert(child);
                ++pending;
                // co_spawn's completion adapter only publishes an owned result.
                // It runs on the same strand; its captures cannot outlive the
                // drain below, even for partial frames or handler errors.
                try {
                    ::asio::co_spawn(owner_, serve_connection(*child),
                        [&, child](std::exception_ptr error) {
                            std::error_code ignored;
                            child->close(ignored);
                            connections.erase(child);
                            (void)ended.try_send(std::error_code{}, error);
                        });
                } catch (...) {
                    --pending;
                    connections.erase(child);
                    throw;
                }
            }
        } catch (...) {
            failure = std::current_exception();
        }
        close_owned();
        stop();
        co_await ::asio::this_coro::reset_cancellation_state(::asio::disable_cancellation());
        while (pending != 0) co_await reap();
    };
    auto receive_stop = [&]() -> ::asio::awaitable<void> {
        try {
            (void)co_await stop_requests_.async_receive(::asio::use_awaitable);
        } catch (...) {
            close_owned();
            throw;
        }
        spdlog::info("[sv2] Stop received; closing listener and {} connections", connections.size());
        close_owned();
    };
    co_await (accept_connections() && receive_stop());
    spdlog::info("[sv2] Listener stopped; all connections joined");
    if (failure) std::rethrow_exception(failure);
}

} // namespace kth::node::sv2
