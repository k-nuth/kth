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
#include <kth/network/ws_discovery.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <spdlog/spdlog.h>
#include <asio/cancellation_signal.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <cassert>
#include <stdexcept>
#include <optional>
#include <algorithm>
#include <functional>

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

// A peer that sent invalid data: it is not retried.
struct peer_fault : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The index is full: no peer can go on.
// Handles a message that is not part of the header exchange (discovery);
// true when it was one.
using message_handler = std::function<::asio::awaitable<bool>(raw_message const&)>;

constexpr size_t maximum_peers = 8;
constexpr auto connect_timeout = std::chrono::seconds(10);
constexpr auto response_timeout = std::chrono::seconds(30);
constexpr auto tip_refresh_interval = std::chrono::seconds(120);
constexpr unsigned retry_initial_seconds = 5;
constexpr unsigned retry_maximum_seconds = 60;

bool valid_peer_url(std::string const& url) {
    if (url.size() > 2048) return false;
    if (!url.starts_with("ws://") && !url.starts_with("wss://")) return false;
    auto const host = url.substr(url.find("//") + 2);
    return !host.empty() && std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= ' ' || c == 0x7f; });
}

data_chunk discovery_packet(uint32_t magic, std::string_view command, data_chunk payload) {
    domain::message::heading heading(magic, std::string(command), uint32_t(payload.size()), bitcoin_checksum(payload));
    auto bytes = heading.to_data();
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

// The next message from the peer, until the deadline or a refresh request. The
// peer session's inbound queue is itself a selectable channel: a losing receive
// leaves its message queued. nullopt for the deadline or a refresh; a closed
// session is an error.
::asio::awaitable<std::optional<raw_message>> receive_until(peer_session& peer,
        std::chrono::steady_clock::time_point deadline, refresh_channel* refresh = nullptr) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    if (refresh) {
        auto completion = co_await make_select(receive(peer.messages()), receive(*refresh), after(deadline))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) throw std::runtime_error("peer session closed");
        if (selected.index != 0) co_return std::nullopt;
        co_return std::move(*selected.value<0>());
    }
    auto completion = co_await make_select(receive(peer.messages()), after(deadline))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) throw std::runtime_error("peer session closed");
    if (selected.index != 0) co_return std::nullopt;
    co_return std::move(*selected.value<0>());
}

// As receive_until without a refresh, but a closed session ends the wait
// like the deadline does.
::asio::awaitable<std::optional<raw_message>> receive_or_close(peer_session& peer,
        std::chrono::steady_clock::time_point deadline) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    auto completion = co_await make_select(receive(peer.messages()), after(deadline))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec || selected.index != 0) co_return std::nullopt;
    co_return std::move(*selected.value<0>());
}

::asio::awaitable<void> answer_ping(peer_session& peer, byte_reader& reader) {
    auto ping = message::ping::from_data(reader, peer.negotiated_version());
    if (!ping) throw peer_fault("invalid ping");
    auto ec = co_await peer.send(message::pong(ping->nonce()));
    if (ec) throw std::runtime_error(ec.message());
}

// At the tip: until the peer announces a block, a refresh is requested, or a
// periodic refresh is due. It never opens another connection. Pings and
// discovery are answered meanwhile.
::asio::awaitable<void> wait_for_tip(peer_session& peer, refresh_channel& refresh_request,
        message_handler const& other, uint64_t& announcements) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const refresh = std::chrono::steady_clock::now() + tip_refresh_interval;
    for (;;) {
        auto raw = co_await receive_until(peer, refresh, &refresh_request);
        if (!raw) co_return;
        if (co_await other(*raw)) continue;
        byte_reader reader(raw->payload);
        auto const command = raw->heading.command();
        if (command == message::ping::command) {
            co_await answer_ping(peer, reader);
        } else if (command == message::inventory::command) {
            auto inventory = message::inventory::from_data(reader, peer.negotiated_version());
            if (!inventory) throw peer_fault("invalid inventory");
            if (inventory->count(message::inventory::type_id::block) != 0) {
                ++announcements;
                co_return;
            }
        } else if (command == message::headers::command) {
            auto headers = message::headers::from_data(reader, peer.negotiated_version());
            if (!headers || !headers->is_sequential()) throw peer_fault("invalid headers announcement");
            // Fetched from our locator, so an announcement cannot bypass validation.
            if (!headers->elements().empty()) {
                ++announcements;
                co_return;
            }
        }
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }
}

constexpr size_t header_size = 80;
constexpr std::string_view cache_root = "/kth-header-cache/";
constexpr std::string_view cache_file = "chain-v1.bin";

} // namespace

struct header_sync::impl {
    std::shared_ptr<browser::runtime> runtime = worker_runtime();
    domain::config::network network;
    blockchain::header_index index;
    blockchain::settings consensus;
    blockchain::header_organizer organizer;

    // One per peer; each runs on its own session, all on this Worker's
    // executor, sharing the index (validation does not suspend).
    struct remote {
        remote(std::string endpoint, std::string from) : url(std::move(endpoint)), source(std::move(from)) {}
        std::string url;
        std::string source;
        std::string status = "idle";
        std::string agent;
        std::string error;
        uint64_t height = 0;
        uint64_t announcements = 0;
        bool connected = false;
        bool complete = false;
        bool quarantined = false;
        std::shared_ptr<network::peer_session> session;
        ::asio::cancellation_signal cancel;
        refresh_channel refresh{1};
        std::unique_ptr<kth::steady_timer> retry;
    };
    std::vector<std::unique_ptr<remote>> peers;
    kth::task_group* sessions = nullptr;
    bool discovery_enabled = true;
    bool discovery_allow_private = false;
    bool discovery_wss_only = false;
    uint64_t discovery_sequence = 1;
    std::string fatal_error;

    void update_status() {
        if (stop_requested) { message = "stopped"; return; }
        if (!fatal_error.empty()) { message = fatal_error; return; }
        for (auto const& peer : peers) if (peer->status == "synchronizing") { message = "synchronizing"; return; }
        for (auto const& peer : peers) if (peer->connected && peer->status == "caught_up") { message = "caught_up"; return; }
        message = "connecting";
    }

    void stop_peers() {
        for (auto& peer : peers) {
            peer->cancel.emit(::asio::cancellation_type::all);
            if (peer->session) peer->session->stop();
            if (peer->retry) peer->retry->cancel();
        }
    }

    network::settings transport() const {
        network::settings result(network);
        result.validate_checksum = true;
        result.user_agent = "/KTH-browser-headers/";
        if (discovery_enabled) result.user_agent = network::ws_discovery::advertise(result.user_agent).value();
        result.channel_handshake_seconds = 10;
        result.browser_maximum_payload = maximum_payload;
        result.channel_expiration_minutes = session_expiration_minutes;
        return result;
    }

    bool active = false;
    size_t admitted = 0;
    bool stop_requested = false;
    std::string message = "idle";

    // The cache: the active chain's headers, 80 bytes each from genesis, in a
    // file the embedding Worker keeps on IndexedDB (IDBFS). Never trusted: a
    // restore validates every header again.
    std::string cache_path;
    int32_t cache_height = -1;
    hash_digest cache_tip{};

    // Replays the cached chain through the header organizer. Throws if the
    // file is not this network's valid active chain.
    void restore(std::string const& path, size_t capacity) {
        cache_path = path;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return;
        auto const length = uint64_t(std::streamoff(input.tellg()));
        if (length == 0) return;
        if (length % header_size != 0 || length / header_size > capacity) {
            throw std::runtime_error("its size is not a whole chain within capacity");
        }
        auto const count = length / header_size;
        input.seekg(0);
        std::array<uint8_t, header_size> bytes{};
        auto read_header = [&] {
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
                throw std::runtime_error("it is truncated");
            }
            byte_reader reader(bytes);
            auto header = domain::chain::header::from_data(reader);
            if (!header) throw std::runtime_error("a header does not decode");
            return *header;
        };
        if (domain::chain::hash(read_header()) != index.get_hash(index.active_at(0))) {
            throw std::runtime_error("it starts at another genesis");
        }
        organizer.start();
        for (uint64_t height = 1; height < count;) {
            domain::message::header::list batch;
            auto const end = std::min(count, height + max_get_headers);
            for (; height < end; ++height) batch.emplace_back(read_header());
            auto const result = organizer.add_headers(batch);
            if (result.error || result.headers_added != batch.size()) {
                throw std::runtime_error("a header fails validation");
            }
            if (result.reorg_candidate) organizer.adopt_tip(result.reorg_branch_head);
        }
        organizer.stop();
        if (uint64_t(organizer.header_height()) + 1 != count) {
            throw std::runtime_error("it is not one active chain");
        }
        cache_height = organizer.header_height();
        cache_tip = index.get_hash(index.active_at(cache_height));
    }

    // The file ends with this client's last written tip, at its height.
    bool holds_own_tip() const {
        std::error_code ec;
        if (std::filesystem::file_size(cache_path, ec) != uint64_t(cache_height + 1) * header_size || ec) return false;
        std::ifstream input(cache_path, std::ios::binary);
        input.seekg(std::streamoff(uint64_t(cache_height) * header_size));
        std::array<uint8_t, header_size> bytes{};
        if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
        byte_reader reader(bytes);
        auto const header = domain::chain::header::from_data(reader);
        return header && domain::chain::hash(*header) == cache_tip;
    }

    // Brings the file up to the active chain: appends while the chain it holds
    // is still active, rewrites it after a reorganization. A failure disables
    // the cache (synchronization goes on in memory).
    void persist() noexcept {
        if (cache_path.empty()) return;
        try {
            auto const height = organizer.header_height();
            // Appended only to the file this client last wrote: another client
            // of the same network may have written it since. Otherwise the
            // whole active chain is written again.
            auto const append = cache_height >= 0 && cache_height <= height &&
                index.get_hash(index.active_at(cache_height)) == cache_tip && holds_own_tip();
            if (append && cache_height == height) return;
            std::ofstream output(cache_path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!output) throw std::runtime_error("it cannot be opened");
            for (int32_t at = append ? cache_height + 1 : 0; at <= height; ++at) {
                std::array<uint8_t, header_size> bytes{};
                byte_writer writer(bytes);
                if (!index.get_header(index.active_at(at)).to_data(writer, true)) {
                    throw std::runtime_error("a header does not encode");
                }
                output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            }
            output.flush();
            if (!output) throw std::runtime_error("the write failed");
            cache_height = height;
            cache_tip = index.get_hash(index.active_at(height));
        } catch (std::exception const& e) {
            spdlog::warn("[header_sync] Header cache disabled: {}", e.what());
            std::error_code ignored;
            std::filesystem::remove(cache_path, ignored);
            cache_path.clear();
        }
    }

    impl(std::string const& url, domain::config::network net, size_t capacity)
        : network(net), index(checked_capacity(capacity)), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        if (url.empty()) throw std::invalid_argument("WebSocket bridge URL is required");
        peers.push_back(std::make_unique<remote>(url, "configured"));
        auto genesis = genesis_block(network);
        if (!index.add(genesis.hash(), genesis.header()).inserted) throw std::runtime_error("genesis initialization failed");
        organizer.sync_tip();
    }
};

header_sync::header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity)
    : state_(std::make_unique<impl>(bridge_url, network, capacity)) {
    // Opt-in: only when the embedding Worker mounted this network's directory.
    auto const directory = std::string(cache_root) + std::string(domain::config::chain_name(network,
        domain::config::chain_name_format::display));
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return;
    auto const path = directory + "/" + std::string(cache_file);
    try {
        state_->restore(path, capacity);
    } catch (std::exception const& e) {
        // An invalid cache is discarded and the client starts at genesis.
        spdlog::warn("[header_sync] Header cache discarded: {}", e.what());
        std::filesystem::remove(path, ec);
        state_ = std::make_unique<impl>(bridge_url, network, capacity);
        state_->cache_path = path;
    }
}

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
    state.active = true;
    struct active_guard {
        bool& flag;
        ~active_guard() { flag = false; }
    } guard{state.active};
    state.fatal_error.clear();
    state.message = "connecting";
    state.organizer.start();
    auto executor = co_await ::asio::this_coro::executor;
    {
        kth::task_group tasks("header peers", executor);
        state.sessions = &tasks;
        for (size_t at = 0; at < state.peers.size(); ++at) {
            auto& peer = *state.peers[at];
            peer.complete = false;
            // A rejected peer stays rejected until reset().
            if (!peer.quarantined) spawn_peer(at, follow_tip);
        }
        co_await tasks.join();
        state.sessions = nullptr;
    }
    state.organizer.stop();
    state.persist();
    if (state.stop_requested) {
        state.message = "stopped";
        co_return last_height();
    }
    if (!state.fatal_error.empty()) {
        state.message = state.fatal_error;
        co_return std::unexpected(error::operation_failed);
    }
    for (auto const& peer : state.peers) {
        if (peer->complete) {
            state.message = "caught_up";
            co_return last_height();
        }
    }
    auto const first_error = std::find_if(state.peers.begin(), state.peers.end(),
        [](auto const& peer) { return !peer->error.empty(); });
    state.message = first_error == state.peers.end() ? "no usable header peers" : (*first_error)->error;
    co_return std::unexpected(error::operation_failed);
}

void header_sync::spawn_peer(size_t at, bool follow_tip) {
    auto& state = *state_;
    state.sessions->spawn("header peer", [this, at, follow_tip, &state]() -> ::asio::awaitable<void> {
        try {
            co_await run_peer(at, follow_tip);
        } catch (std::exception const& e) {
            state.fatal_error = std::string("header peer task failed: ") + e.what();
            state.stop_peers();
        }
    });
}

::asio::awaitable<void> header_sync::run_peer(size_t at, bool follow_tip) {
    namespace ws = network::ws_discovery;
    auto& state = *state_;
    auto& remote = *state.peers[at];
    auto executor = co_await ::asio::this_coro::executor;
    unsigned retries = 0;
    while (!state.stop_requested && state.fatal_error.empty() && !remote.quarantined) {
        std::error_code drained;
        while (remote.refresh.try_receive(drained)) {}
        remote.agent.clear();
        remote.status = "connecting";
        state.update_status();
        auto transport = state.transport();
        transport.browser_bridge_url = remote.url;
        try {
            auto connected = co_await ::asio::co_spawn(executor,
                async_connect(executor, "ws-peer", 8333, transport, connect_timeout),
                ::asio::bind_cancellation_slot(remote.cancel.slot(), ::asio::use_awaitable));
            if (!connected) throw std::runtime_error(connected.error().message());
            remote.session = *connected;
            auto peer = remote.session;
            if (state.stop_requested) throw std::runtime_error("header sync stopped");
            kth::task_group tasks("header session", executor);
            std::exception_ptr failure;
            try {
                auto handshake = co_await perform_handshake_direct(*peer,
                    make_handshake_config(transport, 0, 0x48454144455253ULL + at));
                if (!handshake) throw std::runtime_error(handshake.error().message());
                remote.agent = handshake->peer_version->user_agent();
                remote.connected = true;
                remote.error.clear();
                tasks.spawn("peer I/O", [peer]() -> ::asio::awaitable<void> { co_await peer->run(); });

                // Discovery, when both sides negotiated it. A browser has no
                // listener: it answers requests with no endpoints, and adds the
                // endpoints it learns as peers, within the peer limit.
                ws::session_policy policy(state.discovery_enabled && ws::negotiated(transport.user_agent, remote.agent));
                // When the request this session sent is due an answer.
                std::optional<std::chrono::steady_clock::time_point> discovery_due;
                message_handler discovery = [this, &state, &remote, &policy, &transport, &discovery_due, peer, follow_tip](raw_message const& raw)
                        -> ::asio::awaitable<bool> {
                    auto const command = raw.heading.command();
                    if (command != ws::request_command && command != ws::response_command) co_return false;
                    auto const now = uint32_t(zulu_time());
                    if (command == ws::request_command) {
                        auto const request = ws::decode_request(raw.payload);
                        if (!request || !policy.accept_request(*request, now)) throw peer_fault("invalid discovery request");
                        auto payload = ws::encode(ws::response{request->id, {}}, now, state.discovery_allow_private);
                        auto ec = co_await peer->send_raw(discovery_packet(transport.identifier, ws::response_command,
                            std::move(*payload)));
                        if (ec) throw std::runtime_error(ec.message());
                        co_return true;
                    }
                    auto response = ws::decode_response(raw.payload, now, state.discovery_allow_private);
                    if (!response || !policy.accept_response(*response, now)) throw peer_fault("invalid discovery response");
                    discovery_due.reset();
                    for (auto const& endpoint : response->endpoints) {
                        if (state.stop_requested || state.peers.size() >= maximum_peers) break;
                        if (state.discovery_wss_only && endpoint.transport != ws::wss) continue;
                        auto const url = ws::url(endpoint);
                        if (std::any_of(state.peers.begin(), state.peers.end(),
                                [&](auto const& known) { return known->url == url; })) continue;
                        state.peers.push_back(std::make_unique<impl::remote>(url, remote.url));
                        spawn_peer(state.peers.size() - 1, follow_tip);
                    }
                    co_return true;
                };

                hash_digest cursor{};
                unsigned idle = 0;
                uint64_t reach = 0;
                for (;;) {
                    if (state.stop_requested || !state.fatal_error.empty()) break;
                    ws::request query{state.discovery_wss_only ? ws::wss : ws::all_transports, state.discovery_sequence++};
                    if (policy.begin_request(query, uint32_t(zulu_time()))) {
                        auto ec = co_await peer->send_raw(discovery_packet(transport.identifier, ws::request_command,
                            std::move(*ws::encode(query))));
                        if (ec) throw std::runtime_error(ec.message());
                        discovery_due = std::chrono::steady_clock::now() + std::chrono::seconds(ws::response_timeout);
                    }
                    // What the tip was when this request went out.
                    auto const tip_before = state.index.get_hash(state.index.active_at(state.organizer.header_height()));
                    auto hashes = locator(state.index);
                    if (cursor != null_hash && cursor != hashes.front()) hashes.insert(hashes.begin(), cursor);
                    remote.status = "synchronizing";
                    state.update_status();
                    auto sent = co_await peer->send(message::get_headers(hashes, null_hash));
                    if (sent) throw std::runtime_error(sent.message());

                    // One deadline for the whole request, whatever else the
                    // peer sends meanwhile.
                    auto const deadline = std::chrono::steady_clock::now() + response_timeout;
                    std::optional<message::headers> answer;
                    while (!answer) {
                        // Yield even when messages are queued: the other peers,
                        // cancellation and the deadline run under traffic.
                        co_await ::asio::post(executor, ::asio::use_awaitable);
                        auto raw = co_await receive_until(*peer, deadline);
                        if (!raw) throw std::runtime_error("header response timeout");
                        if (co_await discovery(*raw)) continue;
                        byte_reader reader(raw->payload);
                        if (raw->heading.command() == message::ping::command) {
                            co_await answer_ping(*peer, reader);
                            continue;
                        }
                        if (raw->heading.command() != message::headers::command) continue;
                        auto headers = message::headers::from_data(reader, peer->negotiated_version());
                        if (!headers || !headers->is_sequential()) throw peer_fault("invalid headers response");
                        // A tip announced while the answer is pending shares
                        // the command; with an unknown parent it is not the answer.
                        if (!headers->elements().empty() &&
                            state.index.find(headers->elements().front().previous_block_hash()) == blockchain::header_index::null_index) {
                            ++remote.announcements;
                            continue;
                        }
                        answer = std::move(*headers);
                    }

                    if (answer->elements().empty()) {
                        remote.status = "caught_up";
                        remote.complete = true;
                        retries = 0;
                        state.update_status();
                        if (!follow_tip) {
                            // The session ends here: an outstanding discovery
                            // request is still given its time to be answered.
                            // A peer that closes meanwhile does not undo its
                            // completed headers; one that misbehaves still fails.
                            while (discovery_due && std::chrono::steady_clock::now() < *discovery_due) {
                                auto raw = co_await receive_or_close(*peer, *discovery_due);
                                if (!raw) break;
                                if (co_await discovery(*raw)) continue;
                                if (raw->heading.command() == message::ping::command) {
                                    byte_reader reader(raw->payload);
                                    auto ping = message::ping::from_data(reader, peer->negotiated_version());
                                    if (!ping) throw peer_fault("invalid ping");
                                    if (co_await peer->send(message::pong(ping->nonce()))) break;
                                }
                            }
                            break;
                        }
                        co_await wait_for_tip(*peer, remote.refresh, discovery, remote.announcements);
                        continue;
                    }

                    // Headers another peer already delivered take no capacity.
                    // A batch that does not fit is kept as far as it fits and
                    // its peer stops: what lies past the capacity cannot be
                    // validated, so it stops no other peer. The run fails
                    // only when no peer completes.
                    auto const is_unknown = [&](auto const& header) {
                        return state.index.find(domain::chain::hash(header)) == blockchain::header_index::null_index;
                    };
                    auto batch = answer->elements();
                    size_t const unknown = std::count_if(batch.begin(), batch.end(), is_unknown);
                    auto const remaining = state.index.capacity() - state.index.size();
                    bool const overflow = unknown > remaining;
                    if (overflow) {
                        size_t fits = 0;
                        size_t kept = 0;
                        for (; kept < batch.size(); ++kept) {
                            if (is_unknown(batch[kept]) && fits++ == remaining) break;
                        }
                        batch.resize(kept);
                        if (batch.empty()) throw peer_fault("header capacity exceeded");
                    }
                    // Does not suspend: peers validate one batch at a time.
                    auto const accepted = state.organizer.add_headers(batch);
                    // A heavier branch is reported as stale_chain with a reorg
                    // candidate, which is followed. Otherwise stale_chain is a
                    // batch that did not advance the tip (a lighter branch, or
                    // headers another peer delivered first).
                    if (accepted.reorg_candidate) {
                        state.organizer.adopt_tip(accepted.reorg_branch_head);
                    } else if (accepted.error && accepted.error != error::stale_chain) {
                        throw peer_fault(accepted.error.message());
                    }
                    if (overflow) throw peer_fault("header capacity exceeded");
                    cursor = domain::chain::hash(answer->elements().back());
                    auto const known = state.index.find(cursor);
                    auto const reached = known != blockchain::header_index::null_index ? uint64_t(state.index.get_height(known)) : 0;
                    // Progress: new headers, an adopted branch, a tip that
                    // moved meanwhile, or an answer reaching higher than any
                    // before (known headers another peer delivered first).
                    // Three answers in a row without any is a peer that
                    // makes none, whatever headers it repeats.
                    auto const tip_after = state.index.get_hash(state.index.active_at(state.organizer.header_height()));
                    if (unknown > 0 || accepted.reorg_candidate || tip_after != tip_before || reached > reach) {
                        idle = 0;
                    } else if (++idle >= 3) {
                        throw peer_fault("peer made no header progress");
                    }
                    reach = std::max(reach, reached);
                    if (known != blockchain::header_index::null_index) remote.height = reached;
                    remote.complete = false;
                    state.persist();
                }
            } catch (...) {
                failure = std::current_exception();
            }
            peer->stop();
            co_await tasks.join();
            remote.connected = false;
            remote.agent.clear();
            remote.session.reset();
            if (failure) std::rethrow_exception(failure);
            if (!follow_tip || state.stop_requested || !state.fatal_error.empty()) break;
        } catch (peer_fault const& e) {
            remote.error = e.what();
            remote.quarantined = true;
        } catch (std::exception const& e) {
            remote.error = e.what();
        }
        remote.complete = false;
        remote.connected = false;
        remote.agent.clear();
        if (remote.session) remote.session->stop();
        remote.session.reset();
        if (state.stop_requested || !state.fatal_error.empty()) break;
        remote.status = remote.quarantined ? "rejected" : "disconnected";
        state.update_status();
        if (remote.quarantined || !follow_tip) break;

        // Following: reconnect after a pause that grows to a minute, offset
        // per peer so they do not retry together. A refresh or a stop ends it.
        remote.status = "retry";
        auto const attempt = retries++;
        auto const delay = std::chrono::seconds(std::min(retry_maximum_seconds, retry_initial_seconds << std::min(attempt, 4u))) +
            std::chrono::milliseconds((at * 137 + attempt * 53) % 1000);
        remote.retry = std::make_unique<kth::steady_timer>(executor, delay);
        co_await remote.retry->async_wait(::asio::as_tuple(::asio::use_awaitable));
        remote.retry.reset();
    }
    if (state.stop_requested) {
        remote.connected = false;
        remote.agent.clear();
        remote.status = "stopped";
    }
    state.update_status();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_stop() const {
    state_->stop_requested = true;
    state_->stop_peers();
    co_return last_height();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_refresh() const {
    // A full doorbell already represents this work; one locator refresh satisfies all requests.
    if (state_->active && !state_->stop_requested) {
        for (auto& peer : state_->peers) {
            if (peer->connected) peer->refresh.try_send(0u);
            if (peer->retry) peer->retry->cancel();
        }
    }
    co_return last_height();
}

header_sync::admission header_sync::admit() const { return admission(state_->admitted); }
bool header_sync::settled() const { return !state_->active && state_->admitted == 0; }

std::error_code header_sync::reset() {
    if (!settled()) return error::operation_failed;
    state_->stop_requested = false;
    state_->fatal_error.clear();
    // An explicit restart gives rejected peers another chance.
    for (auto& peer : state_->peers) {
        peer->quarantined = false;
        peer->error.clear();
        peer->status = "idle";
    }
    state_->message = "idle";
    return {};
}

bool header_sync::add_peer(std::string const& url) {
    auto& peers = state_->peers;
    if (!settled() || peers.size() >= maximum_peers || !valid_peer_url(url)) return false;
    if (std::any_of(peers.begin(), peers.end(), [&](auto const& peer) { return peer->url == url; })) return false;
    peers.push_back(std::make_unique<impl::remote>(url, "configured"));
    return true;
}

bool header_sync::configure_discovery(bool enabled, bool allow_private, bool wss_only) {
    if (!settled()) return false;
    state_->discovery_enabled = enabled;
    state_->discovery_allow_private = allow_private;
    state_->discovery_wss_only = wss_only;
    return true;
}

size_t header_sync::peer_count() const { return state_->peers.size(); }

std::string header_sync::peer_endpoint(size_t at) const { return at < peer_count() ? state_->peers[at]->url : std::string{}; }
std::string header_sync::peer_source(size_t at) const { return at < peer_count() ? state_->peers[at]->source : std::string{}; }
std::string header_sync::peer_state(size_t at) const { return at < peer_count() ? state_->peers[at]->status : std::string{}; }
std::string header_sync::peer_agent(size_t at) const { return at < peer_count() ? state_->peers[at]->agent : std::string{}; }
std::string header_sync::peer_error(size_t at) const { return at < peer_count() ? state_->peers[at]->error : std::string{}; }
uint64_t header_sync::peer_height(size_t at) const { return at < peer_count() ? state_->peers[at]->height : 0; }
uint64_t header_sync::peer_announcements(size_t at) const { return at < peer_count() ? state_->peers[at]->announcements : 0; }
bool header_sync::peer_connected(size_t at) const { return at < peer_count() && state_->peers[at]->connected; }

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
