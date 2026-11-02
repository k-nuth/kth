// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#if defined(KTH_BROWSER_NODE)
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/full_node.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <cassert>
#include <stdexcept>

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

std::shared_ptr<browser::runtime> worker_runtime() {
    // One cooperative runtime per Worker; header clients may share it. No cross-thread access.
    static std::weak_ptr<browser::runtime> shared;
    if (auto existing = shared.lock()) return existing;
    auto created = std::make_shared<browser::runtime>();
    shared = created;
    return created;
}

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

::asio::awaitable<raw_message> receive(peer_session& peer) {
    using namespace ::asio::experimental::awaitable_operators;
    auto executor = co_await ::asio::this_coro::executor;
    kth::steady_timer timeout(executor, std::chrono::seconds(30));
    auto result = co_await (peer.read_message_direct() || timeout.async_wait(::asio::as_tuple(::asio::use_awaitable)));
    if (result.index() != 0) throw std::runtime_error("header response timeout");
    auto message = std::move(std::get<0>(result));
    if (!message) throw std::runtime_error(message.error().message());
    co_return std::move(*message);
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
    bool active = false;
    bool stop_requested = false;
    std::string message = "idle";

    impl(std::string const& url, domain::config::network net, size_t capacity)
        : bridge(url), network(net), index(checked_capacity(capacity)), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        if (bridge.empty()) throw std::invalid_argument("WebSocket bridge URL is required");
        auto genesis = full_node::get_genesis_block(network);
        if (!index.add(genesis.hash(), genesis.header()).inserted) throw std::runtime_error("genesis initialization failed");
        organizer.sync_tip();
    }
};

header_sync::header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity)
    : state_(std::make_unique<impl>(bridge_url, network, capacity)) {}

header_sync::~header_sync() { assert(!state_->active); }

::asio::any_io_executor header_sync::executor() const {
    state_->runtime->wake();
    return state_->runtime->context().get_executor();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::run() {
    auto& state = *state_;
    if (state.active || state.stop_requested) co_return std::unexpected(error::service_stopped);
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
        try {
            auto handshake = co_await perform_handshake_direct(*peer, make_handshake_config(transport, 0, 0x48454144455253ULL));
            if (!handshake) throw std::runtime_error(handshake.error().message());
            state.message = "connected";
            state.message = "synchronizing";
            for (;;) {
                if (state.stop_requested) throw std::runtime_error("header sync stopped");
                auto sent = co_await peer->send_direct(message::get_headers(locator(index), null_hash));
                if (sent) throw std::runtime_error(sent.message());
                bool received = false;
                for (unsigned ignored = 0; ignored < 64; ++ignored) {
                    auto raw = co_await receive(*peer);
                    byte_reader reader(raw.payload);
                    if (raw.heading.command() == message::ping::command) {
                        auto ping = message::ping::from_data(reader, peer->negotiated_version());
                        if (!ping) throw std::runtime_error("invalid ping");
                        auto ec = co_await peer->send_direct(message::pong(ping->nonce()));
                        if (ec) throw std::runtime_error(ec.message());
                        continue;
                    }
                    if (raw.heading.command() != message::headers::command) continue;
                    auto headers = message::headers::from_data(reader, peer->negotiated_version());
                    if (!headers || !headers->is_sequential()) throw std::runtime_error("invalid headers response");
                    if (headers->elements().empty()) {
                        state.message = "caught_up";
                        peer->stop();
                        organizer.stop();
                        co_return;
                    }
                    if (headers->elements().size() > capacity - index.size()) throw std::runtime_error("header capacity exceeded");
                    auto before = index.size();
                    auto accepted = organizer.add_headers(headers->elements());
                    if (accepted.error) throw std::runtime_error(accepted.error.message());
                    if (accepted.reorg_candidate) organizer.adopt_tip(accepted.reorg_branch_head);
                    if (index.size() == before) throw std::runtime_error("peer made no header progress");

                    received = true;
                    break;
                }
                if (!received) throw std::runtime_error("unsolicited message limit exceeded");
                // Yield to the Worker between batches, including immediately available replies.
                co_await ::asio::post(executor, ::asio::use_awaitable);
            }
        } catch (...) {
            peer->stop();
            organizer.stop();
            throw;
        }
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

void header_sync::reset() {
    if (state_->active) throw std::logic_error("run must settle before reset");
    state_->stop_requested = false;
    state_->message = "idle";
}

uint64_t header_sync::last_height() const { return uint64_t(state_->organizer.header_height()); }
bool header_sync::running() const { return state_->active; }
bool header_sync::idle() const { return !state_->active && state_->runtime->idle(); }
std::string header_sync::status() const { return state_->message; }
size_t header_sync::index_memory_bytes() const { return state_->index.memory_usage(); }
} // namespace kth::node
#endif
