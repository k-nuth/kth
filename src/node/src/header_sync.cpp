// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#include <kth/node/detail/header_sync_test_seam.hpp>
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
#include <fmt/format.h>
#include <kth/blockchain/validate/validate_header.hpp>
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
#include <deque>
#include <set>
#include <map>
#include <type_traits>

namespace kth::node {
namespace {
using namespace kth::domain;
using namespace kth::network;

constexpr size_t maximum_capacity = 2'000'000;

// Armed only by the test seam.
bool transaction_requests_fault = false;

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

// Why a peer's session ended. A peer at fault (it sent invalid data) is not
// retried until reset(); a lost session is.
struct session_failure {
    std::string reason;
    bool peer_fault = false;
};

template <typename T = void>
using session_result = std::expected<T, session_failure>;

std::unexpected<session_failure> peer_fault(std::string reason) {
    return std::unexpected(session_failure{std::move(reason), true});
}

std::unexpected<session_failure> session_lost(std::string reason) {
    return std::unexpected(session_failure{std::move(reason), false});
}

// Handles a message that is not part of the header exchange (discovery,
// transactions); true when it was one.
using message_handler = std::function<::asio::awaitable<session_result<bool>>(raw_message const&)>;

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
// session is a failure.
::asio::awaitable<session_result<std::optional<raw_message>>> receive_until(peer_session& peer,
        std::chrono::steady_clock::time_point deadline, refresh_channel* refresh = nullptr) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
    if (refresh) {
        auto completion = co_await make_select(receive(peer.messages()), receive(*refresh), after(deadline))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        if (ec) co_return session_lost("peer session closed");
        if (selected.index != 0) co_return std::nullopt;
        co_return std::move(*selected.value<0>());
    }
    auto completion = co_await make_select(receive(peer.messages()), after(deadline))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) co_return session_lost("peer session closed");
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

::asio::awaitable<session_result<>> answer_ping(peer_session& peer, byte_reader& reader) {
    auto ping = message::ping::from_data(reader, peer.negotiated_version());
    if (!ping) co_return peer_fault("invalid ping");
    auto ec = co_await peer.send(message::pong(ping->nonce()));
    if (ec) co_return session_lost(ec.message());
    co_return session_result<>{};
}

// At the tip: until the peer announces a block, a refresh is requested, or a
// periodic refresh is due. It never opens another connection. Pings and
// discovery are answered meanwhile.
::asio::awaitable<session_result<>> wait_for_tip(peer_session& peer, refresh_channel& refresh_request,
        message_handler const& other, uint64_t& announcements) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const refresh = std::chrono::steady_clock::now() + tip_refresh_interval;
    for (;;) {
        auto received = co_await receive_until(peer, refresh, &refresh_request);
        if (!received) co_return std::unexpected(received.error());
        if (!*received) co_return session_result<>{};
        auto const& raw = **received;
        auto handled = co_await other(raw);
        if (!handled) co_return std::unexpected(handled.error());
        if (*handled) continue;
        byte_reader reader(raw.payload);
        auto const command = raw.heading.command();
        if (command == message::ping::command) {
            auto answered = co_await answer_ping(peer, reader);
            if (!answered) co_return answered;
        } else if (command == message::inventory::command) {
            auto inventory = message::inventory::from_data(reader, peer.negotiated_version());
            if (!inventory) co_return peer_fault("invalid inventory");
            if (inventory->count(message::inventory::type_id::block) != 0) {
                ++announcements;
                co_return session_result<>{};
            }
        } else if (command == message::headers::command) {
            auto headers = message::headers::from_data(reader, peer.negotiated_version());
            if (!headers || !headers->is_sequential()) co_return peer_fault("invalid headers announcement");
            // Fetched from our locator, so an announcement cannot bypass validation.
            if (!headers->elements().empty()) {
                ++announcements;
                co_return session_result<>{};
            }
        }
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }
}

// A small JSON object writer for the snapshots. Strings are escaped; 64-bit
// integers past JavaScript's safe range are written as strings.
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += char(ch);
        } else if (ch < 0x20) {
            out += fmt::format("\\u{:04x}", ch);
        } else {
            out += char(ch);
        }
    }
    return out + '"';
}

class json_object {
public:
    void raw(std::string_view key, std::string_view value) {
        out_ += first_ ? "" : ",";
        first_ = false;
        out_ += json_string(key);
        out_ += ':';
        out_ += value;
    }
    void put(std::string_view key, std::string_view value) { raw(key, json_string(value)); }
    void put(std::string_view key, char const* value) { put(key, std::string_view(value)); }
    void put(std::string_view key, bool value) { raw(key, value ? "true" : "false"); }
    template <typename Integer>
        requires std::is_integral_v<Integer>
    void put(std::string_view key, Integer value) {
        constexpr int64_t safe = 9007199254740991;
        auto const unsafe = std::is_signed_v<Integer> ? (int64_t(value) > safe || int64_t(value) < -safe)
                                                      : uint64_t(value) > uint64_t(safe);
        raw(key, unsafe ? json_string(fmt::format("{}", value)) : fmt::format("{}", value));
    }
    void put(std::string_view key, double value) { raw(key, fmt::format("{}", value)); }
    std::string finish() const { return "{" + out_ + "}"; }
private:
    std::string out_;
    bool first_ = true;
};

template <typename Range, typename Encode>
std::string json_array(Range const& values, Encode encode) {
    std::string out = "[";
    bool first = true;
    for (auto const& value : values) {
        out += first ? "" : ",";
        first = false;
        out += encode(value);
    }
    return out + "]";
}

std::string compact_hex(uint32_t value) { return fmt::format("0x{:08x}", value); }

// Content sampling bounds.
constexpr uint64_t content_window = 100;
constexpr size_t content_budget = 16 * 1024 * 1024;
constexpr size_t body_limit = 32 * 1024 * 1024;
constexpr size_t transaction_limit = 1'000'000;
constexpr size_t pool_limit = 1000;
constexpr size_t pool_budget = 8 * 1024 * 1024;
constexpr uint64_t pool_lifetime_seconds = 20 * 60;
constexpr size_t transaction_requests = 16;
constexpr auto request_lifetime = std::chrono::seconds(10);
constexpr auto body_timeout = std::chrono::seconds(5);

struct sampled_block {
    hash_digest hash{};
    uint64_t bytes = 0;
    uint64_t transactions = 0;
    data_chunk coinbase;
    hash_list txids;
    hash_list proof;
    // What it holds, allocations included.
    size_t memory() const { return coinbase.capacity() + (txids.capacity() + proof.capacity()) * hash_size + 128; }
};

// A transaction asked of a peer, and when.
struct pending_request {
    std::chrono::steady_clock::time_point at;
    size_t owner = 0;
};

struct observed_transaction {
    uint64_t bytes = 0;
    uint64_t received = 0;
};

hash_digest merkle_pair(hash_digest const& left, hash_digest const& right) {
    data_chunk pair(left.begin(), left.end());
    pair.insert(pair.end(), right.begin(), right.end());
    return bitcoin_hash(pair);
}

// The coinbase's Merkle branch: its sibling at each level up to the root.
hash_list coinbase_proof(hash_list level) {
    hash_list proof;
    while (level.size() > 1) {
        if (level.size() % 2 != 0) level.push_back(level.back());
        proof.push_back(level[1]);
        for (size_t i = 0; i < level.size() / 2; ++i) level[i] = merkle_pair(level[2 * i], level[2 * i + 1]);
        level.resize(level.size() / 2);
    }
    return proof;
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
        // Bodies this peer did not provide, and when: not asked again for 30 s.
        std::map<hash_digest, uint64_t> unavailable_bodies;
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

    bool sample_blocks = false;
    bool observe_transactions = false;
    std::map<uint64_t, sampled_block> blocks;
    std::set<uint64_t> requested_blocks;
    std::map<hash_digest, observed_transaction> pool;
    std::map<hash_digest, pending_request> requested_transactions;
    uint64_t transaction_announcements = 0;
    uint64_t transactions_received = 0;
    uint64_t content_revision = 0;
    uint64_t pool_revision = 0;
    uint64_t body_floor = 0;
    uint64_t rejected_bodies = 0;
    std::string content_error;

    // Sampled blocks stay within the window, on the active chain and within
    // the budget: transaction ids are dropped first, then the oldest blocks.
    void prune_content() {
        auto const tip = uint64_t(organizer.header_height());
        auto const floor = tip >= content_window ? tip - content_window + 1 : 0;
        for (auto it = blocks.begin(); it != blocks.end();) {
            auto const at = it->first <= tip ? index.active_at(int32_t(it->first)) : blockchain::header_index::null_index;
            if (it->first < floor || at == blockchain::header_index::null_index || index.get_hash(at) != it->second.hash) {
                it = blocks.erase(it);
                ++content_revision;
            } else {
                ++it;
            }
        }
        auto memory = [&] {
            size_t total = 0;
            for (auto const& entry : blocks) total += entry.second.memory();
            return total;
        };
        for (auto& entry : blocks) {
            if (memory() <= content_budget) break;
            if (!entry.second.txids.empty()) {
                // Released, not only emptied.
                hash_list{}.swap(entry.second.txids);
                ++content_revision;
            }
        }
        while (!blocks.empty() && memory() > content_budget) {
            body_floor = std::max(body_floor, blocks.begin()->first + 1);
            blocks.erase(blocks.begin());
            ++content_revision;
        }
    }

    // Observations expire, and stay within count and size.
    void prune_pool() {
        auto const now = uint64_t(zulu_time());
        for (auto it = pool.begin(); it != pool.end();) {
            if (now >= it->second.received + pool_lifetime_seconds) {
                it = pool.erase(it);
                ++pool_revision;
            } else {
                ++it;
            }
        }
        auto const steady = std::chrono::steady_clock::now();
        std::erase_if(requested_transactions, [&](auto const& entry) { return steady >= entry.second.at + request_lifetime; });
        auto bytes = [&] {
            size_t total = 0;
            for (auto const& entry : pool) total += entry.second.bytes;
            return total;
        };
        while (pool.size() > pool_limit || bytes() > pool_budget) {
            pool.erase(std::min_element(pool.begin(), pool.end(),
                [](auto const& a, auto const& b) { return a.second.received < b.second.received; }));
            ++pool_revision;
        }
    }

    network::settings transport() const {
        network::settings result(network);
        // It stores headers: it serves no blocks and relays no transactions.
        result.services = 0;
        result.relay_transactions = observe_transactions;
        result.validate_checksum = true;
        result.user_agent = "/KTH-browser-headers/";
        if (discovery_enabled) result.user_agent = network::ws_discovery::advertise(result.user_agent).value();
        result.channel_handshake_seconds = 10;
        // Blocks up to 32 MiB when sampling them, transactions up to 1 MB
        // when observing them; otherwise headers.
        result.browser_maximum_payload = sample_blocks ? body_limit : observe_transactions ? transaction_limit : maximum_payload;
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

    // Replays the cached chain through the header organizer. Fails, with
    // the reason, if the file is not this network's valid active chain.
    std::expected<void, std::string> restore(std::string const& path, size_t capacity) {
        cache_path = path;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return {};
        auto const length = uint64_t(std::streamoff(input.tellg()));
        if (length == 0) return {};
        if (length % header_size != 0 || length / header_size > capacity) {
            return std::unexpected("its size is not a whole chain within capacity");
        }
        auto const count = length / header_size;
        input.seekg(0);
        std::array<uint8_t, header_size> bytes{};
        auto read_header = [&]() -> std::expected<domain::chain::header, std::string> {
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
                return std::unexpected("it is truncated");
            }
            byte_reader reader(bytes);
            auto header = domain::chain::header::from_data(reader);
            if (!header) return std::unexpected("a header does not decode");
            return *header;
        };
        auto const first = read_header();
        if (!first) return std::unexpected(first.error());
        if (domain::chain::hash(*first) != index.get_hash(index.active_at(0))) {
            return std::unexpected("it starts at another genesis");
        }
        organizer.start();
        struct stop_organizer {
            blockchain::header_organizer& organizer;
            ~stop_organizer() { organizer.stop(); }
        } guard{organizer};
        for (uint64_t height = 1; height < count;) {
            domain::message::header::list batch;
            auto const end = std::min(count, height + max_get_headers);
            for (; height < end; ++height) {
                auto header = read_header();
                if (!header) return std::unexpected(header.error());
                batch.emplace_back(std::move(*header));
            }
            auto const result = organizer.add_headers(batch);
            if (result.error || result.headers_added != batch.size()) {
                return std::unexpected("a header fails validation");
            }
            if (result.reorg_candidate) organizer.adopt_tip(result.reorg_branch_head);
        }
        if (uint64_t(organizer.header_height()) + 1 != count) {
            return std::unexpected("it is not one active chain");
        }
        cache_height = organizer.header_height();
        cache_tip = index.get_hash(index.active_at(cache_height));
        return {};
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
    void persist() {
        if (cache_path.empty()) return;
        auto const written = write_cache();
        if (written) return;
        spdlog::warn("[header_sync] Header cache disabled: {}", written.error());
        std::error_code ignored;
        std::filesystem::remove(cache_path, ignored);
        cache_path.clear();
    }

    std::expected<void, std::string> write_cache() {
        auto const height = organizer.header_height();
        // Appended only to the file this client last wrote: another client
        // of the same network may have written it since. Otherwise the
        // whole active chain is written again.
        auto const append = cache_height >= 0 && cache_height <= height &&
            index.get_hash(index.active_at(cache_height)) == cache_tip && holds_own_tip();
        if (append && cache_height == height) return {};
        std::ofstream output(cache_path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!output) return std::unexpected("it cannot be opened");
        for (int32_t at = append ? cache_height + 1 : 0; at <= height; ++at) {
            std::array<uint8_t, header_size> bytes{};
            byte_writer writer(bytes);
            if (!index.get_header(index.active_at(at)).to_data(writer, true)) {
                return std::unexpected("a header does not encode");
            }
            output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        }
        output.flush();
        if (!output) return std::unexpected("the write failed");
        cache_height = height;
        cache_tip = index.get_hash(index.active_at(height));
        return {};
    }

    impl(std::string const& url, domain::config::network net, size_t capacity)
        : network(net), index(capacity), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        peers.push_back(std::make_unique<remote>(url, "configured"));
        // The index is empty and has room: the genesis header always fits.
        auto genesis = genesis_block(network);
        [[maybe_unused]] auto const added = index.add(genesis.hash(), genesis.header());
        assert(added.inserted);
        organizer.sync_tip();
    }
};

std::expected<std::unique_ptr<header_sync>, std::error_code> header_sync::create(std::string const& bridge_url,
        domain::config::network network, size_t capacity) {
    if (bridge_url.empty() || capacity == 0 || capacity > maximum_capacity) {
        return std::unexpected(error::operation_failed);
    }
    return std::unique_ptr<header_sync>(new header_sync(bridge_url, network, capacity));
}

header_sync::header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity)
    : state_(std::make_unique<impl>(bridge_url, network, capacity)) {
    // Opt-in: only when the embedding Worker mounted this network's directory.
    auto const directory = std::string(cache_root) + std::string(domain::config::chain_name(network,
        domain::config::chain_name_format::display));
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return;
    auto const path = directory + "/" + std::string(cache_file);
    auto const restored = state_->restore(path, capacity);
    if (!restored) {
        // An invalid cache is discarded and the client starts at genesis.
        spdlog::warn("[header_sync] Header cache discarded: {}", restored.error());
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
        auto session = [&]() -> ::asio::awaitable<session_result<>> {
            auto [thrown, connected] = co_await ::asio::co_spawn(executor,
                async_connect(executor, "ws-peer", 8333, transport, connect_timeout),
                ::asio::bind_cancellation_slot(remote.cancel.slot(), ::asio::as_tuple(::asio::use_awaitable)));
            // Asio reports a cancelled connection attempt as an exception.
            if (thrown) co_return session_lost(state.stop_requested ? "header sync stopped" : "connection failed");
            if (!connected) co_return session_lost(connected.error().message());
            remote.session = *connected;
            auto peer = remote.session;
            if (state.stop_requested) co_return session_lost("header sync stopped");
            // This session's transactions to ask for, and the doorbell of the
            // task that asks for them. Outlive the task group that joins it.
            std::deque<hash_digest> wanted_transactions;
            kth::select_channel<unsigned> wanted_doorbell{1};
            // A session task that fails ends its session: it records why and
            // stops the peer, which the exchange sees as a closed session.
            std::string task_failure;
            kth::task_group tasks("header session", executor);
            auto spawn_task = [&](char const* name, std::function<::asio::awaitable<void>()> body) {
                tasks.spawn(name, [&task_failure, peer, body = std::move(body)]() -> ::asio::awaitable<void> {
                    try {
                        co_await body();
                    } catch (std::exception const& e) {
                        // From a library (allocation, Asio).
                        if (task_failure.empty()) task_failure = e.what();
                        peer->stop();
                    }
                });
            };
            auto exchange = [&]() -> ::asio::awaitable<session_result<>> {
                auto handshake = co_await perform_handshake_direct(*peer,
                    make_handshake_config(transport, 0, 0x48454144455253ULL + at));
                if (!handshake) co_return session_lost(handshake.error().message());
                remote.agent = handshake->peer_version->user_agent();
                remote.connected = true;
                remote.error.clear();
                spawn_task("peer I/O", [peer]() -> ::asio::awaitable<void> { co_await peer->run(); });

                // Discovery, when both sides negotiated it. A browser has no
                // listener: it answers requests with no endpoints, and adds the
                // endpoints it learns as peers, within the peer limit.
                ws::session_policy policy(state.discovery_enabled && ws::negotiated(transport.user_agent, remote.agent));
                // When the request this session sent is due an answer.
                std::optional<std::chrono::steady_clock::time_point> discovery_due;
                message_handler discovery = [this, &state, &remote, &policy, &transport, &discovery_due, peer, follow_tip](raw_message const& raw)
                        -> ::asio::awaitable<session_result<bool>> {
                    auto const command = raw.heading.command();
                    if (command != ws::request_command && command != ws::response_command) co_return false;
                    auto const now = uint32_t(zulu_time());
                    if (command == ws::request_command) {
                        auto const request = ws::decode_request(raw.payload);
                        if (!request || !policy.accept_request(*request, now)) co_return peer_fault("invalid discovery request");
                        auto payload = ws::encode(ws::response{request->id, {}}, now, state.discovery_allow_private);
                        auto ec = co_await peer->send_raw(discovery_packet(transport.identifier, ws::response_command,
                            std::move(*payload)));
                        if (ec) co_return session_lost(ec.message());
                        co_return true;
                    }
                    auto response = ws::decode_response(raw.payload, now, state.discovery_allow_private);
                    if (!response || !policy.accept_response(*response, now)) co_return peer_fault("invalid discovery response");
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

                // Transaction observation: announced transactions are queued
                // here, and one task asks for them, at most 16 outstanding for
                // this peer; an unanswered request expires after 10 s, and the
                // task wakes for it as for new work.
                if (state.observe_transactions) {
                    spawn_task("transaction requests", [&state, peer, at, &wanted_transactions, &wanted_doorbell]()
                            -> ::asio::awaitable<void> {
                        using namespace ::asio::experimental;
                        auto executor = co_await ::asio::this_coro::executor;
                        if (std::exchange(transaction_requests_fault, false)) {
                            throw std::runtime_error("transaction requests failed (test seam)");
                        }
                        // Until the session ends: its doorbell closes, or a send fails.
                        for (;;) {
                            state.prune_pool();
                            auto const mine = [&](auto const& entry) { return entry.second.owner == at; };
                            auto outstanding = size_t(std::count_if(state.requested_transactions.begin(),
                                state.requested_transactions.end(), mine));
                            hash_list wanted;
                            while (!wanted_transactions.empty() && outstanding < transaction_requests) {
                                auto const hash = wanted_transactions.front();
                                wanted_transactions.pop_front();
                                if (state.pool.contains(hash) || state.requested_transactions.contains(hash)) continue;
                                state.requested_transactions[hash] = {std::chrono::steady_clock::now(), at};
                                ++outstanding;
                                wanted.push_back(hash);
                            }
                            if (!wanted.empty()) {
                                auto request = message::get_data::create(wanted, message::inventory::type_id::transaction);
                                if (!request || co_await peer->send(*request)) co_return;
                            }
                            auto due = std::chrono::steady_clock::time_point::max();
                            for (auto const& entry : state.requested_transactions) {
                                if (mine(entry)) due = std::min(due, entry.second.at + request_lifetime);
                            }
                            auto completion = co_await make_select(receive(wanted_doorbell), after(due))
                                .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
                            if (std::get<0>(completion)) co_return;
                        }
                    });
                }
                auto const ring = [&wanted_doorbell] { (void)wanted_doorbell.try_send(0u); };
                message_handler other = [&state, peer, at, discovery, ring, &wanted_transactions](raw_message const& raw)
                        -> ::asio::awaitable<session_result<bool>> {
                    auto discovered = co_await discovery(raw);
                    if (!discovered || *discovered) co_return discovered;
                    if (!state.observe_transactions) co_return false;
                    auto const command = raw.heading.command();
                    byte_reader reader(raw.payload);
                    if (command == message::inventory::command) {
                        auto inventory = message::inventory::from_data(reader, peer->negotiated_version());
                        if (!inventory) co_return peer_fault("invalid inventory");
                        for (auto const& item : inventory->inventories()) {
                            if (item.type() != message::inventory::type_id::transaction) continue;
                            ++state.transaction_announcements;
                            if (wanted_transactions.size() >= pool_limit) break;
                            if (state.pool.contains(item.hash()) || state.requested_transactions.contains(item.hash())) continue;
                            if (std::find(wanted_transactions.begin(), wanted_transactions.end(), item.hash()) != wanted_transactions.end()) continue;
                            wanted_transactions.push_back(item.hash());
                        }
                        ring();
                        // Block announcements in it are the header exchange's.
                        co_return false;
                    }
                    if (command == message::transaction::command) {
                        auto transaction = message::transaction::from_data(reader, peer->negotiated_version());
                        if (!transaction || reader.remaining_size() != 0 || transaction->check(body_limit, true, true)) {
                            co_return peer_fault("invalid transaction structure");
                        }
                        ++state.transactions_received;
                        // Kept only when it was asked for.
                        if (state.requested_transactions.erase(domain::chain::hash(*transaction)) != 0) {
                            state.pool[domain::chain::hash(*transaction)] = {raw.payload.size(), uint64_t(zulu_time())};
                            ++state.pool_revision;
                            state.prune_pool();
                        }
                        ring();
                        co_return true;
                    }
                    if (command == message::not_found::command) {
                        auto missing = message::not_found::from_data(reader, peer->negotiated_version());
                        if (!missing) co_return peer_fault("invalid notfound");
                        // Only this peer's requests: another peer's stay
                        // pending, whatever this one has or had.
                        for (auto const& item : missing->inventories()) {
                            if (item.type() != message::inventory::type_id::transaction) continue;
                            auto const request = state.requested_transactions.find(item.hash());
                            if (request != state.requested_transactions.end() && request->second.owner == at) {
                                state.requested_transactions.erase(request);
                            }
                        }
                        ring();
                    }
                    co_return false;
                };

                // Block sampling: the most recent active block not yet sampled,
                // within the window, fetched and checked against its header.
                // True when there may be more to fetch: a body was handled
                // (sampled, or overtaken by a reorganization) or the peer has
                // not got it (the next call tries the next block). False when
                // there is nothing to fetch, or the peer did not answer.
                auto download_one = [&]() -> ::asio::awaitable<session_result<bool>> {
                    if (!state.sample_blocks || state.requested_blocks.size() >= 2) co_return false;
                    state.prune_content();
                    auto const tip = uint64_t(state.organizer.header_height());
                    auto const floor = std::max(state.body_floor, tip >= content_window ? tip - content_window + 1 : 0);
                    auto const now = uint64_t(zulu_time());
                    std::optional<uint64_t> selected;
                    hash_digest expected{};
                    for (auto height = tip; height >= floor && height > 0; --height) {
                        auto const hash = state.index.get_hash(state.index.active_at(int32_t(height)));
                        auto const refused = remote.unavailable_bodies.find(hash);
                        if (state.blocks.contains(height) || state.requested_blocks.contains(height)) continue;
                        if (refused != remote.unavailable_bodies.end() && now < refused->second + 30) continue;
                        selected = height;
                        expected = hash;
                        break;
                    }
                    if (!selected) co_return false;
                    auto const height = *selected;
                    state.requested_blocks.insert(height);
                    struct release {
                        impl& state;
                        uint64_t height;
                        ~release() { state.requested_blocks.erase(height); }
                    } guard{state, height};
                    auto request = message::get_data::create(hash_list{expected}, message::inventory::type_id::block);
                    if (!request) co_return session_lost("block request");
                    auto ec = co_await peer->send(*request);
                    if (ec) co_return session_lost(ec.message());
                    auto const deadline = std::chrono::steady_clock::now() + body_timeout;
                    bool not_found = false;
                    while (!state.stop_requested) {
                        auto received = co_await receive_until(*peer, deadline);
                        if (!received) co_return std::unexpected(received.error());
                        auto& raw = *received;
                        if (!raw) break;
                        auto const command = raw->heading.command();
                        if (command == message::not_found::command) {
                            byte_reader reader(raw->payload);
                            auto missing = message::not_found::from_data(reader, peer->negotiated_version());
                            if (!missing) co_return peer_fault("invalid notfound");
                            if (std::any_of(missing->inventories().begin(), missing->inventories().end(), [&](auto const& item) {
                                    return item.type() == message::inventory::type_id::block && item.hash() == expected;
                                })) {
                                not_found = true;
                                break;
                            }
                            auto handled = co_await other(*raw);
                            if (!handled) co_return std::unexpected(handled.error());
                            continue;
                        }
                        auto handled = co_await other(*raw);
                        if (!handled) co_return std::unexpected(handled.error());
                        if (*handled) continue;
                        byte_reader reader(raw->payload);
                        if (command == message::ping::command) {
                            auto answered = co_await answer_ping(*peer, reader);
                            if (!answered) co_return std::unexpected(answered.error());
                            continue;
                        }
                        if (command == message::headers::command) {
                            ++remote.announcements;
                            continue;
                        }
                        if (command != message::block::command) continue;
                        auto body = message::block::from_data(reader, peer->negotiated_version());
                        if (!body || reader.remaining_size() != 0) co_return peer_fault("invalid block encoding");
                        // A late reply to an earlier request.
                        if (domain::chain::hash(body->header()) != expected) continue;
                        auto const at = state.index.active_at(int32_t(height));
                        // Reorganized while it was fetched.
                        if (at == blockchain::header_index::null_index || state.index.get_hash(at) != expected) co_return true;
                        auto const error = body->check_body();
                        if (error || !body->is_distinct_transaction_set()) {
                            ++state.rejected_bodies;
                            state.content_error = error ? error.message() : "duplicate block transactions";
                            co_return peer_fault("invalid block body: " + state.content_error);
                        }
                        sampled_block record;
                        record.hash = expected;
                        record.bytes = raw->payload.size();
                        record.transactions = body->transactions().size();
                        auto const& coinbase = body->transactions().front();
                        record.coinbase.resize(coinbase.serialized_size(true));
                        byte_writer writer(record.coinbase);
                        if (!coinbase.to_data(writer, true)) co_return peer_fault("coinbase serialization");
                        for (auto const& transaction : body->transactions()) {
                            auto const hash = domain::chain::hash(transaction);
                            record.txids.push_back(hash);
                            // Confirmed: no longer an observation.
                            if (state.pool.erase(hash) != 0) ++state.pool_revision;
                            state.requested_transactions.erase(hash);
                        }
                        record.proof = coinbase_proof(record.txids);
                        state.blocks[height] = std::move(record);
                        ++state.content_revision;
                        state.prune_content();
                        co_return true;
                    }
                    if (remote.unavailable_bodies.size() >= content_window) remote.unavailable_bodies.clear();
                    remote.unavailable_bodies[expected] = uint64_t(zulu_time());
                    co_return not_found;
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
                        if (ec) co_return session_lost(ec.message());
                        discovery_due = std::chrono::steady_clock::now() + std::chrono::seconds(ws::response_timeout);
                    }
                    // What the tip was when this request went out.
                    auto const tip_before = state.index.get_hash(state.index.active_at(state.organizer.header_height()));
                    auto hashes = locator(state.index);
                    if (cursor != null_hash && cursor != hashes.front()) hashes.insert(hashes.begin(), cursor);
                    remote.status = "synchronizing";
                    state.update_status();
                    auto sent = co_await peer->send(message::get_headers(hashes, null_hash));
                    if (sent) co_return session_lost(sent.message());

                    // One deadline for the whole request, whatever else the
                    // peer sends meanwhile.
                    auto const deadline = std::chrono::steady_clock::now() + response_timeout;
                    std::optional<message::headers> answer;
                    while (!answer) {
                        // Yield even when messages are queued: the other peers,
                        // cancellation and the deadline run under traffic.
                        co_await ::asio::post(executor, ::asio::use_awaitable);
                        auto received = co_await receive_until(*peer, deadline);
                        if (!received) co_return std::unexpected(received.error());
                        auto& raw = *received;
                        if (!raw) co_return session_lost("header response timeout");
                        auto handled = co_await other(*raw);
                        if (!handled) co_return std::unexpected(handled.error());
                        if (*handled) continue;
                        byte_reader reader(raw->payload);
                        if (raw->heading.command() == message::ping::command) {
                            auto answered = co_await answer_ping(*peer, reader);
                            if (!answered) co_return std::unexpected(answered.error());
                            continue;
                        }
                        if (raw->heading.command() != message::headers::command) continue;
                        auto headers = message::headers::from_data(reader, peer->negotiated_version());
                        if (!headers || !headers->is_sequential()) co_return peer_fault("invalid headers response");
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
                        // At the tip: sample a body, then ask again for headers.
                        auto sampled = co_await download_one();
                        if (!sampled) co_return std::unexpected(sampled.error());
                        if (*sampled) continue;
                        if (!follow_tip) {
                            // The session ends here: an outstanding discovery
                            // request is still given its time to be answered.
                            // A peer that closes meanwhile does not undo its
                            // completed headers; one that misbehaves still fails.
                            while (discovery_due && std::chrono::steady_clock::now() < *discovery_due) {
                                auto raw = co_await receive_or_close(*peer, *discovery_due);
                                if (!raw) break;
                                auto handled = co_await other(*raw);
                                if (!handled) co_return std::unexpected(handled.error());
                                if (*handled) continue;
                                if (raw->heading.command() == message::ping::command) {
                                    byte_reader reader(raw->payload);
                                    auto ping = message::ping::from_data(reader, peer->negotiated_version());
                                    if (!ping) co_return peer_fault("invalid ping");
                                    if (co_await peer->send(message::pong(ping->nonce()))) break;
                                }
                            }
                            break;
                        }
                        auto waited = co_await wait_for_tip(*peer, remote.refresh, other, remote.announcements);
                        if (!waited) co_return waited;
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
                        if (batch.empty()) co_return peer_fault("header capacity exceeded");
                    }
                    // Does not suspend: peers validate one batch at a time.
                    auto const accepted = state.organizer.add_headers(batch);
                    // A heavier branch is reported as stale_chain with a reorg
                    // candidate, which is followed. Otherwise stale_chain is a
                    // batch that did not advance the tip (a lighter branch, or
                    // headers another peer delivered first).
                    if (accepted.reorg_candidate) {
                        state.organizer.adopt_tip(accepted.reorg_branch_head);
                        // Observations were relative to the abandoned chain.
                        state.pool.clear();
                        state.requested_transactions.clear();
                        ++state.pool_revision;
                    } else if (accepted.error && accepted.error != error::stale_chain) {
                        co_return peer_fault(accepted.error.message());
                    }
                    if (overflow) co_return peer_fault("header capacity exceeded");
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
                        co_return peer_fault("peer made no header progress");
                    }
                    reach = std::max(reach, reached);
                    if (known != blockchain::header_index::null_index) remote.height = reached;
                    remote.complete = false;
                    state.prune_content();
                    state.persist();
                }
                co_return session_result<>{};
            };
            session_result<> result;
            try {
                result = co_await exchange();
            } catch (std::exception const& e) {
                // An exception from a library (allocation, Asio) ends the
                // session as a lost one, after its tasks are joined.
                result = session_lost(e.what());
            }
            wanted_doorbell.close();
            peer->stop();
            co_await tasks.join();
            // The closed session a failed task caused is reported by its cause.
            if (!task_failure.empty() && (result || !result.error().peer_fault)) result = session_lost(task_failure);
            std::erase_if(state.requested_transactions, [at](auto const& entry) { return entry.second.owner == at; });
            remote.connected = false;
            remote.agent.clear();
            remote.session.reset();
            co_return result;
        };
        auto const ended = co_await session();
        if (ended && (!follow_tip || state.stop_requested || !state.fatal_error.empty())) break;
        if (!ended) {
            remote.error = ended.error().reason;
            if (ended.error().peer_fault) remote.quarantined = true;
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

bool header_sync::configure_content(bool blocks, bool transactions) {
    if (!settled()) return false;
    state_->sample_blocks = blocks;
    state_->observe_transactions = transactions;
    return true;
}

std::string header_sync::block_metadata(uint64_t height, bool include_transactions) const {
    auto const& state = *state_;
    auto const found = state.blocks.find(height);
    if (found == state.blocks.end() || height > last_height()) return "null";
    auto const& block = found->second;
    // Still the active block at that height.
    if (state.index.get_hash(state.index.active_at(int32_t(height))) != block.hash) return "null";
    json_object result;
    result.put("height", height);
    result.put("hash", encode_hash(block.hash));
    result.put("bytes", block.bytes);
    result.put("transactions", block.transactions);
    result.put("verification", "header-merkle-structure");
    result.put("txids_retained", !block.txids.empty());
    if (include_transactions) {
        result.put("coinbase_hex", encode_base16(block.coinbase));
        result.raw("txids", json_array(block.txids, [](auto const& hash) { return json_string(encode_hash(hash)); }));
        result.raw("coinbase_merkle_proof", json_array(block.proof, [](auto const& hash) { return json_string(encode_hash(hash)); }));
    }
    byte_reader reader(block.coinbase);
    auto const coinbase = domain::chain::transaction::from_data(reader, true);
    if (coinbase && !coinbase->inputs().empty()) {
        // The printable bytes of its input script, after the BIP34 height push.
        auto const& script = coinbase->inputs().front().script().bytes();
        size_t const start = !script.empty() && script[0] >= 1 && script[0] <= 5 && script.size() > script[0] ? script[0] + 1 : 0;
        std::string tag;
        for (size_t i = start; i < script.size(); ++i) tag += script[i] >= 32 && script[i] <= 126 ? char(script[i]) : ' ';
        result.put("coinbase_tag", tag);
    }
    return result.finish();
}

std::string header_sync::content_state() const {
    auto const& state = *state_;
    size_t block_bytes = 0;
    for (auto const& entry : state.blocks) block_bytes += entry.second.memory();
    size_t pool_bytes = 0;
    for (auto const& entry : state.pool) pool_bytes += entry.second.bytes;
    json_object result;
    result.put("blocks_enabled", state.sample_blocks);
    result.put("transactions_enabled", state.observe_transactions);
    result.put("revision", state.content_revision);
    result.put("pool_revision", state.pool_revision);
    result.put("cached_blocks", state.blocks.size());
    result.put("cache_bytes", block_bytes);
    result.put("inflight_blocks", state.requested_blocks.size());
    result.put("rejected_bodies", state.rejected_bodies);
    result.put("error", state.content_error);
    result.put("transaction_announcements", state.transaction_announcements);
    result.put("transactions_received", state.transactions_received);
    result.put("pending_transactions", state.requested_transactions.size());
    result.put("observed_transactions", state.pool.size());
    result.put("observed_bytes", pool_bytes);
    return result.finish();
}

std::string header_sync::observed_transactions() const {
    auto const now = uint64_t(zulu_time());
    std::string out = "[";
    bool first = true;
    for (auto const& [hash, observed] : state_->pool) {
        if (now >= observed.received + pool_lifetime_seconds) continue;
        json_object item;
        item.put("txid", encode_hash(hash));
        item.put("bytes", observed.bytes);
        item.put("received_at", observed.received);
        out += first ? "" : ",";
        first = false;
        out += item.finish();
    }
    return out + "]";
}

std::string header_sync::chain_state_snapshot() const {
    auto const& index = state_->index;
    auto const height = state_->organizer.header_height();
    auto const at = index.active_at(height);
    if (at == blockchain::header_index::null_index || height == 0) return "null";
    auto const header = index.get_header(at);
    auto const hash = index.get_hash(at);
    blockchain::validate_header validator(state_->consensus, state_->network);
    auto const result = validator.chain_state_at(header, hash, height, index.get_parent_index(at), index);
    if (!result) {
        json_object failure;
        failure.put("error", result.error().message());
        return failure.finish();
    }
    auto const& chain = **result;
    json_object snapshot;
    snapshot.put("height", chain.height());
    snapshot.put("hash", encode_hash(hash));
    snapshot.put("parent_hash", encode_hash(header.previous_block_hash()));
    snapshot.put("timestamp", header.timestamp());
    snapshot.put("version", header.version());
    snapshot.put("bits", compact_hex(header.bits()));
    snapshot.put("minimum_version", chain.minimum_version());
    snapshot.put("median_time_past", chain.median_time_past());
    snapshot.put("work_required", compact_hex(chain.work_required()));
    snapshot.put("enabled_flags", uint64_t(chain.enabled_flags()));
    snapshot.put("is_under_checkpoint", chain.is_under_checkpoint());
    snapshot.put("checkpoint_conflict", chain.is_checkpoint_conflict(hash));
    snapshot.put("is_retarget_height", domain::chain::chain_state::is_retarget_height(size_t(height)));
    snapshot.put("is_null", chain.is_null());
#if defined(KTH_CURRENCY_BCH)
    snapshot.put("block_interval_seconds", chain.block_interval());
    snapshot.put("tick", chain.tick());
    snapshot.put("parent_tick", chain.parent_tick());
    snapshot.put("faster_blocks_active", chain.is_faster_blocks_active());
    json_object asert;
    auto const anchor = chain.assert_anchor_block_info();
    asert.put("anchor_height", anchor.height);
    asert.put("ancestor_timestamp", anchor.ancestor_timestamp);
    asert.put("anchor_bits", compact_hex(anchor.bits));
    asert.put("half_life_seconds", chain.asert_half_life());
    snapshot.raw("asert", asert.finish());
    json_object upgrades;
    upgrades.put("pythagoras", chain.is_pythagoras_enabled());
    upgrades.put("euclid", chain.is_euclid_enabled());
    upgrades.put("pisano", chain.is_pisano_enabled());
    upgrades.put("mersenne", chain.is_mersenne_enabled());
    upgrades.put("fermat", chain.is_fermat_enabled());
    upgrades.put("euler", chain.is_euler_enabled());
    upgrades.put("gauss", chain.is_gauss_enabled());
    upgrades.put("descartes", chain.is_descartes_enabled());
    upgrades.put("lobachevski", chain.is_lobachevski_enabled());
    upgrades.put("galois", chain.is_galois_enabled());
    upgrades.put("leibniz", chain.is_leibniz_enabled());
    upgrades.put("cantor", chain.is_cantor_enabled());
    snapshot.raw("upgrades", upgrades.finish());
    // A header-only state seeds ABLA from defaults, not from the active
    // chain's block sizes: it is not reported as if it were.
    json_object abla;
    abla.put("available", false);
    abla.put("reason", "Requires the block-size history of the active chain");
    snapshot.raw("abla", abla.finish());
#endif
    return snapshot.finish();
}

std::string header_sync::runtime_settings() const {
    auto const& state = *state_;
    auto const name = std::string(domain::config::chain_name(state.network, domain::config::chain_name_format::display));
    json_object root;

    json_object node;
    node.put("sync_peers", maximum_peers);
    node.put("sync_timeout_seconds", uint32_t(response_timeout.count()));
    root.raw("node", node.finish());

    auto const t = state.transport();
    json_object network;
    network.put("identifier", t.identifier);
    network.put("protocol_maximum", t.protocol_maximum);
    network.put("protocol_minimum", t.protocol_minimum);
    network.put("services", t.services);
    network.put("invalid_services", t.invalid_services);
    network.put("relay_transactions", t.relay_transactions);
    network.put("validate_checksum", t.validate_checksum);
    network.put("user_agent", t.user_agent);
    network.put("browser_maximum_payload", t.browser_maximum_payload);
    network.put("channel_handshake_seconds", t.channel_handshake_seconds);
    network.put("channel_expiration_minutes", t.channel_expiration_minutes);
    root.raw("network", network.finish());

    auto const& c = state.consensus;
    json_object chain;
    chain.put("easy_blocks", c.easy_blocks);
    chain.put("retarget", c.retarget);
    chain.put("reorganization_limit", c.reorganization_limit);
    chain.put("max_reorg_depth", c.max_reorg_depth);
    chain.put("max_checkpoint_height", c.max_checkpoint_height);
    chain.raw("checkpoints", json_array(c.checkpoints, [](auto const& value) { return json_string(value.to_string()); }));
#if defined(KTH_CURRENCY_BCH)
    chain.put("bch_uahf", c.bch_uahf);
    chain.put("bch_daa_cw144", c.bch_daa_cw144);
    chain.put("bch_pythagoras", c.bch_pythagoras);
    chain.put("bch_euclid", c.bch_euclid);
    chain.put("bch_pisano", c.bch_pisano);
    chain.put("bch_mersenne", c.bch_mersenne);
    chain.put("bch_fermat", c.bch_fermat);
    chain.put("bch_euler", c.bch_euler);
    chain.put("bch_gauss", c.bch_gauss);
    chain.put("bch_descartes", c.bch_descartes);
    chain.put("bch_lobachevski", c.bch_lobachevski);
    chain.put("bch_galois", c.bch_galois);
    chain.put("bch_leibniz", c.bch_leibniz);
    chain.put("leibniz_activation_time", c.leibniz_activation_time);
    chain.put("cantor_activation_time", c.cantor_activation_time);
    chain.put("asert_half_life", c.asert_half_life);
#endif
    root.raw("chain", chain.finish());

    json_object sync;
    sync.put("network", name);
    sync.put("index_capacity", state.index.capacity());
    sync.put("discovery_enabled", state.discovery_enabled);
    sync.put("discovery_allow_private", state.discovery_allow_private);
    sync.put("discovery_wss_only", state.discovery_wss_only);
    sync.put("connect_timeout_seconds", uint32_t(connect_timeout.count()));
    sync.put("tip_refresh_seconds", uint32_t(tip_refresh_interval.count()));
    sync.put("retry_initial_seconds", retry_initial_seconds);
    sync.put("retry_maximum_seconds", retry_maximum_seconds);
    sync.put("cache_directory", std::string(cache_root) + name);
    sync.put("download_blocks", state.sample_blocks);
    sync.put("observe_transactions", state.observe_transactions);
    sync.put("recent_block_window", content_window);
    sync.put("content_cache_bytes", content_budget);
    sync.put("maximum_block_payload_bytes", body_limit);
    sync.put("observed_transaction_limit", pool_limit);
    sync.put("observed_transaction_bytes", pool_budget);
    sync.put("observed_transaction_expiry_seconds", pool_lifetime_seconds);
    sync.raw("peer_endpoints", json_array(state.peers, [](auto const& peer) { return json_string(peer->url); }));
    root.raw("header_sync", sync.finish());
    return root.finish();
}

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

void detail::fail_next_transaction_requests() { transaction_requests_fault = true; }
} // namespace kth::node
#endif
