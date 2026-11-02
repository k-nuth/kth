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
#include <kth/node/full_node.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <array>
#include <cassert>
#include <stdexcept>
#include <optional>
#include <filesystem>
#include <fstream>
#include <cstdio>

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

using refresh_channel = kth::select_channel<unsigned>;
using message_channel = kth::select_channel<raw_message>;

::asio::awaitable<std::optional<raw_message>> receive_timed(message_channel& incoming,
        std::chrono::seconds deadline, refresh_channel* refresh = nullptr) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    auto expires = std::chrono::steady_clock::now() + deadline;
    if (refresh) {
        auto completion = co_await make_select(receive(incoming), receive(*refresh), after(expires))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) throw std::runtime_error(ec.message());
        if (selected.index != 0) co_return std::nullopt;
        co_return std::move(*selected.value<0>());
    }
    auto completion = co_await make_select(receive(incoming), after(expires))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) throw std::runtime_error(ec.message());
    if (selected.index != 0) co_return std::nullopt;
    co_return std::move(*selected.value<0>());
}

::asio::awaitable<raw_message> receive(message_channel& incoming) {
    auto raw = co_await receive_timed(incoming, std::chrono::seconds(30));
    if (!raw) throw std::runtime_error("header response timeout");
    co_return std::move(*raw);
}

// One bounded handoff bridges the existing peer I/O queue to lossless channel selection.
// A losing receive leaves its message queued; a full handoff retains the pending payload.
::asio::awaitable<void> forward_messages(peer_session& peer, message_channel& incoming) {
    struct close_guard {
        message_channel& channel;
        ~close_guard() { channel.close(); }
    } guard{incoming};
    auto executor = co_await ::asio::this_coro::executor;
    for (;;) {
        auto [ec, raw] = co_await peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) co_return;
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::send(incoming, std::move(raw)))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        if (std::get<0>(completion)) co_return;
    }
}

// A deadline refreshes headers on the existing session when the peer does not announce its tip.
// It never opens another connection. Reads and ping responses remain owned by this coroutine.
::asio::awaitable<void> wait_for_tip(peer_session& peer, message_channel& incoming, refresh_channel& refresh_request) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const refresh = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    for (;;) {
        auto const now = std::chrono::steady_clock::now();
        if (now >= refresh) co_return;
        auto raw = co_await receive_timed(incoming, std::chrono::ceil<std::chrono::seconds>(refresh - now), &refresh_request);
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
    bool stop_requested = false;
    std::string message = "idle";
    // Opt-in: the embedding Worker mounts this network-specific directory on IDBFS.
    // The cache is raw active-chain headers, never a trusted memory/index image.
    std::string cache_path;
    int32_t cache_height = -1;
    hash_digest cache_tip{};

    void restore_cache(size_t capacity) {
        auto directory = std::string("/kth-header-cache/") + std::string(domain::config::name(network));
        if (!std::filesystem::is_directory(directory)) return;
        cache_path = directory + "/chain-v1.bin";
        std::ifstream input(cache_path, std::ios::binary | std::ios::ate);
        if (!input) return;
        auto length = static_cast<std::streamoff>(input.tellg());
        if (length < 80 || length % 80 != 0 || uint64_t(length) / 80 > capacity)
            throw std::runtime_error("invalid stored header cache size");
        auto count = uint64_t(length) / 80;
        input.seekg(0);
        std::array<uint8_t, 80> bytes{};
        auto read_header = [&]() {
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
                throw std::runtime_error("truncated stored header cache");
            byte_reader reader(bytes);
            auto header = domain::chain::header::from_data(reader);
            if (!header) throw std::runtime_error("invalid stored header encoding");
            return *header;
        };
        if (domain::chain::hash(read_header()) != index.get_hash(index.active_at(0)))
            throw std::runtime_error("stored header cache has wrong genesis");
        organizer.start();
        uint64_t height = 1;
        while (height < count) {
            domain::message::header::list batch;
            auto end = std::min(count, height + 2000);
            for (; height < end; ++height) batch.emplace_back(read_header());
            auto result = organizer.add_headers(batch);
            if (result.error || result.headers_added != batch.size())
                throw std::runtime_error("stored header cache failed consensus validation");
            if (result.reorg_candidate) organizer.adopt_tip(result.reorg_branch_head);
        }
        organizer.stop();
        if (uint64_t(organizer.header_height()) + 1 != count)
            throw std::runtime_error("stored header cache is not an active chain");
        cache_height = organizer.header_height();
        cache_tip = index.get_hash(index.active_at(cache_height));
    }

    void persist_cache() noexcept {
        if (cache_path.empty()) return;
        try {
            auto height = organizer.header_height();
            auto append = cache_height >= 0 && cache_height <= height &&
                index.get_hash(index.active_at(cache_height)) == cache_tip;
            if (append && cache_height == height) return;
            std::ofstream output(cache_path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!output) throw std::runtime_error("cannot write header cache");
            for (int32_t at = append ? cache_height + 1 : 0; at <= height; ++at) {
                std::array<uint8_t, 80> bytes{};
                byte_writer writer(bytes);
                auto header = index.get_header(index.active_at(at));
                if (!header.to_data(writer, true)) throw std::runtime_error("cannot encode header cache");
                output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            }
            output.flush();
            if (!output) throw std::runtime_error("header cache write failed");
            cache_height = height;
            cache_tip = index.get_hash(index.active_at(height));
        } catch (std::exception const& e) {
            std::fprintf(stderr, "Header cache disabled: %s\n", e.what());
            std::remove(cache_path.c_str());
            cache_path.clear();
        }
    }

    impl(std::string const& url, domain::config::network net, size_t capacity)
        : bridge(url), network(net), index(checked_capacity(capacity)), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        if (bridge.empty()) throw std::invalid_argument("WebSocket bridge URL is required");
        auto genesis = full_node::get_genesis_block(network);
        if (!index.add(genesis.hash(), genesis.header()).inserted) throw std::runtime_error("genesis initialization failed");
        organizer.sync_tip();
        restore_cache(capacity);
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
        message_channel incoming{1};
        kth::task_group tasks("header session", executor);
        std::exception_ptr failure;
        try {
            auto handshake = co_await perform_handshake_direct(*peer, make_handshake_config(transport, 0, 0x48454144455253ULL));
            if (!handshake) throw std::runtime_error(handshake.error().message());
            tasks.spawn("peer I/O", [peer]() -> ::asio::awaitable<void> { co_await peer->run(); });
            tasks.spawn("inbound handoff", [peer, &incoming]() -> ::asio::awaitable<void> {
                co_await forward_messages(*peer, incoming);
            });
            state.message = "connected";
            state.message = "synchronizing";
            bool complete = false;
            while (!complete) {
                if (state.stop_requested) throw std::runtime_error("header sync stopped");
                auto sent = co_await peer->send(message::get_headers(locator(index), null_hash));
                if (sent) throw std::runtime_error(sent.message());
                bool received = false;
                for (unsigned ignored = 0; ignored < 64; ++ignored) {
                    auto raw = co_await receive(incoming);
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
                        co_await wait_for_tip(*peer, incoming, state.refresh_request);
                        state.message = "synchronizing";
                        received = true;
                        break;
                    }
                    if (headers->elements().size() > capacity - index.size()) throw std::runtime_error("header capacity exceeded");
                    auto before = index.size();
                    auto accepted = organizer.add_headers(headers->elements());
                    if (accepted.error) throw std::runtime_error(accepted.error.message());
                    if (accepted.reorg_candidate) organizer.adopt_tip(accepted.reorg_branch_head);
                    if (index.size() == before) throw std::runtime_error("peer made no header progress");
                    state.persist_cache();

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
        incoming.close();
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

void header_sync::reset() {
    if (state_->active) throw std::logic_error("run must settle before reset");
    state_->stop_requested = false;
    state_->message = "idle";
}

uint64_t header_sync::last_height() const { return uint64_t(state_->organizer.header_height()); }
std::string header_sync::header_hex(uint64_t height) const {
    if (height > last_height()) return {};
    auto const header = state_->index.get_header(state_->index.active_at(static_cast<int32_t>(height)));
    std::array<uint8_t, domain::chain::header::satoshi_fixed_size()> bytes{};
    byte_writer writer(bytes);
    if (!header.to_data(writer, true)) return {};
    return encode_base16(bytes);
}

std::string header_sync::header_hash(uint64_t height) const {
    if (height > last_height()) return {};
    return encode_hash(state_->index.get_hash(state_->index.active_at(static_cast<int32_t>(height))));
}

bool header_sync::running() const { return state_->active; }
bool header_sync::idle() const { return !state_->active && state_->runtime->idle(); }
std::string header_sync::status() const { return state_->message; }
size_t header_sync::index_memory_bytes() const { return state_->index.memory_usage(); }
} // namespace kth::node
#endif
