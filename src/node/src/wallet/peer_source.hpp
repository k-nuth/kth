// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_PEER_SOURCE_HPP
#define KTH_NODE_WALLET_PEER_SOURCE_HPP

// Wallet support answered from peers, by concern: peer_source.cpp (life,
// status, the loop and the answers), peer_links.cpp (connections, requests,
// what peers send unasked), peer_filters.cpp (headers, filter headers and
// filters, disputes), peer_blocks.cpp (blocks, their checks and their cover)
// and peer_mempool.cpp (the mempool copy, double-spend proofs, broadcast).

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#if ! defined(KTH_BROWSER_NODE)
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#endif

#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#if defined(KTH_BROWSER_NODE)
#include <kth/node/detail/worker_runtime.hpp>
#endif
#include <kth/network/peer_session.hpp>
#include <kth/node/wallet/detail/service_impl.hpp>
#include <kth/node/wallet/filter_store.hpp>
#include <kth/node/wallet/scan.hpp>

namespace kth::node::wallet::detail {

namespace message = domain::message;
using block_ptr = std::shared_ptr<domain::chain::block const>;
using type_id = message::inventory_vector::type_id;

constexpr uint8_t script_type = message::script_compact_filter;
constexpr uint8_t prefix_type = message::input_prefix_compact_filter;
constexpr uint32_t checkpoint_interval = uint32_t(message::compact_filter_checkpoint_interval);
constexpr uint32_t headers_per_request = uint32_t(message::max_compact_filter_headers);
constexpr uint32_t filters_per_request = uint32_t(message::max_compact_filters_per_request);
constexpr size_t headers_per_answer = 2000;
constexpr size_t header_size = 80;
// Blocks at the tip downloaded one by one while following it; further
// behind, the mempool copy starts over.
constexpr uint32_t live_window = 12;
constexpr size_t recent_blocks = 24;
constexpr uint64_t default_relay_fee = 1000;
// A fee filter above this (10 satoshis a byte) is not believed.
constexpr uint64_t maximum_relay_fee = 10'000;
constexpr auto retry_pause = std::chrono::seconds(5);
constexpr auto follow_pause = std::chrono::seconds(30);
constexpr auto connect_timeout = std::chrono::seconds(10);
// Decoys are drawn from the block's bucket of this many heights.
constexpr uint32_t decoy_bucket = 144;
// Confirmed transactions found by hash alone, kept so asking again
// downloads nothing.
constexpr size_t located_limit = 10'000;
constexpr size_t dsproof_limit = 1'000;
constexpr size_t refused_limit = 32;
// Filters held from one answer before they are appended.
constexpr uint64_t filter_answer_bytes = uint64_t(64) << 20;
// Headers a store can grow by after it is opened.
constexpr size_t header_headroom = 1'000'000;
// Rounds in a row a peer may leave a checkpoint request unanswered before
// it is taken for one that does not serve the type: a peer stays silent for
// a block it does not have yet (BIP157), as one a block behind does.
constexpr size_t silences_limit = 3;

// What a waiting request does with a message: not its own, part of its
// answer with more to come, or the end of its answer.
enum class step { skip, more, done };
using matcher = std::function<step(network::raw_message const&)>;

struct waiter {
    explicit waiter(matcher match_message) : match(std::move(match_message)) {}
    matcher match;
    std::vector<network::raw_message> taken;
    kth::select_channel<int> signal{1};
};

// A connection to a peer. Its one reader hands each message to the request
// that matches it, or handles it as one nobody asked for.
struct peer_link {
    uint64_t id = 0;
    std::string endpoint;
    network::peer_session::ptr session;
    std::list<waiter*> waiters;
    uint64_t services = 0;
    bool isolated = false;
    bool closed = false;
    // The filter types it did not answer for.
    std::set<uint8_t> no_filters;
    // Rounds in a row it left a checkpoint request of a type unanswered.
    std::map<uint8_t, size_t> silences;
};

// A confirmed transaction found by hash, with its Merkle branch.
struct located_tx {
    domain::chain::transaction tx;
    confirmation confirmed;
    hash_list branch;
};

blockchain::settings validation_settings(domain::config::network network);
size_t default_capacity(domain::config::network network);
hash_list locator(blockchain::header_index const& index);

struct peer_impl final : service::impl {
    peer_settings settings;
    network::settings transport;
    std::filesystem::path folder;
    size_t threads;
    // Per wallet store, kept beside its filters: the decoys of a block are
    // derived from it, the same in every session.
    data_chunk decoy_seed;

    // Headers, validated as a kth node validates them.
    blockchain::header_index index;
    blockchain::settings consensus;
    blockchain::header_organizer organizer;
    std::filesystem::path header_file;
    int32_t written_height = -1;
    hash_digest written_tip{};

    std::optional<filter_store> scripts;
    std::optional<filter_store> prefixes;

    // Peers.
    std::vector<std::string> endpoints;
    size_t next_endpoint = 0;
    std::vector<std::shared_ptr<peer_link>> links;
    uint64_t next_link_id = 1;
    size_t next_link_at = 0;
    // The filter server a round asks first, moved on every round.
    size_t filter_turn = 0;
    std::set<std::string> refused_endpoints;
    std::deque<std::string> refused;
    std::mt19937_64 random{std::random_device{}()};

    // Watched targets and their histories.
    std::map<hash_digest, target_state> targets;
    uint64_t generation = 0;

    // The mempool copy, from relay.
    mempool_copy pool;
    // Transactions asked for, and the link asked.
    std::map<hash_digest, uint64_t> requested;
    std::map<uint64_t, uint64_t> fee_filters;
    std::map<domain::chain::point, uint64_t> recent_outputs;
    std::deque<std::vector<domain::chain::point>> recent_created;
    std::optional<uint32_t> live_tip;
    // Transactions being broadcast, and what a peer rejected them for.
    std::set<hash_digest> pending_broadcasts;
    std::map<hash_digest, std::string> rejections;

    // Downloaded blocks.
    std::map<hash_digest, block_ptr> cache;
    std::deque<std::pair<hash_digest, size_t>> cache_order;
    size_t cache_bytes = 0;
    // Blocks being downloaded, and who waits for each.
    std::map<hash_digest, std::vector<std::shared_ptr<kth::select_channel<int>>>> inflight;
    // Transactions found by hash alone.
    std::map<hash_digest, located_tx> located;
    std::deque<hash_digest> located_order;
    header_tree tree;

    std::optional<bool> transaction_keys;
    uint32_t transaction_keys_height = 0;
    bool synced = false;
    change_signal changes;
    std::optional<uint32_t> announced_tip;
    // Double-spend proofs from relay, by their hash, oldest first out.
    std::map<hash_digest, dsproof_info> dsproofs;
    std::deque<hash_digest> dsproof_order;
    // Proofs asked for, and the link asked.
    std::map<hash_digest, uint64_t> requested_dsproofs;
    // Which link served the filters of each recent appended batch: a filter
    // that does not fit its block is traced to the peer that served it.
    std::deque<std::tuple<uint8_t, uint32_t, uint32_t, uint64_t>> served_by;
    std::string last_error;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t blocks_downloaded = 0;
    std::ofstream sent_log;

    mutable std::mutex status_mutex;
    status snapshot;
    std::atomic<bool> stopping{false};

    // Last: destroyed first, so no operation outlives what it uses.
#if defined(KTH_BROWSER_NODE)
    // The Worker's runtime: everything runs on the Worker's one thread, and
    // the scans inline.
    std::shared_ptr<browser::runtime> runtime = node::detail::worker_runtime();
#else
    std::unique_ptr<::asio::io_context> io;
    std::optional<::asio::executor_work_guard<::asio::io_context::executor_type>> work;
#endif
    kth::select_channel<int> wake{1};
    kth::select_channel<int> history_lock{1};
    std::unique_ptr<kth::task_group> tasks;
#if ! defined(KTH_BROWSER_NODE)
    std::unique_ptr<::asio::thread_pool> scan_pool;
    std::promise<void> finished;
    std::future<void> done = finished.get_future();
    std::thread thread;
#endif

    peer_impl(peer_settings given, size_t capacity, data_chunk seed);
    ~peer_impl() override;

    // ------------------------------------------------------------------------
    // Life and status (peer_source.cpp)

    void start_thread();
    ::asio::any_io_executor executor() const override;
    void ring() { (void)wake.try_send(0); }
    void shutdown();
    void request_stop() override;
    bool stopped() const override { return stopping; }
    void update_status();
    status current() const override;

    // ------------------------------------------------------------------------
    // Links (peer_links.cpp)

    void log_sent(std::string_view command, data_chunk const& packet);

    template <typename Message>
    data_chunk packet_of(peer_link const& link, Message const& item) const {
        return message::serialize(link.session->negotiated_version(), item, transport.identifier);
    }

    template <typename Message>
    ::asio::awaitable<std::error_code> send(peer_link& link, Message const& item) {
        if (link.closed) co_return error::channel_stopped;
        auto packet = packet_of(link, item);
        log_sent(Message::command, packet);
        co_return co_await link.session->send_raw(std::move(packet));
    }

    // Sends a request and collects the messages its matcher takes, until it
    // says done, the peer closes, or the request times out.
    template <typename Message>
    ::asio::awaitable<std::expected<std::vector<network::raw_message>, std::error_code>>
    request(std::shared_ptr<peer_link> link, Message const& item, matcher match) {
        if ( ! link || link->closed) co_return std::unexpected(error::channel_stopped);
        waiter wait(std::move(match));
        link->waiters.push_back(&wait);
        struct leave {
            peer_link& link;
            waiter* entry;
            ~leave() { link.waiters.remove(entry); }
        } guard{*link, &wait};
        auto packet = packet_of(*link, item);
        log_sent(Message::command, packet);
        auto const ec = co_await link->session->send_raw(std::move(packet));
        if (ec) co_return std::unexpected(ec);
        auto const deadline = std::chrono::steady_clock::now() + settings.request_timeout;
        auto executor = co_await ::asio::this_coro::executor;
        auto completion = co_await ::asio::experimental::make_select(
                ::asio::experimental::receive(wait.signal), ::asio::experimental::after(deadline))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [failed, selected] = completion;
        if (failed) co_return std::unexpected(error::channel_stopped);
        if (selected.index == 1) co_return std::unexpected(error::channel_timeout);
        co_return std::move(wait.taken);
    }

    ::asio::awaitable<void> pump(std::shared_ptr<peer_link> link);
    ::asio::awaitable<void> unsolicited(std::shared_ptr<peer_link> link, network::raw_message const& raw);
    uint64_t next_nonce();
    ::asio::awaitable<std::shared_ptr<peer_link>> connect(std::string endpoint, bool isolated);
    ::asio::awaitable<std::shared_ptr<peer_link>> connect_isolated();
    ::asio::awaitable<void> keep_peers();
    std::shared_ptr<peer_link> next_link();
    std::vector<std::shared_ptr<peer_link>> filter_servers(uint8_t type) const;
    // A peer that served what does not fit the chain: it serves this service
    // nothing again.
    void refuse(peer_link& link, std::string const& why);

    // ------------------------------------------------------------------------
    // Headers and filters (peer_filters.cpp)

    void restore();
    void persist();
    uint32_t header_tip() const;
    std::optional<hash_digest> hash_at(uint32_t height) const;
    std::optional<domain::chain::header> header(uint32_t height) const;
    ::asio::awaitable<std::error_code> sync_headers(std::shared_ptr<peer_link> link);
    void on_reorg(uint32_t fork);

    using header_answer = std::expected<message::compact_filter_headers, std::error_code>;
    ::asio::awaitable<header_answer> filter_hashes(std::shared_ptr<peer_link> link, uint8_t type,
        uint32_t start, hash_digest const& stop);
    ::asio::awaitable<std::expected<hash_digest, std::error_code>> header_before(std::shared_ptr<peer_link> link,
        uint8_t type, uint32_t before, std::vector<hash_digest> const& checkpoints);
    ::asio::awaitable<std::expected<data_chunk, std::error_code>> filter_of(std::shared_ptr<peer_link> link,
        uint8_t type, uint32_t height);
    ::asio::awaitable<std::expected<std::vector<hash_digest>, std::error_code>> agreed_checkpoints(uint8_t type,
        hash_digest const& tip_hash, uint32_t tip);
    // Which of the filters served for the block at `height` fit it, read
    // from the block itself; none when the block cannot be had.
    ::asio::awaitable<std::optional<std::vector<bool>>> judge(uint8_t type, uint32_t height, std::vector<data_chunk> const& filters);
    // Peers whose filter headers differ at `height`: each one's filter is
    // judged against the block, and those that do not fit are refused;
    // when all fit or none does, they cannot be told apart and all are.
    ::asio::awaitable<void> settle(uint8_t type, uint32_t height, std::vector<std::shared_ptr<peer_link>> parties);
    // The stored filter headers above the last checkpoint against another
    // peer's: a difference is settled by the block, and the store is cut
    // where it held a filter that does not fit.
    ::asio::awaitable<void> cross_check(uint8_t type, uint64_t served);
    // Drops the filters from `height` on (and what targets read from them),
    // and refuses the peer that served the one at `height`.
    void drop_filters(uint32_t height, uint8_t type, std::string const& why);
    ::asio::awaitable<std::error_code> sync_filters(uint8_t type);

    // ------------------------------------------------------------------------
    // Blocks (peer_blocks.cpp)

    void cache_insert(hash_digest const& hash, block_ptr const& blk, size_t bytes);
    void check_filters(uint32_t height, domain::chain::block const& blk, hash_digest const& hash);
    // A block of the header chain, checked against its header; and, unless
    // `check` is false, its stored filters against it.
    ::asio::awaitable<std::expected<block_ptr, std::error_code>> fetch_block(uint32_t height, bool check = true);
    std::vector<uint32_t> decoys_for(uint32_t height) const;

    // Downloads the blocks at `heights`, a few at a time, and hands each to
    // `use` in height order; `use` returns an error to stop.
    template <typename Use>
    ::asio::awaitable<std::error_code> each_block(std::vector<uint32_t> const& heights, Use use) {
        auto const width = std::max<size_t>(1, settings.parallel_downloads);
        auto executor = co_await ::asio::this_coro::executor;
        for (size_t at = 0; at < heights.size(); at += width) {
            auto const count = std::min(width, heights.size() - at);
            std::vector<std::expected<block_ptr, std::error_code>> fetched(count, std::unexpected(error::not_found));
            {
                kth::task_group downloads("wallet block downloads", executor);
                for (size_t i = 0; i < count; ++i) {
                    downloads.spawn("wallet block download", [this, &fetched, i, height = heights[at + i]]() -> ::asio::awaitable<void> {
                        fetched[i] = co_await fetch_block(height);
                    });
                }
                co_await downloads.join();
            }
            for (size_t i = 0; i < count; ++i) {
                if ( ! fetched[i]) co_return fetched[i].error();
                if (auto const ec = use(heights[at + i], *fetched[i])) co_return ec;
            }
        }
        co_return error::success;
    }

    // The blocks a wallet needs, each with its decoys, in an order that does
    // not single it out; `use` sees the needed ones, in height order.
    template <typename Use>
    ::asio::awaitable<std::error_code> each_covered(std::vector<uint32_t> const& needed, Use use) {
        std::set<uint32_t> all(needed.begin(), needed.end());
        for (auto const height : needed) {
            for (auto const decoy : decoys_for(height)) all.insert(decoy);
        }
        std::vector<uint32_t> heights(all.begin(), all.end());
        std::set<uint32_t> const wanted(needed.begin(), needed.end());
        co_return co_await each_block(heights, [&](uint32_t height, block_ptr const& blk) -> std::error_code {
            if ( ! wanted.contains(height)) return {};
            return use(height, blk);
        });
    }

    ::asio::awaitable<std::error_code> follow_tip();

    // ------------------------------------------------------------------------
    // The mempool copy, proofs and broadcast (peer_mempool.cpp)

    std::optional<uint64_t> known_value(domain::chain::point const& outpoint) const;
    void on_mempool_tx(domain::chain::transaction tx);
    void on_dsproof(message::double_spend_proof const& proof, data_chunk const& raw);
    void resolve_dsproofs(bool drop_unknown);
    ::asio::awaitable<std::expected<hash_digest, std::error_code>> broadcast(domain::chain::transaction tx) override;
    fee_estimate fees() const override;
    mempool_summary mempool() const override;
    std::vector<rpa_match> rpa_mempool(uint16_t prefix, uint8_t bits) const override;
    ::asio::awaitable<std::optional<dsproof_info>> dsproof(hash_digest id) override;
    ::asio::awaitable<std::vector<hash_digest>> dsproof_transactions() override;

    // ------------------------------------------------------------------------
    // The loop and the answers (peer_source.cpp)

    ::asio::awaitable<std::expected<scan_hits, std::error_code>> scan_keys(std::vector<scan_key> keys, uint32_t from, uint32_t to);
    ::asio::awaitable<std::error_code> catch_up();
    ::asio::awaitable<std::error_code> run() override;
    ::asio::awaitable<void> rest(std::chrono::steady_clock::duration duration);
    target_state& watch_one(target const& watched, uint32_t birthday);
    ::asio::awaitable<std::error_code> watch(std::vector<target> watched, uint32_t birthday) override;
    bool unwatch(target const& watched) override;
    ::asio::awaitable<std::expected<target_state const*, std::error_code>> ready(target const& watched);
    ::asio::awaitable<std::expected<target_history, std::error_code>> history(target watched) override;
    ::asio::awaitable<std::expected<balance, std::error_code>> get_balance(target watched, token_filter tokens) override;
    ::asio::awaitable<std::expected<std::vector<utxo>, std::error_code>> unspent(target watched, token_filter tokens) override;
    void keep_located(located_tx found);
    ::asio::awaitable<std::expected<located_tx, std::error_code>> locate(hash_digest const& txid, std::optional<uint32_t> hint);
    ::asio::awaitable<std::expected<transaction_info, std::error_code>> transaction(hash_digest txid, std::optional<uint32_t> hint) override;
    ::asio::awaitable<std::expected<merkle_proof, std::error_code>> merkle(hash_digest txid, std::optional<uint32_t> hint) override;
    ::asio::awaitable<std::expected<std::pair<domain::chain::transaction, merkle_proof>, std::error_code>>
        transaction_at(uint32_t height, uint32_t position) override;
    ::asio::awaitable<std::expected<std::vector<rpa_match>, std::error_code>>
        rpa_history(uint16_t prefix, uint8_t bits, uint32_t from, std::optional<uint32_t> to) override;
    ::asio::awaitable<std::expected<std::pair<hash_list, hash_digest>, std::error_code>>
        header_branch(uint32_t height, uint32_t cp_height) override;
    ::asio::awaitable<std::expected<std::optional<utxo>, std::error_code>> output(domain::chain::point outpoint) override;
    ::asio::awaitable<uint64_t> changed(uint64_t seen, std::chrono::steady_clock::time_point deadline) override;
    ::asio::awaitable<touched_keys> touched(uint64_t seen) override;
    ::asio::awaitable<std::optional<domain::chain::header>> header_at(uint32_t height) override;
};

} // namespace kth::node::wallet::detail

#endif // KTH_NODE_WALLET_PEER_SOURCE_HPP
