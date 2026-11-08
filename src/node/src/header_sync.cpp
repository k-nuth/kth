// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#if defined(KTH_BROWSER_NODE)
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <array>
#include <cassert>
#include <stdexcept>
#include <optional>

namespace kth::node {
namespace {
using namespace kth::domain;
using namespace kth::network;

size_t checked_capacity(size_t capacity) {
    if (capacity == 0 || capacity > 2'000'000) throw std::invalid_argument("header capacity outside 1..2000000");
    return capacity;
}

blockchain::settings validation_settings(domain::config::network network) {
    blockchain::settings result(network);
    result.checkpoints.clear();
    result.checkpoints_sorted.clear();
    result.max_checkpoint_height = 0;
    return result;
}

// The Worker's one runtime. The application's, when it runs one (not owned
// here); otherwise one shared by this Worker's header clients, created by the
// first and kept while any of them lives.
std::shared_ptr<browser::runtime> worker_runtime() {
    static std::weak_ptr<browser::runtime> shared;
    if (auto existing = shared.lock()) return existing;
    if (auto* application = browser::runtime::find()) {
        return std::shared_ptr<browser::runtime>(application, [](browser::runtime*) {});
    }
    auto created = std::make_shared<browser::runtime>();
    shared = created;
    return created;
}

domain::chain::block genesis_block(domain::config::network network) {
    switch (network) {
        case domain::config::network::testnet: return domain::chain::block::genesis_testnet();
        case domain::config::network::regtest: return domain::chain::block::genesis_regtest();
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4: return domain::chain::block::genesis_testnet4();
        case domain::config::network::scalenet: return domain::chain::block::genesis_scalenet();
        case domain::config::network::tempnet:
        case domain::config::network::chipnet: return domain::chain::block::genesis_chipnet();
#endif
        default:
        case domain::config::network::mainnet: return domain::chain::block::genesis_mainnet();
    }
}

// The largest message a header client accepts: a full `headers` (2000 headers
// of 81 bytes and its count), with room for the handshake and control
// messages. An untrusted bridge cannot make it allocate more per message.
constexpr size_t maximum_payload = 256 * 1024;

// The session stays open while it follows the tip: a year, within what a
// timer's deadline can hold.
constexpr uint32_t session_expiration_minutes = 365 * 24 * 60;

hash_list locator(blockchain::header_index const& index) {
    hash_list result;
    auto height = index.active_tip_height();
    int32_t step = 1;
    for (;;) {
        result.push_back(index.get_hash(index.active_at(height)));
        if (height == 0) return result;
        if (result.size() > 10) step *= 2;
        height = std::max<int32_t>(0, height - step);
    }
}

using refresh_channel = kth::select_channel<unsigned>;

// The next message from the peer, until the deadline or a refresh request. The
// peer session's inbound queue is itself a selectable channel: a losing receive
// leaves its message queued. nullopt for the deadline or a refresh; a closed
// session is an error.
::asio::awaitable<std::optional<raw_message>> receive_timed(peer_session& peer,
        std::chrono::seconds deadline, refresh_channel* refresh = nullptr) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    auto expires = std::chrono::steady_clock::now() + deadline;
    if (refresh) {
        auto completion = co_await make_select(receive(peer.messages()), receive(*refresh), after(expires))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) throw std::runtime_error("peer session closed");
        if (selected.index != 0) co_return std::nullopt;
        co_return std::move(*selected.value<0>());
    }
    auto completion = co_await make_select(receive(peer.messages()), after(expires))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) throw std::runtime_error("peer session closed");
    if (selected.index != 0) co_return std::nullopt;
    co_return std::move(*selected.value<0>());
}

::asio::awaitable<raw_message> receive(peer_session& peer) {
    auto raw = co_await receive_timed(peer, std::chrono::seconds(30));
    if (!raw) throw std::runtime_error("header response timeout");
    co_return std::move(*raw);
}

// A deadline refreshes headers on the existing session when the peer does not announce its tip.
// It never opens another connection. Reads and ping responses remain owned by this coroutine.
::asio::awaitable<void> wait_for_tip(peer_session& peer, refresh_channel& refresh_request) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const refresh = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    for (;;) {
        auto const now = std::chrono::steady_clock::now();
        if (now >= refresh) co_return;
        auto raw = co_await receive_timed(peer, std::chrono::ceil<std::chrono::seconds>(refresh - now), &refresh_request);
        if (!raw) co_return;
        byte_reader reader(raw->payload);
        auto const command = raw->heading.command();
        if (command == message::ping::command) {
            auto ping = message::ping::from_data(reader, peer.negotiated_version());
            if (!ping) throw std::runtime_error("invalid ping");
            auto ec = co_await peer.send(message::pong(ping->nonce()));
            if (ec) throw std::runtime_error(ec.message());
        } else if (command == message::inventory::command) {
            auto inventory = message::inventory::from_data(reader, peer.negotiated_version());
            if (!inventory) throw std::runtime_error("invalid inventory");
            if (inventory->count(message::inventory::type_id::block) != 0) co_return;
        } else if (command == message::headers::command) {
            auto headers = message::headers::from_data(reader, peer.negotiated_version());
            if (!headers || !headers->is_sequential()) throw std::runtime_error("invalid headers announcement");
            // Fetch from our locator so unsolicited announcements cannot bypass validation.
            if (!headers->elements().empty()) co_return;
        }
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }
}

} // namespace

struct header_sync::impl {
    std::shared_ptr<browser::runtime> runtime = worker_runtime();
    std::string bridge;
    domain::config::network network;
    blockchain::header_index index;
    blockchain::settings consensus;
    blockchain::header_organizer organizer;
    std::shared_ptr<network::peer_session> peer;
    ::asio::cancellation_signal cancel;
    refresh_channel refresh_request{1};
    bool active = false;
    size_t admitted = 0;
    bool stop_requested = false;
    std::string message = "idle";

    impl(std::string const& url, domain::config::network net, size_t capacity)
        : bridge(url), network(net), index(checked_capacity(capacity)), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        if (bridge.empty()) throw std::invalid_argument("WebSocket bridge URL is required");
        auto genesis = genesis_block(network);
        if (!index.add(genesis.hash(), genesis.header()).inserted) throw std::runtime_error("genesis initialization failed");
        organizer.sync_tip();
    }
};

header_sync::header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity)
    : state_(std::make_unique<impl>(bridge_url, network, capacity)) {}

header_sync::~header_sync() { assert(settled()); }

::asio::any_io_executor header_sync::executor() const {
    state_->runtime->wake();
    return state_->runtime->context().get_executor();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::run() {
    co_return co_await run_session(false);
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::follow() {
    co_return co_await run_session(true);
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::run_session(bool follow_tip) {
    auto& state = *state_;
    if (state.active || state.stop_requested) co_return std::unexpected(error::service_stopped);
    std::error_code drained;
    while (state.refresh_request.try_receive(drained)) {}
    state.active = true;
    struct active_guard {
        bool& flag;
        ~active_guard() { flag = false; }
    } guard{state.active};
    state.message = "connecting";
    auto& index = state.index;
    auto& organizer = state.organizer;
    auto const capacity = index.capacity();
    network::settings transport(state.network);
    transport.browser_bridge_url = state.bridge;
    transport.validate_checksum = true;
    transport.user_agent = "/KTH-browser-headers/";
    transport.channel_handshake_seconds = 10;
    transport.browser_maximum_payload = maximum_payload;
    transport.channel_expiration_minutes = session_expiration_minutes;
    organizer.start();
    try {
        auto operation = [&]() -> ::asio::awaitable<void> {
        auto executor = co_await ::asio::this_coro::executor;
        auto connected = co_await ::asio::co_spawn(executor,
            async_connect(executor, "bridge-peer", 8333, transport, std::chrono::seconds(10)),
            ::asio::bind_cancellation_slot(state.cancel.slot(), ::asio::use_awaitable));
        if (!connected) throw std::runtime_error(connected.error().message());
        state.peer = *connected;
        auto peer = state.peer;
        if (state.stop_requested) throw std::runtime_error("header sync stopped");
        kth::task_group tasks("header session", executor);
        std::exception_ptr failure;
        try {
            auto handshake = co_await perform_handshake_direct(*peer, make_handshake_config(transport, 0, 0x48454144455253ULL));
            if (!handshake) throw std::runtime_error(handshake.error().message());
            tasks.spawn("peer I/O", [peer]() -> ::asio::awaitable<void> { co_await peer->run(); });
            state.message = "connected";
            state.message = "synchronizing";
            bool complete = false;
            while (!complete) {
                if (state.stop_requested) throw std::runtime_error("header sync stopped");
                auto sent = co_await peer->send(message::get_headers(locator(index), null_hash));
                if (sent) throw std::runtime_error(sent.message());
                bool received = false;
                for (unsigned ignored = 0; ignored < 64; ++ignored) {
                    auto raw = co_await receive(*peer);
                    byte_reader reader(raw.payload);
                    if (raw.heading.command() == message::ping::command) {
                        auto ping = message::ping::from_data(reader, peer->negotiated_version());
                        if (!ping) throw std::runtime_error("invalid ping");
                        auto ec = co_await peer->send(message::pong(ping->nonce()));
                        if (ec) throw std::runtime_error(ec.message());
                        continue;
                    }
                    if (raw.heading.command() != message::headers::command) continue;
                    auto headers = message::headers::from_data(reader, peer->negotiated_version());
                    if (!headers || !headers->is_sequential()) throw std::runtime_error("invalid headers response");
                    if (headers->elements().empty()) {
                        state.message = "caught_up";
                        if (!follow_tip) {
                            complete = true;
                            received = true;
                            break;
                        }
                        co_await wait_for_tip(*peer, state.refresh_request);
                        state.message = "synchronizing";
                        received = true;
                        break;
                    }
                    if (headers->elements().size() > capacity - index.size()) throw std::runtime_error("header capacity exceeded");
                    auto before = index.size();
                    auto accepted = organizer.add_headers(headers->elements());
                    // A heavier branch is reported as stale_chain with a reorg
                    // candidate: the client follows it. Otherwise stale_chain is
                    // a batch that did not advance the tip (a lighter branch,
                    // headers already known), judged below by its progress.
                    // Adopting a branch is progress even when its headers were
                    // already stored (a retry after a batch that failed past them).
                    if (accepted.reorg_candidate) {
                        organizer.adopt_tip(accepted.reorg_branch_head);
                    } else if (accepted.error && accepted.error != error::stale_chain) {
                        throw std::runtime_error(accepted.error.message());
                    } else if (index.size() == before) {
                        throw std::runtime_error("peer made no header progress");
                    }

                    received = true;
                    break;
                }
                if (!received) throw std::runtime_error("unsolicited message limit exceeded");
                // Yield to the Worker between batches, including immediately available replies.
                co_await ::asio::post(executor, ::asio::use_awaitable);
            }
        } catch (...) {
            failure = std::current_exception();
        }
        peer->stop();
        organizer.stop();
        co_await tasks.join();
        if (failure) std::rethrow_exception(failure);
        };
        co_await operation();
    } catch (std::exception const& exception) {
        if (state.peer) state.peer->stop();
        state.peer.reset();
        organizer.stop();
        state.active = false;
        if (state.stop_requested) {
            state.message = "stopped";
            co_return last_height();
        }
        state.message = exception.what();
        co_return std::unexpected(error::operation_failed);
    }
    state.peer.reset();
    state.active = false;
    co_return last_height();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_stop() const {
    state_->stop_requested = true;
    state_->cancel.emit(::asio::cancellation_type::all);
    if (state_->peer) state_->peer->stop();
    co_return last_height();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_refresh() const {
    // A full doorbell already represents this work; one locator refresh satisfies all requests.
    if (state_->active && !state_->stop_requested) state_->refresh_request.try_send(0u);
    co_return last_height();
}

header_sync::admission header_sync::admit() const { return admission(state_->admitted); }
bool header_sync::settled() const { return !state_->active && state_->admitted == 0; }

std::error_code header_sync::reset() {
    if (!settled()) return error::operation_failed;
    state_->stop_requested = false;
    state_->message = "idle";
    return {};
}

uint64_t header_sync::last_height() const { return uint64_t(state_->organizer.header_height()); }
std::string header_sync::header_hex(uint64_t height) const {
    if (height > last_height()) return {};
    auto const header = state_->index.get_header(state_->index.active_at(int32_t(height)));
    std::array<uint8_t, domain::chain::header::satoshi_fixed_size()> bytes{};
    byte_writer writer(bytes);
    if (!header.to_data(writer, true)) return {};
    return encode_base16(bytes);
}

std::string header_sync::header_hash(uint64_t height) const {
    if (height > last_height()) return {};
    return encode_hash(state_->index.get_hash(state_->index.active_at(int32_t(height))));
}

bool header_sync::running() const { return state_->active; }
bool header_sync::idle() const { return !state_->active && state_->runtime->idle(); }
std::string header_sync::status() const { return state_->message; }
size_t header_sync::index_memory_bytes() const { return state_->index.memory_usage(); }
} // namespace kth::node
#endif
