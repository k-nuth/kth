// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/header_sync.hpp>
#if defined(KTH_BROWSER_NODE)
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <iomanip>
#include <kth/infrastructure/utility/browser/runtime.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/ws_discovery.hpp>
#include <functional>
#include <map>
#include <deque>
#include <set>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/transaction.hpp>
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
#include <vector>
#include <algorithm>
#include <sstream>
#include <limits>
#include <type_traits>
#include <kth/node/settings.hpp>

namespace kth::node {
namespace {
using namespace kth::domain;
using namespace kth::network;

constexpr unsigned header_peer_limit = 8;
constexpr size_t content_window = 100, content_budget = 16 * 1024 * 1024;
constexpr size_t body_limit = 32 * 1024 * 1024, pool_budget = 8 * 1024 * 1024;
constexpr size_t pool_limit = 1000, transaction_requests = 16;

struct sampled_block {
    hash_digest hash{};
    uint64_t bytes = 0, transactions = 0;
    data_chunk coinbase;
    hash_list txids, proof;
    size_t memory() const { return coinbase.size() + (txids.size() + proof.size()) * 32 + 128; }
};
struct observed_transaction { data_chunk wire; uint64_t received = 0; };

hash_digest merkle_pair(hash_digest const& left, hash_digest const& right) {
    data_chunk pair(left.begin(), left.end()); pair.insert(pair.end(), right.begin(), right.end());
    return bitcoin_hash(pair);
}
hash_list coinbase_proof(hash_list level) {
    hash_list proof;
    while (level.size() > 1) {
        if (level.size() % 2) level.push_back(level.back());
        proof.push_back(level[1]);
        for (size_t i = 0; i < level.size() / 2; ++i) level[i] = merkle_pair(level[2*i], level[2*i+1]);
        level.resize(level.size()/2);
    }
    return proof;
}
constexpr unsigned connect_timeout_seconds = 10;
constexpr unsigned retry_initial_seconds = 5;
constexpr unsigned retry_maximum_seconds = 60;
constexpr unsigned retry_offset_range_ms = 1000;

node::settings effective_node_settings(domain::config::network network) {
    node::settings result(network);
    result.sync_peers = header_peer_limit;
    result.sync_timeout_seconds = 30;
    result.block_latency_seconds = 10;
    result.refresh_transactions = false;
    result.compact_blocks_high_bandwidth = false;
    result.ds_proofs_enabled = false;
    return result;
}

network::settings effective_transport_settings(domain::config::network network) {
    network::settings result(network);
    // This actor stores headers and does not serve block bodies or transactions.
    result.services = 0;
    result.relay_transactions = false;
    result.browser_maximum_payload = body_limit;
    result.validate_checksum = true;
    result.user_agent = "/KTH-browser-headers/";
    result.channel_handshake_seconds = 10;
    // Preserve the normal node's 60-minute peer rotation. follow() owns
    // reconnection/backoff and keeps its validated index across rotations.
    return result;
}

// Small, typed JSON snapshot writer. Peer URLs and native strings must be escaped;
// 64-bit consensus values beyond Number.MAX_SAFE_INTEGER are preserved as strings.
std::string json_string(std::string const& text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += ch;
    }
    return out + '"';
}
struct settings_json {
    std::ostringstream output;
    bool first = true;
    settings_json() { output << std::boolalpha << '{'; }
    void raw(std::string const& key, std::string const& value) {
        if (!std::exchange(first, false)) output << ',';
        output << json_string(key) << ':' << value;
    }
    void put(std::string const& key, std::string const& value) { raw(key, json_string(value)); }
    template<class T> requires std::is_arithmetic_v<T>
    void put(std::string const& key, T value) {
        std::ostringstream encoded; encoded << std::boolalpha;
        if constexpr (std::is_integral_v<T> && sizeof(T) >= 8) {
            if (value > T(9007199254740991LL)) { raw(key, json_string(std::to_string(value))); return; }
            if constexpr (std::is_signed_v<T>) {
                if (value < T(-9007199254740991LL)) { raw(key, json_string(std::to_string(value))); return; }
            }
        }
        encoded << value; raw(key, encoded.str());
    }
    std::string finish() { return output.str() + '}'; }
};
template<class Range, class Encode>
std::string settings_array(Range const& values, Encode encode) {
    std::string result = "[";
    bool first = true;
    for (auto const& value : values) {
        if (!std::exchange(first, false)) result += ',';
        result += encode(value);
    }
    return result + ']';
}

bool valid_peer_url(std::string const& url) {
    auto const prefix = url.starts_with("ws://") ? 5u : url.starts_with("wss://") ? 6u : 0u;
    if (!prefix || url.size() > 2048 || url.find_first_of("\r\n\t #") != std::string::npos) return false;
    auto const end = url.find_first_of("/?", prefix);
    auto const authority = url.substr(prefix, end == std::string::npos ? end : end - prefix);
    return !authority.empty() && authority.find('@') == std::string::npos;
}

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

struct peer_fault : std::runtime_error { using std::runtime_error::runtime_error; };
struct capacity_fault : std::runtime_error { using std::runtime_error::runtime_error; };

using refresh_channel = kth::select_channel<unsigned>;
using message_channel = kth::select_channel<raw_message>;
using announcement_handler = std::function<void()>;
using discovery_handler = std::function<::asio::awaitable<bool>(raw_message const&)>;

data_chunk discovery_packet(uint32_t magic, std::string_view command, data_chunk payload) {
    domain::message::heading heading(magic, std::string(command), static_cast<uint32_t>(payload.size()), bitcoin_checksum(payload));
    auto packet = heading.to_data(); packet.insert(packet.end(), payload.begin(), payload.end()); return packet;
}

::asio::awaitable<std::optional<raw_message>> receive_timed(message_channel& incoming,
        std::chrono::steady_clock::time_point expires, refresh_channel* refresh = nullptr) {
    using namespace ::asio::experimental;
    auto executor = co_await ::asio::this_coro::executor;
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
::asio::awaitable<void> wait_for_tip(peer_session& peer, message_channel& incoming, refresh_channel& refresh_request, std::chrono::seconds interval, discovery_handler const& discovery, announcement_handler const& announced) {
    auto executor = co_await ::asio::this_coro::executor;
    auto const refresh = std::chrono::steady_clock::now() + interval;
    for (;;) {
        auto const now = std::chrono::steady_clock::now();
        if (now >= refresh) co_return;
        auto raw = co_await receive_timed(incoming, refresh, &refresh_request);
        if (!raw) co_return;
        if (co_await discovery(*raw)) continue;
        byte_reader reader(raw->payload);
        auto const command = raw->heading.command();
        if (command == message::ping::command) {
            auto ping = message::ping::from_data(reader, peer.negotiated_version());
            if (!ping) throw std::runtime_error("invalid ping");
            auto ec = co_await peer.send(message::pong(ping->nonce()));
            if (ec) throw std::runtime_error(ec.message());
        } else if (command == message::inventory::command) {
            auto inventory = message::inventory::from_data(reader, peer.negotiated_version());
            if (!inventory) throw peer_fault("invalid inventory");
            if (inventory->count(message::inventory::type_id::block) != 0) { announced(); co_return; }
        } else if (command == message::headers::command) {
            auto headers = message::headers::from_data(reader, peer.negotiated_version());
            if (!headers || !headers->is_sequential()) throw peer_fault("invalid headers announcement");
            // Fetch from our locator so unsolicited announcements cannot bypass validation.
            if (!headers->elements().empty()) { announced(); co_return; }
        }
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }
}

} // namespace

struct header_sync::impl {
    std::shared_ptr<browser::runtime> runtime = worker_runtime();
    std::string bridge;
    domain::config::network network;
    node::settings runtime_node;
    network::settings transport;
    blockchain::header_index index;
    blockchain::settings consensus;
    blockchain::header_organizer organizer;
    struct remote {
        explicit remote(std::string endpoint) : url(std::move(endpoint)) {}
        std::string url, status = "idle", agent, error, source = "configured";
        uint64_t height = 0, announcements = 0, last_pool_request = 0;
        std::map<std::string,uint64_t> unavailable_bodies;
        bool connected = false, complete = false, quarantined = false;
        std::shared_ptr<network::peer_session> session;
        ::asio::cancellation_signal cancel;
        refresh_channel refresh{1};
        std::unique_ptr<browser::timer> retry;
    };
    std::vector<std::unique_ptr<remote>> peers;
    kth::task_group* sessions = nullptr;
    bool discovery_enabled = true, discovery_allow_private = false, discovery_wss_only = false;
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
    bool sample_blocks = false, observe_transactions = false;
    std::map<uint64_t, sampled_block> blocks;
    std::set<uint64_t> requested_blocks;
    std::map<std::string, observed_transaction> pool;
    std::map<std::string, uint64_t> requested_transactions;
    uint64_t transaction_announcements = 0, transactions_received = 0;
    uint64_t content_revision = 0, pool_revision = 0, body_floor = 0, rejected_bodies = 0;
    std::string content_error;
    std::string content_path() const {
        return cache_path.empty() ? "" : "/kth-block-cache/" + std::string(domain::config::name(network)) + "/content-v1.bin";
    }
    void prune_content() {
        auto const tip = uint64_t(organizer.header_height());
        auto const floor = tip >= content_window ? tip - content_window + 1 : 0;
        for (auto it = blocks.begin(); it != blocks.end();) {
            auto at = it->first <= tip ? index.active_at(static_cast<int32_t>(it->first)) : blockchain::header_index::null_index;
            if (it->first < floor || at == blockchain::header_index::null_index || index.get_hash(at) != it->second.hash) {
                it = blocks.erase(it); ++content_revision;
            } else ++it;
        }
        auto memory = [&] { size_t n=0; for (auto const& [height,b] : blocks) n+=b.memory(); return n; };
        // Keep summaries/coinbases when txid lists exceed the byte budget.
        for (auto& [height,b] : blocks) {
            if (memory() <= content_budget) break;
            if (!b.txids.empty()) { b.txids.clear(); ++content_revision; }
        }
        while (!blocks.empty() && memory() > content_budget) {
            body_floor = std::max(body_floor, blocks.begin()->first + 1);
            blocks.erase(blocks.begin()); ++content_revision;
        }
    }
    void prune_pool() {
        auto now = uint64_t(zulu_time());
        for (auto it=pool.begin(); it!=pool.end();) {
            if (now >= it->second.received + 1200) { it=pool.erase(it); ++pool_revision; } else ++it;
        }
        for (auto it=requested_transactions.begin(); it!=requested_transactions.end();) {
            if (now >= it->second + 10) it=requested_transactions.erase(it); else ++it;
        }
        auto bytes=[&] { size_t n=0; for(auto const& [hash,tx]:pool)n+=tx.wire.size();return n; };
        while (pool.size()>pool_limit || bytes()>pool_budget) {
            auto oldest=std::min_element(pool.begin(),pool.end(),[](auto const& a,auto const& b){return a.second.received<b.second.received;});
            pool.erase(oldest);++pool_revision;
        }
    }
    void save_content() noexcept {
        auto path=content_path(); if(path.empty())return;
        try {
            auto temporary=path+".next"; std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
            auto integer=[&](uint64_t n){ for(unsigned i=0;i<8;++i)out.put(char(n>>(8*i))); };
            out.write("KTHBC001",8);integer(blocks.size());integer(body_floor);
            for(auto const& [height,b]:blocks) {
                integer(height);out.write(reinterpret_cast<char const*>(b.hash.data()),32);
                integer(b.bytes);integer(b.transactions);integer(b.coinbase.size());
                out.write(reinterpret_cast<char const*>(b.coinbase.data()),b.coinbase.size());
                integer(b.proof.size());for(auto const& h:b.proof)out.write(reinterpret_cast<char const*>(h.data()),32);
                integer(b.txids.size());for(auto const& h:b.txids)out.write(reinterpret_cast<char const*>(h.data()),32);
            }
            out.flush();if(!out)throw std::runtime_error("content cache write failed");out.close();
            std::filesystem::rename(temporary,path);
        } catch(std::exception const& e) { content_error=e.what(); }
    }
    void restore_content() {
        auto path=content_path();if(path.empty())return;
        std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return;
        try {
            if(in.tellg()<24 || in.tellg()>std::streamoff(content_budget+65536))throw std::runtime_error("content cache size");
            in.seekg(0);char magic[8];in.read(magic,8);if(std::string(magic,8)!="KTHBC001")throw std::runtime_error("content cache version");
            auto integer=[&](){uint64_t n=0;for(unsigned i=0;i<8;++i){auto c=in.get();if(c==EOF)throw std::runtime_error("truncated content cache");n|=uint64_t(uint8_t(c))<<(8*i);}return n;};
            auto count=integer();body_floor=integer();if(body_floor>uint64_t(organizer.header_height())+1)throw std::runtime_error("content cache floor");if(count>content_window)throw std::runtime_error("content cache count");
            for(uint64_t i=0;i<count;++i) {
                auto height=integer();sampled_block b;in.read(reinterpret_cast<char*>(b.hash.data()),32);b.bytes=integer();b.transactions=integer();
                auto length=integer();if(length>1000000 || !b.transactions || b.transactions>body_limit/10 || b.bytes>body_limit)throw std::runtime_error("content cache bounds");
                b.coinbase.resize(length);in.read(reinterpret_cast<char*>(b.coinbase.data()),length);
                auto read_hashes=[&](size_t limit){auto n=integer();if(n>limit)throw std::runtime_error("content hash bounds");hash_list result(n);for(auto& h:result)in.read(reinterpret_cast<char*>(h.data()),32);if(!in)throw std::runtime_error("truncated content hashes");return result;};
                b.proof=read_hashes(32);b.txids=read_hashes(content_budget/32);
                auto at=height<=uint64_t(organizer.header_height())?index.active_at(static_cast<int32_t>(height)):blockchain::header_index::null_index;
                if(at==blockchain::header_index::null_index || index.get_hash(at)!=b.hash)continue;
                byte_reader reader(b.coinbase);auto tx=chain::transaction::from_data(reader,true);
                if(!tx || reader.remaining_size() || !tx->is_coinbase())throw std::runtime_error("content coinbase");
                auto root=chain::hash(*tx);for(auto const& sibling:b.proof)root=merkle_pair(root,sibling);
                if(root!=index.get_header(at).merkle())throw std::runtime_error("content coinbase proof");
                size_t proof_depth=0;for(auto count=b.transactions;count>1;count=(count+1)/2)++proof_depth;
                if(b.proof.size()!=proof_depth)throw std::runtime_error("content proof depth");
                if(!b.txids.empty() && (b.txids.size()!=b.transactions || b.txids.front()!=chain::hash(*tx)))throw std::runtime_error("content txids");
                if(!b.txids.empty()) {
                    auto hashes=b.txids;
                    while(hashes.size()>1){if(hashes.size()%2)hashes.push_back(hashes.back());for(size_t j=0;j<hashes.size()/2;++j)hashes[j]=merkle_pair(hashes[j*2],hashes[j*2+1]);hashes.resize(hashes.size()/2);}
                    if(hashes.front()!=root)throw std::runtime_error("content txid root");
                }
                blocks[height]=std::move(b);
            }
            if(in.peek()!=EOF)throw std::runtime_error("content cache trailing bytes");
            prune_content();++content_revision;
        }catch(std::exception const& e){blocks.clear();body_floor=0;content_error=e.what();}
    }

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
        : bridge(url), network(net), runtime_node(effective_node_settings(net)), transport(effective_transport_settings(net)), index(checked_capacity(capacity)), consensus(validation_settings(net)),
          organizer(index, consensus, net) {
        if (!valid_peer_url(bridge))
            throw std::invalid_argument("WS/WSS peer URL is required");
        peers.push_back(std::make_unique<remote>(bridge));
        transport.user_agent = network::ws_discovery::advertise(transport.user_agent).value();
        auto genesis = full_node::get_genesis_block(network);
        if (!index.add(genesis.hash(), genesis.header()).inserted) throw std::runtime_error("genesis initialization failed");
        organizer.sync_tip();
        restore_cache(capacity);
        restore_content();
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
    state.active = true;
    struct active_guard { bool& flag; ~active_guard() { flag = false; } } guard{state.active};
    state.fatal_error.clear();
    state.message = "connecting";
    state.organizer.start();
    auto executor = co_await ::asio::this_coro::executor;
    kth::task_group tasks("header peers", executor);
    state.sessions = &tasks;
    struct group_guard { impl& state; ~group_guard() { state.sessions = nullptr; } } session_guard{state};
    std::exception_ptr failure;
    try {
        for (size_t at = 0; at < state.peers.size(); ++at) {
            auto& peer = *state.peers[at];
            peer.complete = false;
            // Quarantine survives transient reconnects and is reset only by an explicit restart.
            if (!peer.quarantined) spawn_peer(at, follow_tip);
        }
    } catch (...) { failure = std::current_exception(); state.fatal_error = "unable to start header peer tasks"; state.stop_peers(); }
    co_await tasks.join();
    state.organizer.stop();
    state.persist_cache();
    if (state.stop_requested) { state.message = "stopped"; co_return last_height(); }
    if (failure) { state.message = "unable to start header peer tasks"; co_return std::unexpected(error::operation_failed); }
    if (!state.fatal_error.empty()) { state.message = state.fatal_error; co_return std::unexpected(error::operation_failed); }
    for (auto const& peer : state.peers) if (peer->complete) { state.message = "caught_up"; co_return last_height(); }
    state.message = state.peers.front()->error.empty() ? "no usable header peers" : state.peers.front()->error;
    co_return std::unexpected(error::operation_failed);
}

void header_sync::spawn_peer(size_t at, bool follow_tip) {
    auto& state = *state_;
    state.sessions->spawn("header peer", [this, at, follow_tip, &state]() -> ::asio::awaitable<void> {
        try { co_await run_peer(at, follow_tip); }
        catch (...) { state.fatal_error = "header peer task failed"; state.stop_peers(); }
    });
}

::asio::awaitable<void> header_sync::run_peer(size_t at, bool follow_tip) {
    auto& state = *state_;
    auto& remote = *state.peers[at];
    auto executor = co_await ::asio::this_coro::executor;
    unsigned retries = 0;
    while (!state.stop_requested && state.fatal_error.empty() && !remote.quarantined) {
        std::error_code drained;
        while (remote.refresh.try_receive(drained)) {}
        remote.agent.clear(); remote.status = "connecting"; state.update_status();
        auto transport = state.transport;
        transport.browser_bridge_url = remote.url;
        try {
            auto connected = co_await ::asio::co_spawn(executor,
                async_connect(executor, "ws-peer", 8333, transport, std::chrono::seconds(connect_timeout_seconds)),
                ::asio::bind_cancellation_slot(remote.cancel.slot(), ::asio::use_awaitable));
            if (!connected) throw std::runtime_error(connected.error().message());
            remote.session = *connected;
            auto peer = remote.session;
            if (state.stop_requested) throw std::runtime_error("header sync stopped");
            message_channel incoming{1};
            kth::task_group tasks("header session", executor);
            std::exception_ptr failure;
            try {
                auto handshake = co_await perform_handshake_direct(*peer, make_handshake_config(transport, 0, 0x48454144455253ULL + at));
                if (!handshake) throw std::runtime_error(handshake.error().message());
                remote.agent = handshake->peer_version->user_agent();
                remote.connected = true; remote.error.clear();
                tasks.spawn("peer I/O", [peer]() -> ::asio::awaitable<void> { co_await peer->run(); });
                tasks.spawn("inbound handoff", [peer, &incoming]() -> ::asio::awaitable<void> { co_await forward_messages(*peer, incoming); });
                namespace ws = network::ws_discovery;
                ws::session_policy policy(state.discovery_enabled && ws::negotiated(transport.user_agent, remote.agent));
                discovery_handler discovery = [this, &state, &remote, &policy, peer, follow_tip](raw_message const& raw) -> ::asio::awaitable<bool> {
                    auto const command = raw.heading.command();
                    if (command != ws::request_command && command != ws::response_command) co_return false;
                    auto const now = static_cast<uint32_t>(zulu_time());
                    if (command == ws::request_command) {
                        auto request = ws::decode_request(raw.payload);
                        if (!request || !policy.accept_request(*request, now)) throw peer_fault("invalid WS discovery request");
                        // A browser has no listener. Never advertise a synthetic browser endpoint.
                        // Relaying addresses requires a separate public-DNS verification policy.
                        auto payload = ws::encode(ws::response{request->id, {}}, now, state.discovery_allow_private);
                        auto ec = co_await peer->send_raw(discovery_packet(state.transport.identifier, ws::response_command, std::move(*payload)));
                        if (ec) throw std::runtime_error(ec.message());
                    } else {
                        auto response = ws::decode_response(raw.payload, now, state.discovery_allow_private);
                        if (!response || !policy.accept_response(*response, now)) throw peer_fault("invalid WS discovery response");
                        for (auto const& endpoint : response->endpoints) {
                            if (state.stop_requested || state.peers.size() >= state.runtime_node.sync_peers) break;
                            if (state.discovery_wss_only && endpoint.transport != ws::wss) continue;
                            auto const url = ws::url(endpoint);
                            if (std::any_of(state.peers.begin(), state.peers.end(), [&](auto const& candidate) { return candidate->url == url; })) continue;
                            auto candidate = std::make_unique<impl::remote>(url);
                            candidate->source = remote.url;
                            state.peers.push_back(std::move(candidate));
                            spawn_peer(state.peers.size() - 1, follow_tip);
                        }
                    }
                    co_return true;
                };
                std::deque<hash_digest> transaction_queue;
                std::set<std::string> queued_transactions;
                auto request_transactions = [&state,peer,&transaction_queue,&queued_transactions]() -> ::asio::awaitable<void> {
                    state.prune_pool();hash_list wanted;
                    while(!transaction_queue.empty() && state.requested_transactions.size()<transaction_requests){
                        auto hash=transaction_queue.front();transaction_queue.pop_front();auto key=encode_hash(hash);queued_transactions.erase(key);
                        if(state.pool.contains(key) || state.requested_transactions.contains(key))continue;
                        state.requested_transactions[key]=zulu_time();wanted.push_back(hash);
                    }
                    if(!wanted.empty()){auto request=message::get_data::create(wanted,message::inventory::type_id::transaction);auto ec=co_await peer->send(*request);if(ec)throw std::runtime_error(ec.message());}
                };
                auto protocol_discovery = std::move(discovery);
                discovery = [&state,peer,protocol_discovery,request_transactions,&transaction_queue,&queued_transactions](raw_message const& raw) -> ::asio::awaitable<bool> {
                    if(co_await protocol_discovery(raw))co_return true;
                    state.prune_pool();
                    if(state.observe_transactions && raw.heading.command()==message::inventory::command) {
                        byte_reader reader(raw.payload);auto inv=message::inventory::from_data(reader,peer->negotiated_version());
                        if(!inv)throw peer_fault("invalid transaction inventory");
                        for(auto const& item:inv->inventories()) {
                            if(item.type()!=message::inventory::type_id::transaction)continue;
                            ++state.transaction_announcements;
                            auto key=encode_hash(item.hash());
                            if(transaction_queue.size()>=pool_limit)break;
                            if(!state.pool.contains(key) && !state.requested_transactions.contains(key) && queued_transactions.insert(key).second)transaction_queue.push_back(item.hash());
                        }
                        co_await request_transactions();
                        co_return false;
                    }
                    if(raw.heading.command()==message::transaction::command) {
                        if(!state.observe_transactions || raw.payload.size()>1000000)co_return true;
                        byte_reader reader(raw.payload);auto tx=message::transaction::from_data(reader,peer->negotiated_version());
                        if(!tx || reader.remaining_size() || tx->check(body_limit,true,true))throw peer_fault("invalid observed transaction structure");
                        ++state.transactions_received;
                        auto key=encode_hash(chain::hash(*tx));
                        // Only retain transactions that were requested through bounded INV handling.
                        if(state.requested_transactions.erase(key)){state.pool[key]={raw.payload,uint64_t(zulu_time())};++state.pool_revision;state.prune_pool();}
                        co_await request_transactions();
                        co_return true;
                    }
                    if(raw.heading.command()=="notfound" && state.observe_transactions){
                        byte_reader reader(raw.payload);auto missing=message::inventory::from_data(reader,peer->negotiated_version());
                        if(!missing)throw peer_fault("invalid notfound inventory");
                        for(auto const& item:missing->inventories())if(item.type()==message::inventory::type_id::transaction)state.requested_transactions.erase(encode_hash(item.hash()));
                    }
                    co_await request_transactions();
                    co_return false;
                };
                auto download_one = [&]() -> ::asio::awaitable<bool> {
                    if(!state.sample_blocks || state.requested_blocks.size()>=2)co_return false;
                    state.prune_content();
                    auto tip=uint64_t(state.organizer.header_height());
                    auto floor=std::max(state.body_floor,tip>=content_window?tip-content_window+1:0);
                    std::optional<uint64_t> selected;hash_digest expected{};
                    for(auto height=tip;;--height) {
                        if(height<floor)break;
                        auto at=state.index.active_at(static_cast<int32_t>(height));auto hash=state.index.get_hash(at);
                        if(!state.blocks.contains(height) && !state.requested_blocks.contains(height) && (!remote.unavailable_bodies.contains(encode_hash(hash)) || uint64_t(zulu_time())>=remote.unavailable_bodies[encode_hash(hash)]+30)){selected=height;expected=hash;break;}
                        if(height==0)break;
                    }
                    if(!selected)co_return false;
                    auto height=*selected;state.requested_blocks.insert(height);
                    struct release { impl& state;uint64_t height;~release(){state.requested_blocks.erase(height);} } guard{state,height};
                    auto request=message::get_data::create(hash_list{expected},message::inventory::type_id::block);
                    auto ec=co_await peer->send(*request);if(ec)throw std::runtime_error(ec.message());
                    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    for(unsigned ignored=0;ignored<128 && !state.stop_requested;++ignored) {
                        auto now=std::chrono::steady_clock::now();if(now>=deadline)break;
                        auto raw=co_await receive_timed(incoming,deadline);if(!raw)break;
                        if(co_await discovery(*raw))continue;
                        if(raw->heading.command()==message::ping::command){byte_reader r(raw->payload);auto ping=message::ping::from_data(r,peer->negotiated_version());if(!ping)throw peer_fault("invalid ping");co_await peer->send(message::pong(ping->nonce()));continue;}
                        if(raw->heading.command()==message::headers::command){++remote.announcements;continue;}
                        if(raw->heading.command()=="notfound") {
                            byte_reader r(raw->payload);auto missing=message::inventory::from_data(r,peer->negotiated_version());
                            if(!missing)throw peer_fault("invalid notfound inventory");
                            if(std::any_of(missing->inventories().begin(),missing->inventories().end(),[&](auto const& item){return item.type()==message::inventory::type_id::block && item.hash()==expected;}))break;
                            continue;
                        }
                        if(raw->heading.command()!=message::block::command)continue;
                        byte_reader reader(raw->payload);auto body=message::block::from_data(reader,peer->negotiated_version());
                        if(!body || reader.remaining_size() || raw->payload.size()>body_limit)throw peer_fault("invalid block body encoding");
                        if(chain::hash(body->header())!=expected)continue; // Late reply to an expired earlier request.
                        auto at=state.index.active_at(static_cast<int32_t>(height));
                        if(at==blockchain::header_index::null_index || state.index.get_hash(at)!=expected)co_return true; // Reorganization during download.
                        auto error=body->check_body();
                        if(error || !body->is_distinct_transaction_set()) {
                            ++state.rejected_bodies;state.content_error=error?error.message():"duplicate block transactions";
                            throw peer_fault("invalid block body: "+state.content_error);
                        }
                        sampled_block record;record.hash=expected;record.bytes=raw->payload.size();record.transactions=body->transactions().size();
                        auto const& coinbase=body->transactions().front();record.coinbase.resize(coinbase.serialized_size(true));byte_writer writer(record.coinbase);if(!coinbase.to_data(writer,true))throw peer_fault("coinbase serialization");
                        for(auto const& tx:body->transactions()) {
                            auto hash=chain::hash(tx);record.txids.push_back(hash);
                            if(state.pool.erase(encode_hash(hash)))++state.pool_revision;
                            state.requested_transactions.erase(encode_hash(hash));
                        }
                        record.proof=coinbase_proof(record.txids);
                        state.blocks[height]=std::move(record);++state.content_revision;
                        state.prune_content();state.save_content();co_return true;
                    }
                    if(remote.unavailable_bodies.size()>=content_window)remote.unavailable_bodies.clear();
                    remote.unavailable_bodies[encode_hash(expected)]=zulu_time();
                    co_return false;
                };
                if (peer->negotiated_version() >= message::send_headers::version_minimum) {
                    auto ec = co_await peer->send(message::send_headers{});
                    if (ec) throw std::runtime_error(ec.message());
                }
                hash_digest cursor{};
                unsigned repeated = 0;
                for (;;) {
                    if (state.stop_requested || !state.fatal_error.empty()) break;
                    // Rate limiting and the one-outstanding-request rule belong to the native session policy.
                    ws::request query{state.discovery_wss_only ? ws::wss : ws::all_transports, state.discovery_sequence++};
                    if (policy.begin_request(query, static_cast<uint32_t>(zulu_time()))) {
                        auto payload = ws::encode(query);
                        auto ec = co_await peer->send_raw(discovery_packet(state.transport.identifier, ws::request_command, std::move(*payload)));
                        if (ec) throw std::runtime_error(ec.message());
                    }
                    if (state.observe_transactions && uint64_t(zulu_time()) >= remote.last_pool_request + 60) {
                        remote.last_pool_request = zulu_time();
                        auto ec = co_await request_mempool(*peer);
                        if (ec) throw std::runtime_error(ec.message());
                    }
                    auto hashes = locator(state.index);
                    if (cursor != null_hash && cursor != hashes.front()) hashes.insert(hashes.begin(), cursor);
                    remote.status = "synchronizing"; state.update_status();
                    auto sent = co_await peer->send(message::get_headers(hashes, null_hash));
                    if (sent) throw std::runtime_error(sent.message());
                    bool received = false;
                    auto const response_deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(state.runtime_node.sync_timeout_seconds);
                    while (!received) {
                        // Bound the whole request, rather than counting unrelated messages
                        // or renewing its timeout for every ping/inventory announcement.
                        // Yield even when messages are already queued, preserving fairness
                        // and allowing cancellation and the deadline to run under traffic.
                        co_await ::asio::post(executor, ::asio::use_awaitable);
                        if (std::chrono::steady_clock::now() >= response_deadline)
                            throw std::runtime_error("header response timeout");
                        auto response = co_await receive_timed(incoming, response_deadline);
                        if (!response) throw std::runtime_error("header response timeout");
                        auto raw = std::move(*response);
                        if (co_await discovery(raw)) continue;
                        byte_reader reader(raw.payload);
                        if (raw.heading.command() == message::ping::command) {
                            auto ping = message::ping::from_data(reader, peer->negotiated_version());
                            if (!ping) throw peer_fault("invalid ping");
                            auto ec = co_await peer->send(message::pong(ping->nonce()));
                            if (ec) throw std::runtime_error(ec.message());
                            continue;
                        }
                        if (raw.heading.command() != message::headers::command) continue;
                        auto headers = message::headers::from_data(reader, peer->negotiated_version());
                        if (!headers || !headers->is_sequential()) throw peer_fault("invalid headers response");
                        // BIP130 announcements and getheaders replies share the same wire
                        // command. A tip announced during IBD may have an unknown parent;
                        // keep waiting for the outstanding locator response instead of
                        // blaming this peer for a missing historical prefix. The absolute
                        // request deadline still prevents an endless stream.
                        if (!headers->elements().empty() &&
                            state.index.find(headers->elements().front().previous_block_hash()) == blockchain::header_index::null_index) {
                            ++remote.announcements;
                            continue;
                        }
                        received = true;
                        if (headers->elements().empty()) {
                            remote.status = "caught_up"; remote.complete = true; retries = 0; state.update_status();
                            if (co_await download_one()) break;
                            if (!follow_tip) break;
                            co_await wait_for_tip(*peer, incoming, remote.refresh, std::chrono::seconds(state.runtime_node.block_latency_seconds), discovery, [&remote] { ++remote.announcements; });
                            break;
                        }
                        // Duplicate replies from concurrent sessions do not consume capacity.
                        size_t unknown = 0;
                        for (auto const& header : headers->elements())
                            if (state.index.find(domain::chain::hash(header)) == blockchain::header_index::null_index) ++unknown;
                        if (unknown > state.index.capacity() - state.index.size()) throw capacity_fault("header capacity exceeded");
                        // This call has no suspension point. All peers serialize validation and publication
                        // on the same Worker executor and share the native chainwork/reorg policy.
                        auto accepted = state.organizer.add_headers(headers->elements());
                        if (accepted.error && accepted.error != error::stale_chain) throw peer_fault(accepted.error.message());
                        if (accepted.reorg_candidate) {
                            state.organizer.adopt_tip(accepted.reorg_branch_head);
                            // Observations are not revalidated UTXO-backed mempool entries.
                            state.pool.clear();state.requested_transactions.clear();++state.pool_revision;
                        }
                        state.prune_content();
                        auto const next = domain::chain::hash(headers->elements().back());
                        if (next == cursor) { if (++repeated >= 3) throw peer_fault("peer made no header progress"); }
                        else repeated = 0;
                        cursor = next;
                        auto known = state.index.find(cursor);
                        if (known != blockchain::header_index::null_index) remote.height = state.index.get_height(known);
                        remote.complete = false;
                        state.persist_cache();
                        break;
                    }
                    if (!follow_tip && remote.complete) break;
                    co_await ::asio::post(executor, ::asio::use_awaitable);
                }
            } catch (...) { failure = std::current_exception(); }
            incoming.close(); peer->stop();
            co_await tasks.join();
            remote.connected = false; remote.agent.clear(); remote.session.reset();
            if (failure) std::rethrow_exception(failure);
            if (!follow_tip || state.stop_requested || !state.fatal_error.empty()) break;
        } catch (capacity_fault const& error) {
            state.fatal_error = error.what(); state.stop_peers();
        } catch (peer_fault const& error) {
            remote.error = error.what(); remote.quarantined = true;
        } catch (std::exception const& error) {
            remote.error = error.what();
        }
        remote.complete = false;
        remote.connected = false; remote.agent.clear();
        if (remote.session) remote.session->stop();
        remote.session.reset();
        if (state.stop_requested || !state.fatal_error.empty()) break;
        remote.status = remote.quarantined ? "rejected" : "disconnected";
        state.update_status();
        if (remote.quarantined || !follow_tip) break;
        remote.status = "retry";
        auto const attempt = retries++;
        auto delay = std::chrono::milliseconds(1000u * std::min(retry_maximum_seconds, retry_initial_seconds << std::min(attempt, 4u)) + (at * 137u + attempt * 53u) % retry_offset_range_ms);
        remote.retry = std::make_unique<browser::timer>(executor, delay);
        co_await remote.retry->async_wait(::asio::as_tuple(::asio::use_awaitable));
        remote.retry.reset();
    }
    if (state.stop_requested) { remote.connected = false; remote.agent.clear(); remote.status = "stopped"; }
    state.update_status();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_stop() const {
    state_->stop_requested = true;
    state_->stop_peers();
    co_return last_height();
}

::asio::awaitable<std::expected<uint64_t, std::error_code>> header_sync::request_refresh() const {
    if (state_->active && !state_->stop_requested)
        for (auto& peer : state_->peers) {
            if (peer->connected) peer->refresh.try_send(0u);
            if (peer->retry) peer->retry->cancel();
        }
    co_return last_height();
}

bool header_sync::reset() noexcept {
    if (state_->active) return false;
    try { state_->message = "idle"; }
    catch (...) { return false; }
    state_->stop_requested = false;
    state_->fatal_error.clear();
    // Reconnection does not reset quarantine. Create a new actor for explicit Start.
    return true;
}

bool header_sync::add_peer(std::string const& url) {
    if (state_->active || state_->peers.size() >= state_->runtime_node.sync_peers || !valid_peer_url(url)) return false;
    for (auto const& peer : state_->peers) if (peer->url == url) return false;
    state_->peers.push_back(std::make_unique<impl::remote>(url));
    return true;
}
bool header_sync::configure_discovery(bool enabled, bool allow_private, bool wss_only) {
    if (state_->active) return false;
    state_->discovery_enabled = enabled;
    state_->discovery_allow_private = allow_private;
    state_->discovery_wss_only = wss_only;
    auto base = effective_transport_settings(state_->network).user_agent;
    state_->transport.user_agent = enabled ? network::ws_discovery::advertise(base).value() : base;
    return true;
}
bool header_sync::configure_content(bool blocks, bool transactions) {
    if(state_->active)return false;
    state_->sample_blocks=blocks;state_->observe_transactions=transactions;
    state_->transport.relay_transactions=transactions;return true;
}
std::string header_sync::block_metadata(uint64_t height, bool include_transactions) const {
    state_->prune_content();auto it=state_->blocks.find(height);if(it==state_->blocks.end())return "null";
    auto const& b=it->second;settings_json result;
    result.put("height",height);result.put("hash",encode_hash(b.hash));result.put("bytes",b.bytes);result.put("transactions",b.transactions);
    result.put("verification",std::string("header-merkle-structure"));result.put("txids_retained",!b.txids.empty());
    if(include_transactions) {
        result.put("coinbase_hex",encode_base16(b.coinbase));
        result.raw("txids",settings_array(b.txids,[](auto const& h){return json_string(encode_hash(h));}));
        result.raw("coinbase_merkle_proof",settings_array(b.proof,[](auto const& h){return json_string(encode_hash(h));}));
    }
    byte_reader reader(b.coinbase);auto tx=chain::transaction::from_data(reader,true);
    if(tx){
        auto const& script=tx->inputs().front().script().bytes();
        // BIP34's first push encodes height, not a miner label.
        size_t start=!script.empty() && script[0]>=1 && script[0]<=5 && script.size()>script[0]?script[0]+1:0;
        std::string tag;for(size_t i=start;i<script.size();++i){auto ch=script[i];tag+=ch>=32&&ch<=126?char(ch):' ';}
        result.put("coinbase_tag",tag);
    }
    return result.finish();
}
namespace {
template<class State>
void tick_snapshot(settings_json& json, State const& state) {
    if constexpr (requires { state.block_interval(); state.tick(); state.parent_tick(); state.is_faster_blocks_active(); }) {
        json.put("block_interval_seconds", state.block_interval());
        json.put("tick", state.tick()); json.put("parent_tick", state.parent_tick());
        json.put("faster_blocks_active", state.is_faster_blocks_active());
    }
}
std::string compact_hex(uint32_t value) {
    std::ostringstream encoded; encoded << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return encoded.str();
}
}
std::string header_sync::chain_state_snapshot() const {
    auto const& index = state_->index;
    auto const height = state_->organizer.header_height();
    auto const at = index.active_at(height);
    if (at == blockchain::header_index::null_index || height == 0) return "null";
    auto const header = index.get_header(at);
    auto const hash = index.get_hash(at);
    blockchain::validate_header validator(state_->consensus, state_->network);
    auto result = validator.chain_state_at(header, hash, height, index.get_parent_index(at), index);
    if (!result) { settings_json error; error.put("error", result.error().message()); return error.finish(); }
    auto const& chain = **result;
    settings_json snapshot;
    snapshot.put("height", chain.height()); snapshot.put("hash", encode_hash(hash));
    snapshot.put("parent_hash", encode_hash(header.previous_block_hash()));
    snapshot.put("timestamp", header.timestamp()); snapshot.put("version", header.version());
    snapshot.put("bits", compact_hex(header.bits()));
    snapshot.put("minimum_version", chain.minimum_version());
    snapshot.put("median_time_past", chain.median_time_past());
    snapshot.put("work_required", compact_hex(chain.work_required()));
    snapshot.put("enabled_flags", uint64_t(chain.enabled_flags()));
    snapshot.put("is_under_checkpoint", chain.is_under_checkpoint());
    snapshot.put("checkpoint_conflict", chain.is_checkpoint_conflict(hash));
    snapshot.put("is_retarget_height", domain::chain::chain_state::is_retarget_height(height));
    snapshot.put("is_null", chain.is_null());
    tick_snapshot(snapshot, chain);
    settings_json asert;
    auto const anchor = chain.assert_anchor_block_info();
    asert.put("anchor_height", anchor.height); asert.put("ancestor_timestamp", anchor.ancestor_timestamp);
    asert.put("anchor_bits", compact_hex(anchor.bits)); asert.put("half_life_seconds", chain.asert_half_life());
    snapshot.raw("asert", asert.finish());
    settings_json upgrades;
    upgrades.put("pythagoras", chain.is_pythagoras_enabled()); upgrades.put("euclid", chain.is_euclid_enabled());
    upgrades.put("pisano", chain.is_pisano_enabled()); upgrades.put("mersenne", chain.is_mersenne_enabled());
    upgrades.put("fermat", chain.is_fermat_enabled()); upgrades.put("euler", chain.is_euler_enabled());
    upgrades.put("gauss", chain.is_gauss_enabled()); upgrades.put("descartes", chain.is_descartes_enabled());
    upgrades.put("lobachevski", chain.is_lobachevski_enabled()); upgrades.put("galois", chain.is_galois_enabled());
    upgrades.put("leibniz", chain.is_leibniz_enabled()); upgrades.put("cantor", chain.is_cantor_enabled());
    snapshot.raw("upgrades", upgrades.finish());
    // Header-only chain_state construction seeds ABLA from defaults. Those are
    // not the active branch's body-derived ABLA state and must not be presented as such.
    settings_json abla; abla.put("available", false);
    abla.put("reason", std::string("Requires the block-size history of the active chain"));
    snapshot.raw("abla", abla.finish());
    return snapshot.finish();
}

std::string header_sync::content_state() const {
    state_->prune_content();state_->prune_pool();settings_json result;size_t bytes=0,pool_bytes=0;
    for(auto const& [height,b]:state_->blocks)bytes+=b.memory();for(auto const& [hash,tx]:state_->pool)pool_bytes+=tx.wire.size();
    result.put("blocks_enabled",state_->sample_blocks);result.put("transactions_enabled",state_->observe_transactions);
    result.put("revision",state_->content_revision);result.put("pool_revision",state_->pool_revision);result.put("cached_blocks",state_->blocks.size());
    result.put("cache_bytes",bytes);result.put("inflight_blocks",state_->requested_blocks.size());result.put("rejected_bodies",state_->rejected_bodies);result.put("error",state_->content_error);
    result.put("transaction_announcements",state_->transaction_announcements);
    result.put("transactions_received",state_->transactions_received);
    result.put("pending_transactions",state_->requested_transactions.size());
    result.put("observed_transactions",state_->pool.size());result.put("observed_bytes",pool_bytes);
    return result.finish();
}
std::string header_sync::observed_transactions() const {
    state_->prune_pool();std::string result="[";bool first=true;
    for(auto const& [hash,tx]:state_->pool){if(!std::exchange(first,false))result+=',';settings_json item;item.put("txid",hash);item.put("bytes",tx.wire.size());item.put("received_at",tx.received);result+=item.finish();}
    return result+']';
}
std::string header_sync::peer_source(size_t at) const { return at < peer_count() ? state_->peers[at]->source : ""; }
uint64_t header_sync::peer_announcements(size_t at) const { return at < peer_count() ? state_->peers[at]->announcements : 0; }
size_t header_sync::peer_count() const { return state_->peers.size(); }
std::string header_sync::peer_endpoint(size_t at) const { return at < peer_count() ? state_->peers[at]->url : ""; }
std::string header_sync::peer_state(size_t at) const { return at < peer_count() ? state_->peers[at]->status : ""; }
std::string header_sync::peer_agent(size_t at) const { return at < peer_count() ? state_->peers[at]->agent : ""; }
std::string header_sync::peer_error(size_t at) const { return at < peer_count() ? state_->peers[at]->error : ""; }
uint64_t header_sync::peer_height(size_t at) const { return at < peer_count() ? state_->peers[at]->height : 0; }
bool header_sync::peer_connected(size_t at) const { return at < peer_count() && state_->peers[at]->connected; }

std::string header_sync::runtime_settings() const {
    auto const& state = *state_;
    settings_json root, node, network, chain, sync, inactive;
    auto const& n = state.runtime_node;
    node.put("sync_peers", n.sync_peers);
    node.put("sync_timeout_seconds", n.sync_timeout_seconds);
    node.put("block_latency_seconds", n.block_latency_seconds);
    node.put("refresh_transactions", n.refresh_transactions);
    node.put("compact_blocks_high_bandwidth", n.compact_blocks_high_bandwidth);
    node.put("ds_proofs_enabled", n.ds_proofs_enabled);
    root.raw("node", node.finish());
    auto const& t = state.transport;
    network.put("threads", t.threads);
    network.put("protocol_maximum", t.protocol_maximum);
    network.put("protocol_minimum", t.protocol_minimum);
    network.put("services", t.services);
    network.put("invalid_services", t.invalid_services);
    network.put("relay_transactions", t.relay_transactions);
    network.put("validate_checksum", t.validate_checksum);
    network.put("identifier", t.identifier);
    network.put("inbound_port", t.inbound_port);
    network.put("inbound_connections", t.inbound_connections);
    network.put("outbound_connections", t.outbound_connections);
    network.put("manual_attempt_limit", t.manual_attempt_limit);
    network.put("connect_batch_size", t.connect_batch_size);
    network.put("connect_timeout_seconds", t.connect_timeout_seconds);
    network.put("channel_handshake_seconds", t.channel_handshake_seconds);
    network.put("channel_heartbeat_minutes", t.channel_heartbeat_minutes);
    network.put("channel_inactivity_minutes", t.channel_inactivity_minutes);
    network.put("channel_expiration_minutes", t.channel_expiration_minutes);
    network.put("channel_germination_seconds", t.channel_germination_seconds);
    network.put("host_pool_capacity", t.host_pool_capacity);
    network.put("rotation_size", t.rotation_size);
    network.put("minimum_free_space", t.minimum_free_space);
    network.put("maximum_archive_size", t.maximum_archive_size);
    network.put("maximum_archive_files", t.maximum_archive_files);
    network.put("verbose", t.verbose);
    network.put("use_ipv6", t.use_ipv6);
    network.put("user_agent", t.user_agent);
    network.put("browser_maximum_payload", t.browser_maximum_payload);
    network.put("whitelist_force_relay", t.whitelist_force_relay);
    network.put("whitelist_relay", t.whitelist_relay);
    network.put("hosts_file", t.hosts_file.string());
    network.put("banlist_file", t.banlist_file.string());
    network.put("peers_file", t.peers_file.string());
    network.put("debug_file", t.debug_file.string());
    network.put("error_file", t.error_file.string());
    network.put("archive_directory", t.archive_directory.string());
    network.put("self", t.self.to_string());
    network.put("statistics_server", t.statistics_server.to_string());
    network.raw("blacklist", settings_array(t.blacklist, [](auto const& value) { return json_string(value.to_string()); }));
    network.raw("peers", settings_array(t.peers, [](auto const& value) { return json_string(value.to_string()); }));
    network.raw("seeds", settings_array(t.seeds, [](auto const& value) { return json_string(value.to_string()); }));
    network.raw("user_agent_blacklist", settings_array(t.user_agent_blacklist, [](auto const& value) { return json_string(value); }));
    network.put("whitelist_count", t.whitelist.size());
    network.put("whitebind_count", t.whitebind.size());
    root.raw("network", network.finish());
    auto const& c = state.consensus;
    chain.put("cores", c.cores);
    chain.put("priority", c.priority);
    chain.put("byte_fee_satoshis", c.byte_fee_satoshis);
    chain.put("sigop_fee_satoshis", c.sigop_fee_satoshis);
    chain.put("minimum_output_satoshis", c.minimum_output_satoshis);
    chain.put("accept_non_standard_transactions", c.accept_non_standard_transactions);
    chain.put("notify_limit_hours", c.notify_limit_hours);
    chain.put("reorganization_limit", c.reorganization_limit);
    chain.put("max_reorg_depth", c.max_reorg_depth);
    chain.put("finalization_delay_seconds", c.finalization_delay_seconds);
    chain.put("gbt_template_refresh_seconds", c.gbt_template_refresh_seconds);
    chain.put("max_checkpoint_height", c.max_checkpoint_height);
    chain.put("fix_checkpoints", c.fix_checkpoints);
    chain.put("allow_collisions", c.allow_collisions);
    chain.put("easy_blocks", c.easy_blocks);
    chain.put("retarget", c.retarget);
    chain.put("bip16", c.bip16);
    chain.put("bip30", c.bip30);
    chain.put("bip34", c.bip34);
    chain.put("bip66", c.bip66);
    chain.put("bip65", c.bip65);
    chain.put("bip90", c.bip90);
    chain.put("bip68", c.bip68);
    chain.put("bip112", c.bip112);
    chain.put("bip113", c.bip113);
    chain.raw("checkpoints", settings_array(c.checkpoints, [](auto const& value) { return json_string(value.to_string()); }));
    chain.raw("checkpoints_sorted", settings_array(c.checkpoints_sorted, [](auto const& value) { return json_string(value.to_string()); }));
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
    chain.put("default_consensus_block_size", c.default_consensus_block_size);
    settings_json abla, standardness;
    abla.put("epsilon0", c.abla_config.epsilon0);
    abla.put("beta0", c.abla_config.beta0);
    abla.put("n0", c.abla_config.n0);
    abla.put("gamma_reciprocal", c.abla_config.gamma_reciprocal);
    abla.put("zeta_xB7", c.abla_config.zeta_xB7);
    abla.put("theta_reciprocal", c.abla_config.theta_reciprocal);
    abla.put("delta", c.abla_config.delta);
    abla.put("epsilon_max", c.abla_config.epsilon_max);
    abla.put("beta_max", c.abla_config.beta_max);
    standardness.put("maximum_transaction_size", c.standardness.maximum_transaction_size);
    standardness.put("maximum_scriptsig_size", c.standardness.maximum_scriptsig_size);
    standardness.put("maximum_output_script_size", c.standardness.maximum_output_script_size);
    standardness.put("minimum_version", c.standardness.minimum_version);
    standardness.put("maximum_version", c.standardness.maximum_version);
    standardness.put("minimum_output_value", c.standardness.minimum_output_value);
    standardness.put("maximum_datacarrier_bytes", c.standardness.maximum_datacarrier_bytes);
    standardness.put("require_push_only", c.standardness.require_push_only);
    chain.raw("abla_config", abla.finish());
    chain.raw("standardness", standardness.finish());
#else
    chain.put("bip141", c.bip141);
    chain.put("bip143", c.bip143);
    chain.put("bip147", c.bip147);
#endif
    root.raw("chain", chain.finish());
    sync.put("block_cache_directory", "/kth-block-cache/" + std::string(domain::config::name(state.network)));
    sync.put("download_blocks", state.sample_blocks);
    sync.put("observe_transactions", state.observe_transactions);
    sync.put("recent_block_window", content_window);
    sync.put("content_cache_bytes", content_budget);
    sync.put("maximum_block_payload_bytes", body_limit);
    sync.put("maximum_inflight_blocks", 2);
    sync.put("body_timeout_seconds", 5);
    sync.put("observed_inventory_queue_per_peer", pool_limit);
    sync.put("observed_transaction_limit", pool_limit);
    sync.put("observed_transaction_bytes", pool_budget);
    sync.put("observed_transaction_expiry_seconds", 1200);
    sync.put("network", std::string(domain::config::name(state.network)));
    sync.put("index_capacity", state.index.capacity());
    sync.put("discovery_enabled", state.discovery_enabled);
    sync.put("discovery_allow_private", state.discovery_allow_private);
    sync.put("discovery_wss_only", state.discovery_wss_only);
    sync.put("connect_timeout_seconds", connect_timeout_seconds);
    sync.put("retry_initial_seconds", retry_initial_seconds);
    sync.put("retry_maximum_seconds", retry_maximum_seconds);
    sync.put("retry_offset_range_ms", retry_offset_range_ms);
    sync.put("cache_directory", "/kth-header-cache/" + std::string(domain::config::name(state.network)));
    sync.raw("peer_endpoints", settings_array(state.peers, [](auto const& peer) { return json_string(peer->url); }));
    root.raw("header_sync", sync.finish());
    inactive.raw("node", "[\"refresh_transactions\",\"compact_blocks_high_bandwidth\",\"ds_proofs_enabled\"]");
    inactive.raw("chain", "[\"cores\",\"priority\",\"byte_fee_satoshis\",\"sigop_fee_satoshis\",\"minimum_output_satoshis\",\"accept_non_standard_transactions\",\"notify_limit_hours\",\"gbt_template_refresh_seconds\",\"default_consensus_block_size\",\"abla_config\",\"standardness\"]");
    inactive.raw("network", "[\"threads\",\"inbound_port\",\"inbound_connections\",\"outbound_connections\",\"manual_attempt_limit\",\"connect_batch_size\",\"connect_timeout_seconds\",\"channel_heartbeat_minutes\",\"channel_germination_seconds\",\"host_pool_capacity\",\"hosts_file\",\"banlist_file\",\"peers_file\",\"self\",\"blacklist\",\"peers\",\"seeds\",\"debug_file\",\"error_file\",\"archive_directory\",\"rotation_size\",\"minimum_free_space\",\"maximum_archive_size\",\"maximum_archive_files\",\"statistics_server\",\"verbose\",\"use_ipv6\",\"whitelist_force_relay\",\"whitelist_relay\",\"whitelist_count\",\"whitebind_count\"]");
    root.raw("inactive_fields", inactive.finish());
    root.raw("uninstantiated_sections", "[\"database\",\"rpc\",\"wallet\"]");
    return root.finish();
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
std::string header_sync::peer_user_agent() const {
    for (auto const& peer : state_->peers) if (peer->connected) return peer->agent;
    return {};
}
size_t header_sync::index_memory_bytes() const { return state_->index.memory_usage(); }
} // namespace kth::node
#endif
