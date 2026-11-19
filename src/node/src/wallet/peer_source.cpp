// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers: headers through the header organizer,
// script filters with their checkpoints and filter headers, whole blocks
// where the filters match, and unconfirmed transactions from ordinary relay.
// Nothing sent names what the wallet watches, and which blocks are
// downloaded does not single it out: each over a connection of its own, each
// with decoys that are the same in every session, and every block of an RPA
// range. Nothing kept grows with the chain beyond the headers and filters.
// Here: its life, its status, the loop that keeps it current, the answers.

#include "peer_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include <kth/node/user_agent.hpp>
#include <kth/node/wallet/blocks.hpp>

namespace kth::node::wallet {

namespace detail {

using domain::chain::point;

blockchain::settings validation_settings(domain::config::network network) {
    blockchain::settings result(network);
    result.checkpoints.clear();
    result.checkpoints_sorted.clear();
    result.max_checkpoint_height = 0;
    return result;
}

size_t default_capacity(domain::config::network network) {
    return network == domain::config::network::regtest ? 200'000 : 2'000'000;
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

// ============================================================================
// Life and status
// ============================================================================

peer_impl::peer_impl(peer_settings given, size_t capacity, data_chunk seed)
    : settings(std::move(given))
    , transport(settings.network)
    , threads(settings.scan_threads != 0 ? settings.scan_threads : std::max<size_t>(1, std::thread::hardware_concurrency()))
    , decoy_seed(std::move(seed))
    , index(capacity)
    , consensus(validation_settings(settings.network))
    , organizer(index, consensus, settings.network)
    , pool(settings.mempool_bytes)
#if ! defined(KTH_BROWSER_NODE)
    , io(std::make_unique<::asio::io_context>(1))
#endif
{
    // It keeps headers and filters: it serves no blocks. It asks peers to
    // announce transactions, for the mempool copy. It says what the node
    // says it is: a user agent of its own would single it out.
    transport.services = 0;
    transport.relay_transactions = true;
    transport.validate_checksum = true;
    transport.user_agent = settings.user_agent.empty() ? get_user_agent({}) : settings.user_agent;
    transport.channel_handshake_seconds = 10;
    transport.channel_expiration_minutes = 365 * 24 * 60;
    endpoints = settings.peers;
    auto const genesis = genesis_of(settings.network);
    [[maybe_unused]] auto const added = index.add(genesis.hash(), genesis.header());
    organizer.sync_tip();
    organizer.start();
    (void)history_lock.try_send(0);
#if ! defined(KTH_BROWSER_NODE)
    work.emplace(io->get_executor());
    scan_pool = std::make_unique<::asio::thread_pool>(1);
#endif
    tasks = std::make_unique<kth::task_group>("wallet peers", executor());
}

peer_impl::~peer_impl() {
    stopping = true;
#if defined(KTH_BROWSER_NODE)
    // The Worker owns the thread: run() has returned before this (see
    // service.hpp), so nothing waits on what is destroyed here.
    shutdown();
#else
    // What still runs on the service ends once it is shut down; whatever a
    // caller still holds after ten seconds is cut off.
    ::asio::post(*io, [this] { shutdown(); });
    work.reset();
    if (thread.joinable() && done.wait_for(std::chrono::seconds(10)) != std::future_status::ready) io->stop();
    if (thread.joinable()) thread.join();
    scan_pool->join();
#endif
    links.clear();
#if ! defined(KTH_BROWSER_NODE)
    io.reset();
#endif
    organizer.stop();
    persist();
}

void peer_impl::start_thread() {
#if ! defined(KTH_BROWSER_NODE)
    thread = std::thread([this] {
        io->run();
        finished.set_value();
    });
#endif
}

::asio::any_io_executor peer_impl::executor() const {
#if defined(KTH_BROWSER_NODE)
    runtime->wake();
    return runtime->context().get_executor();
#else
    return io->get_executor();
#endif
}

void peer_impl::shutdown() {
    wake.close();
    history_lock.close();
    changes.close();
    for (auto const& link : links) {
        if (link->session) link->session->stop();
    }
}

void peer_impl::request_stop() {
    stopping = true;
    ::asio::post(executor(), [this] { shutdown(); });
}

void peer_impl::update_status() {
    status next;
    next.source = "peers";
    next.header_height = header_tip();
    next.tip = scripts ? scripts->tip() : std::nullopt;
    next.input_prefix_tip = prefixes ? prefixes->tip() : std::nullopt;
    next.peers = size_t(std::ranges::count_if(links, [](auto const& link) { return ! link->closed && ! link->isolated; }));
    next.synced = synced;
    next.transaction_keys = transaction_keys;
    next.bytes_received = bytes_received;
    next.bytes_sent = bytes_sent;
    next.blocks_downloaded = blocks_downloaded;
    next.filter_bytes = (scripts ? scripts->data_bytes() : 0) + (prefixes ? prefixes->data_bytes() : 0);
    next.error = last_error;
    next.refused.assign(refused.begin(), refused.end());
    // A new tip, or where the service stands (synced, its error, a refused
    // peer), wakes whoever waits on changes.
    auto moved = next.tip != announced_tip;
    if (moved) announced_tip = next.tip;
    {
        std::lock_guard<std::mutex> guard(status_mutex);
        moved = moved || next.synced != snapshot.synced || next.error != snapshot.error || next.refused != snapshot.refused;
        snapshot = std::move(next);
    }
    if (moved) changes.bump();
}

status peer_impl::current() const {
    std::lock_guard<std::mutex> guard(status_mutex);
    return snapshot;
}

// ============================================================================
// The loop that keeps everything current
// ============================================================================

::asio::awaitable<std::expected<scan_hits, std::error_code>> peer_impl::scan_keys(std::vector<scan_key> keys, uint32_t from, uint32_t to) {
    if ( ! scripts) co_return scan_hits{};
    auto locations = scripts->locations(from, to + 1);
    auto path = scripts->data_path();
#if defined(KTH_BROWSER_NODE)
    // Inline: the Worker has one thread.
    co_return scan_script_filters(path, locations, keys, 1);
#else
    auto const workers = threads;
    co_return co_await ::asio::co_spawn(scan_pool->get_executor(),
        [path, locations = std::move(locations), keys = std::move(keys), workers]()
            -> ::asio::awaitable<std::expected<scan_hits, std::error_code>> {
            co_return scan_script_filters(path, locations, keys, workers);
        }, ::asio::use_awaitable);
#endif
}

// One scan for every watched target, then the blocks the filters point to,
// each with its decoys, in height order. With download_all_blocks, every
// block of the scanned range, a window at a time.
::asio::awaitable<std::error_code> peer_impl::catch_up() {
    if ( ! co_await acquire(history_lock)) co_return error::service_stopped;
    lock_release release{history_lock};
    for (size_t rounds = 0; rounds < 16 && ! stopping; ++rounds) {
        auto const generation_before = generation;
        if ( ! scripts || ! scripts->tip()) co_return error::success;
        auto const tip = *scripts->tip();
        std::vector<scan_key> keys;
        std::set<hash_digest> owners;
        uint32_t from = tip + 1;
        for (auto const& [key, state] : targets) {
            auto const next = std::max(state.next_height(), scripts->first());
            if (next > tip) continue;
            keys.push_back(scan_key{key, next});
            owners.insert(key);
            from = std::min(from, next);
        }
        if (keys.empty()) co_return error::success;
        auto const apply = [&](uint32_t height, block_ptr const& blk) -> std::error_code {
            if (generation != generation_before) return error::service_stopped;
            for (auto& [key, state] : targets) {
                if (owners.contains(key) && state.next_height() <= height) {
                    (void)state.apply_block(height, blk->hash(), blk->transactions());
                }
            }
            return {};
        };
        std::error_code applied;
        if (settings.download_all_blocks) {
            constexpr uint32_t window = 256;
            for (uint32_t start = from; start <= tip && ! applied; start += window) {
                std::vector<uint32_t> heights;
                for (auto height = start; height <= std::min(tip, start + window - 1); ++height) heights.push_back(height);
                applied = co_await each_block(heights, apply);
            }
        } else {
            auto const hits = co_await scan_keys(keys, from, tip);
            if ( ! hits) co_return hits.error();
            if (generation != generation_before) continue;
            std::vector<uint32_t> heights;
            for (auto const& [height, matched] : *hits) heights.push_back(height);
            applied = co_await each_covered(heights, apply);
        }
        if (generation != generation_before) continue;
        if (applied) co_return applied;
        for (auto const& key : owners) {
            auto const state = targets.find(key);
            if (state != targets.end()) state->second.set_synced(tip);
        }
        co_return error::success;
    }
    co_return error::success;
}

::asio::awaitable<std::error_code> peer_impl::run() {
    std::error_code result;
    while ( ! stopping) {
        co_await keep_peers();
        std::error_code ec;
        if ( ! next_link()) {
            ec = error::channel_stopped;
            last_error = "no peer connected";
        } else {
            // Headers from every connected peer: the tip does not wait for a
            // round that asks the one ahead. A round goes on when one answers.
            bool answered = false;
            std::error_code failed = error::channel_stopped;
            auto const open = links;
            for (auto const& link : open) {
                if (link->closed) continue;
                auto const one = co_await sync_headers(link);
                if ( ! one) {
                    answered = true;
                } else {
                    failed = one;
                }
            }
            if ( ! answered) ec = failed;
            if ( ! ec && scripts) ec = co_await sync_filters(script_type);
            if ( ! ec && prefixes) ec = co_await sync_filters(prefix_type);
            if ( ! ec) {
                synced = scripts && scripts->tip() == header_tip() && ( ! prefixes || prefixes->tip() == header_tip());
                if (synced) ec = co_await follow_tip();
            }
            if ( ! ec && ! targets.empty()) ec = co_await catch_up();
            persist();
        }
        if ( ! ec && ! synced && filter_servers(script_type).empty()) {
            last_error = "no connected peer serves script filters";
        } else if ( ! ec) {
            last_error.clear();
        }
        if (ec && ! stopping) {
            synced = false;
            if (last_error.empty()) last_error = ec.message();
            spdlog::debug("[wallet] Sync round: {}", ec.message());
        }
        result = ec;
        update_status();
        co_await rest(ec ? retry_pause : follow_pause);
    }
    shutdown();
    co_await tasks->join();
    update_status();
    co_return result;
}

// Until a peer announces something, a refused peer or a dispute asks for
// another round, or the pause ends.
::asio::awaitable<void> peer_impl::rest(std::chrono::steady_clock::duration duration) {
    auto executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::receive(wake), ::asio::experimental::after(std::chrono::steady_clock::now() + duration))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    (void)completion;
}

// ============================================================================
// Answers
// ============================================================================

target_state& peer_impl::watch_one(target const& watched, uint32_t birthday) {
    auto const key = watched.key();
    auto found = targets.find(key);
    if (found == targets.end()) {
        found = targets.emplace(key, target_state(watched, std::max(birthday, settings.birthday))).first;
    }
    return found->second;
}

::asio::awaitable<std::error_code> peer_impl::watch(std::vector<target> watched, uint32_t birthday) {
    for (auto const& one : watched) (void)watch_one(one, birthday);
    co_return co_await catch_up();
}

bool peer_impl::unwatch(target const& watched) {
    return targets.erase(watched.key()) != 0;
}

::asio::awaitable<std::expected<target_state const*, std::error_code>> peer_impl::ready(target const& watched) {
    (void)watch_one(watched, settings.birthday);
    auto const ec = co_await catch_up();
    update_status();
    if (ec) co_return std::unexpected(ec);
    auto const found = targets.find(watched.key());
    if (found == targets.end()) co_return std::unexpected(error::not_found);
    co_return &found->second;
}

::asio::awaitable<std::expected<target_history, std::error_code>> peer_impl::history(target watched) {
    auto const state = co_await ready(watched);
    if ( ! state) co_return std::unexpected(state.error());
    co_return target_history{(*state)->synced(), (*state)->history(), unconfirmed_of(**state, pool)};
}

::asio::awaitable<std::expected<balance, std::error_code>> peer_impl::get_balance(target watched, token_filter tokens) {
    auto const state = co_await ready(watched);
    if ( ! state) co_return std::unexpected(state.error());
    co_return balance_of(**state, pool, tokens);
}

::asio::awaitable<std::expected<std::vector<utxo>, std::error_code>> peer_impl::unspent(target watched, token_filter tokens) {
    auto const state = co_await ready(watched);
    if ( ! state) co_return std::unexpected(state.error());
    co_return unspent_of(**state, pool, tokens);
}

void peer_impl::keep_located(located_tx found) {
    auto const txid = found.tx.hash();
    if (located.contains(txid)) return;
    located.emplace(txid, std::move(found));
    located_order.push_back(txid);
    while (located_order.size() > located_limit) {
        located.erase(located_order.front());
        located_order.pop_front();
    }
}

// The confirmed transaction with this hash: one found before, the height
// hint, a watched history, then its transaction key (kind 4) in the script
// filters. Each block read comes with its decoys, and a transaction found
// once is kept: asking again, as a subscription does, downloads nothing.
::asio::awaitable<std::expected<located_tx, std::error_code>> peer_impl::locate(hash_digest const& txid, std::optional<uint32_t> hint) {
    if (auto const known = located.find(txid); known != located.end()) co_return known->second;
    std::optional<located_tx> found;
    auto const look_in = [&](uint32_t height, block_ptr const& blk) -> std::error_code {
        auto const& txs = blk->transactions();
        for (uint32_t at = 0; at < txs.size() && ! found; ++at) {
            if (txs[at].hash() != txid) continue;
            found = located_tx{txs[at], confirmation{height, blk->hash(), at}, blk->transaction_merkle_branch(at)};
        }
        return {};
    };
    std::vector<uint32_t> candidates;
    if (hint && *hint <= header_tip()) candidates.push_back(*hint);
    for (auto const& [key, state] : targets) {
        for (auto const& entry : state.history()) {
            if (entry.txid == txid) candidates.push_back(entry.height);
        }
    }
    if ( ! candidates.empty()) {
        std::ranges::sort(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        auto const ec = co_await each_covered(candidates, look_in);
        if (ec) co_return std::unexpected(ec);
    }
    if ( ! found) {
        if (transaction_keys && ! *transaction_keys) co_return std::unexpected(error::not_implemented);
        if ( ! scripts || ! scripts->tip()) co_return std::unexpected(error::not_found);
        std::vector<scan_key> keys{scan_key{domain::chain::transaction_key(txid), scripts->first()}};
        auto const hits = co_await scan_keys(std::move(keys), scripts->first(), *scripts->tip());
        if ( ! hits) co_return std::unexpected(hits.error());
        std::vector<uint32_t> heights;
        for (auto const& [height, matched] : *hits) heights.push_back(height);
        auto const ec = co_await each_covered(heights, look_in);
        if (ec) co_return std::unexpected(ec);
    }
    if ( ! found) co_return std::unexpected(error::not_found);
    keep_located(*found);
    co_return *found;
}

::asio::awaitable<std::expected<transaction_info, std::error_code>> peer_impl::transaction(hash_digest txid, std::optional<uint32_t> hint) {
    if (auto const pending = pool.find(txid)) co_return transaction_info{pending->tx, std::nullopt};
    auto const found = co_await locate(txid, hint);
    if ( ! found) co_return std::unexpected(found.error());
    co_return transaction_info{found->tx, found->confirmed};
}

::asio::awaitable<std::expected<merkle_proof, std::error_code>> peer_impl::merkle(hash_digest txid, std::optional<uint32_t> hint) {
    auto const found = co_await locate(txid, hint);
    if ( ! found) co_return std::unexpected(found.error());
    co_return merkle_proof{txid, found->confirmed, found->branch};
}

::asio::awaitable<std::expected<std::pair<domain::chain::transaction, merkle_proof>, std::error_code>>
peer_impl::transaction_at(uint32_t height, uint32_t position) {
    if (height > header_tip()) co_return std::unexpected(error::not_found);
    std::optional<located_tx> found;
    std::vector<uint32_t> const needed{height};
    auto const ec = co_await each_covered(needed, [&](uint32_t, block_ptr const& blk) -> std::error_code {
        auto const& txs = blk->transactions();
        if (position < txs.size()) {
            found = located_tx{txs[position], confirmation{height, blk->hash(), position}, blk->transaction_merkle_branch(position)};
        }
        return {};
    });
    if (ec) co_return std::unexpected(ec);
    if ( ! found) co_return std::unexpected(error::not_found);
    keep_located(*found);
    merkle_proof proof{found->tx.hash(), found->confirmed, found->branch};
    co_return std::pair{found->tx, std::move(proof)};
}

// Every block of the range is downloaded: which blocks an RPA wallet reads
// then names no prefix, whatever the input prefix filters a peer served.
::asio::awaitable<std::expected<std::vector<rpa_match>, std::error_code>>
peer_impl::rpa_history(uint16_t prefix, uint8_t bits, uint32_t from, std::optional<uint32_t> to) {
    if (bits > 16 || (bits < 16 && (uint32_t(prefix) >> bits) != 0)) co_return std::unexpected(error::operation_failed);
    auto const tip = header_tip();
    auto const last = std::min(to.value_or(tip), tip);
    std::vector<rpa_match> out;
    if (from > last) co_return out;
    std::vector<uint32_t> heights;
    for (auto height = from; height <= last; ++height) heights.push_back(height);
    auto const ec = co_await each_block(heights, [&](uint32_t height, block_ptr const& blk) -> std::error_code {
        auto const& txs = blk->transactions();
        for (size_t at = 1; at < txs.size(); ++at) {
            auto inputs = prefix_inputs(txs[at], prefix, bits);
            if ( ! inputs.empty()) out.push_back(rpa_match{txs[at].hash(), height, std::move(inputs)});
        }
        return {};
    });
    if (ec) co_return std::unexpected(ec);
    co_return out;
}

::asio::awaitable<std::expected<std::pair<hash_list, hash_digest>, std::error_code>>
peer_impl::header_branch(uint32_t height, uint32_t cp_height) {
    if (height > cp_height || cp_height > header_tip()) co_return std::unexpected(error::operation_failed);
    co_return tree.branch(height, cp_height, [this](uint32_t at) { return hash_at(at).value_or(null_hash); });
}

// An output a watched target owns is answered from its state. Any other:
// spending it puts its script's key (or its category's) in the spending
// block's script filter, so only the blocks those keys match are read, each
// with its decoys.
::asio::awaitable<std::expected<std::optional<utxo>, std::error_code>> peer_impl::output(point outpoint) {
    if (pool.spender(outpoint)) co_return std::optional<utxo>{};
    for (auto const& [key, state] : targets) {
        auto const owned = state.coins().find(outpoint);
        if (owned == state.coins().end() || ! state.synced() || *state.synced() < header_tip()) continue;
        if (state.is_spent(outpoint)) co_return std::optional<utxo>{};
        auto const& coin = owned->second;
        co_return std::optional<utxo>{utxo{outpoint, coin.value, coin.token, coin.script, coin.height}};
    }
    auto const found = co_await transaction(outpoint.hash(), std::nullopt);
    if ( ! found) {
        if (found.error() == error::not_found) co_return std::optional<utxo>{};
        co_return std::unexpected(found.error());
    }
    auto const& outputs = found->tx.outputs();
    if (outpoint.index() >= outputs.size()) co_return std::optional<utxo>{};
    auto const& out = outputs[outpoint.index()];
    auto const& script = out.script().bytes();
    // OP_RETURN outputs are not spendable outputs.
    if ( ! script.empty() && script.front() == 0x6a) co_return std::optional<utxo>{};
    std::optional<uint32_t> height;
    if (found->confirmed) height = found->confirmed->height;
    utxo result{outpoint, out.value(), out.token_data(), script, height};
    if ( ! height || ! scripts || ! scripts->tip() || *scripts->tip() <= *height) co_return std::optional<utxo>{result};
    std::vector<scan_key> keys;
    if ( ! script.empty()) keys.push_back(scan_key{domain::chain::script_key(script), *height + 1});
    if (out.token_data()) keys.push_back(scan_key{domain::chain::category_key(out.token_data()->id), *height + 1});
    if (keys.empty()) co_return std::optional<utxo>{result};
    auto const hits = co_await scan_keys(keys, *height + 1, *scripts->tip());
    if ( ! hits) co_return std::unexpected(hits.error());
    std::vector<uint32_t> heights;
    for (auto const& [at, matched] : *hits) heights.push_back(at);
    bool spent = false;
    auto const ec = co_await each_covered(heights, [&](uint32_t, block_ptr const& blk) -> std::error_code {
        for (auto const& tx : blk->transactions()) {
            for (auto const& in : tx.inputs()) {
                if (in.previous_output().hash() == outpoint.hash() && in.previous_output().index() == outpoint.index()) spent = true;
            }
        }
        return {};
    });
    if (ec) co_return std::unexpected(ec);
    if (spent) co_return std::optional<utxo>{};
    co_return std::optional<utxo>{result};
}

::asio::awaitable<uint64_t> peer_impl::changed(uint64_t seen, std::chrono::steady_clock::time_point deadline) {
    co_return co_await changes.wait(seen, deadline);
}

::asio::awaitable<touched_keys> peer_impl::touched(uint64_t seen) {
    co_return touched_keys{changes.count(), changes.touched_since(seen)};
}

::asio::awaitable<std::optional<domain::chain::header>> peer_impl::header_at(uint32_t height) {
    co_return header(height);
}

} // namespace detail

// ============================================================================
// service
// ============================================================================

namespace {

// The seed of a store's decoys: made once, kept beside its filters.
std::expected<data_chunk, std::error_code> decoy_seed_of(std::filesystem::path const& folder) {
    auto const path = folder / "decoys.seed";
    data_chunk seed(32);
    {
        std::ifstream in(path, std::ios::binary);
        if (in && in.read(reinterpret_cast<char*>(seed.data()), std::streamsize(seed.size()))) return seed;
    }
    std::random_device device;
    for (auto& byte : seed) byte = uint8_t(device());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<char const*>(seed.data()), std::streamsize(seed.size()));
    out.flush();
    if ( ! out) return std::unexpected(error::file_system);
    return seed;
}

} // namespace

std::expected<std::unique_ptr<service>, std::error_code> service::from_peers(peer_settings settings) {
    if (settings.peers.empty()) return std::unexpected(error::operation_failed);
    auto const folder = settings.directory / std::string(domain::config::chain_name(settings.network,
        domain::config::chain_name_format::display));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return std::unexpected(error::file_system);
    // Room for the headers already kept and for those still to come.
    auto capacity = settings.header_capacity;
    if (capacity == 0) {
        std::error_code missing;
        auto const kept = std::filesystem::file_size(folder / "headers.dat", missing);
        capacity = std::max(detail::default_capacity(settings.network),
            (missing ? size_t{0} : size_t(kept / detail::header_size)) + detail::header_headroom);
    }
    auto seed = decoy_seed_of(folder);
    if ( ! seed) return std::unexpected(seed.error());

    auto state = std::make_unique<detail::peer_impl>(std::move(settings), capacity, std::move(*seed));
    state->folder = folder;
    state->header_file = folder / "headers.dat";
    state->restore();
    auto const block_at = [&state](uint32_t height) { return state->hash_at(height); };
    auto scripts = filter_store::open(folder, detail::script_type, block_at);
    if ( ! scripts) return std::unexpected(scripts.error());
    state->scripts.emplace(std::move(*scripts));
    if (state->settings.input_prefix_filters) {
        auto prefixes = filter_store::open(folder, detail::prefix_type, block_at);
        if ( ! prefixes) return std::unexpected(prefixes.error());
        state->prefixes.emplace(std::move(*prefixes));
    }
    if ( ! state->settings.sent_log.empty()) {
        state->sent_log.open(state->settings.sent_log, std::ios::app);
        if ( ! state->sent_log) return std::unexpected(error::file_system);
    }
    state->update_status();
    state->start_thread();
    return std::unique_ptr<service>(new service(std::unique_ptr<impl>(std::move(state))));
}

} // namespace kth::node::wallet
