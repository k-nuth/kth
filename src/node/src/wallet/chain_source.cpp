// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support on a full node: answered from the node's own chain. A
// target is read from its birthday on by parsing the stored blocks, many at
// once, and applying them in height order, as Satoshi's wallet rescanned:
// outputs to a watched script or category, inputs that spend the target's
// own outpoints. Each block that connects later is read the same way, woken
// by the chain views the node publishes; a reorganization rolls every target
// back to the fork and the chain's blocks are read again. No request leaves
// the node and nothing is stored: the histories are what the wallet owns,
// kept in memory and rebuilt from the chain.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <stop_token>
#include <thread>
#include <tuple>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/node/wallet/blocks.hpp>
#include <kth/node/wallet/detail/service_impl.hpp>

namespace kth::node::wallet {

namespace {

using namespace std::chrono_literals;
using domain::chain::point;
using block_ptr = std::shared_ptr<domain::chain::block const>;

// The heights whose applied blocks are remembered, to find where a
// reorganization forked. Bounded: it does not grow with the chain.
constexpr uint32_t reorg_window = 2016;
// Transactions found by hash alone, kept so that asking again reads no block.
constexpr size_t located_limit = 10'000;
// How long a closing service waits for what still runs on it.
constexpr auto close_wait = 10s;

struct chain_impl final : service::impl {
    blockchain::block_chain& chain;
    domain::config::network network;
    chain_settings settings;

    std::map<hash_digest, target_state> targets;
    mempool_copy pool;
    std::set<hash_digest> pooled;
    // The identities followed (in memory: the node keeps no wallet files).
    detail::identity_memory known_identities;
    // The block applied to the targets at each recent height.
    std::map<uint32_t, hash_digest> applied;
    // The generation of the chain view last followed.
    uint64_t followed = 0;
    // Confirmed transactions found by hash alone, the oldest first out.
    std::map<hash_digest, transaction_info> located;
    std::deque<hash_digest> located_order;
    detail::header_tree tree;
    detail::change_signal changes;
    std::string last_error;
    uint64_t blocks_read = 0;

    mutable std::mutex status_mutex;
    status snapshot;
    std::atomic<bool> stopping{false};

    std::unique_ptr<::asio::io_context> io;
    std::optional<::asio::executor_work_guard<::asio::io_context::executor_type>> work;
    kth::select_channel<int> wake{1};
    kth::select_channel<int> history_lock{1};
    kth::select_channel<int> pool_lock{1};
    std::promise<void> finished;
    std::future<void> done = finished.get_future();
    std::thread thread;

    chain_impl(blockchain::block_chain& owner, domain::config::network net, chain_settings given)
        : chain(owner), network(net), settings(given), pool(given.mempool_bytes),
          io(std::make_unique<::asio::io_context>(1)) {
        (void)history_lock.try_send(0);
        (void)pool_lock.try_send(0);
        work.emplace(io->get_executor());
        update_status();
        thread = std::thread([this] {
            io->run();
            finished.set_value();
        });
    }

    // What still runs on the service ends once it is shut down; whatever a
    // caller still holds after close_wait is cut off.
    ~chain_impl() override {
        stopping = true;
        ::asio::post(*io, [this] { shutdown(); });
        work.reset();
        if (done.wait_for(close_wait) != std::future_status::ready) io->stop();
        if (thread.joinable()) thread.join();
        io.reset();
    }

    void shutdown() {
        wake.close();
        history_lock.close();
        pool_lock.close();
        changes.close();
    }

    uint32_t tip() const {
        auto const view = chain.chain_view();
        return view ? uint32_t(view->connected_tip_height) : 0;
    }

    ::asio::any_io_executor executor() const override { return io->get_executor(); }

    void request_stop() override {
        stopping = true;
        ::asio::post(*io, [this] { shutdown(); });
    }

    bool stopped() const override { return stopping; }

    void update_status() {
        status next;
        next.source = "chain";
        next.header_height = tip();
        next.tip = next.header_height;
        next.synced = true;
        next.transaction_keys = true;
        next.blocks_downloaded = blocks_read;
        next.error = last_error;
        std::lock_guard<std::mutex> guard(status_mutex);
        snapshot = std::move(next);
    }

    status current() const override {
        std::lock_guard<std::mutex> guard(status_mutex);
        return snapshot;
    }

    ::asio::awaitable<std::expected<block_ptr, std::error_code>> fetch_block(uint32_t height) {
        // The node stores no body for the genesis block; it is known.
        if (height == 0) co_return std::make_shared<domain::chain::block const>(detail::genesis_of(network));
        auto fetched = co_await chain.fetch_block(size_t(height));
        if ( ! fetched) co_return std::unexpected(fetched.error());
        ++blocks_read;
        co_return block_ptr(fetched->first);
    }

    // Reads the blocks of [from, to] a batch at a time, the batch's reads
    // running at once, and hands each to `use` in height order (or, when
    // `newest_first`, from `to` down). `use` returns true to stop.
    template <typename Use>
    ::asio::awaitable<std::error_code> each_block(uint32_t from, uint32_t to, bool newest_first, Use use) {
        if (from > to) co_return error::success;
        auto const width = std::max<size_t>(1, settings.parallel_reads);
        auto executor = co_await ::asio::this_coro::executor;
        uint64_t const count = uint64_t(to) - from + 1;
        for (uint64_t read = 0; read < count && ! stopping; read += width) {
            auto const batch = size_t(std::min<uint64_t>(width, count - read));
            std::vector<uint32_t> heights;
            for (size_t i = 0; i < batch; ++i) {
                heights.push_back(newest_first ? uint32_t(to - read - i) : uint32_t(from + read + i));
            }
            std::vector<std::expected<block_ptr, std::error_code>> fetched(batch, std::unexpected(error::not_found));
            {
                kth::task_group reads("wallet block reads", executor);
                for (size_t i = 0; i < batch; ++i) {
                    reads.spawn("wallet block read", [this, &fetched, &heights, i]() -> ::asio::awaitable<void> {
                        fetched[i] = co_await fetch_block(heights[i]);
                    });
                }
                co_await reads.join();
            }
            for (size_t i = 0; i < batch; ++i) {
                if ( ! fetched[i]) co_return fetched[i].error();
                if (use(heights[i], *fetched[i])) co_return error::success;
            }
        }
        co_return stopping ? std::error_code(error::service_stopped) : std::error_code{};
    }

    // ------------------------------------------------------------------------
    // Reorganizations

    bool on_chain(uint32_t height, hash_digest const& hash) const {
        auto const stored = chain.get_block_hash(size_t(height));
        return stored && *stored == hash;
    }

    void remember(uint32_t height, hash_digest const& hash) {
        // The genesis block is never reorganized away.
        if (height == 0) return;
        applied.insert_or_assign(height, hash);
        while (applied.size() > 1 && applied.rbegin()->first - applied.begin()->first >= reorg_window) {
            applied.erase(applied.begin());
        }
    }

    // When the block applied at the highest remembered height is no longer
    // the chain's, every target forgets the blocks above the fork: the
    // highest remembered height whose block still is, or every block, for a
    // reorganization deeper than the window. True when it rolled back.
    bool unwind() {
        if (applied.empty() || on_chain(applied.rbegin()->first, applied.rbegin()->second)) return false;
        uint32_t fork = 0;
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            if (on_chain(it->first, it->second)) {
                fork = it->first;
                break;
            }
        }
        spdlog::info("[wallet] Chain reorganization above height {}", fork);
        for (auto& [key, state] : targets) (void)state.rollback(fork);
        applied.erase(applied.upper_bound(fork), applied.end());
        std::erase_if(located, [fork](auto const& entry) { return entry.second.confirmed->height > fork; });
        std::erase_if(located_order, [this](hash_digest const& txid) { return ! located.contains(txid); });
        changes.bump();
        return true;
    }

    // ------------------------------------------------------------------------
    // Histories

    // Every watched target up to the connected tip: a reorganization rolled
    // back first, then the blocks from the lowest next height on, each
    // applied to the targets that have not read it. A block that does not
    // follow the one applied below it means the chain moved during the read:
    // the rest waits for the next round, which finds the fork.
    ::asio::awaitable<std::error_code> catch_up() {
        if ( ! co_await detail::acquire(history_lock)) co_return error::service_stopped;
        detail::lock_release release{history_lock};
        (void)unwind();
        auto const last = tip();
        uint32_t from = last + 1;
        for (auto const& [key, state] : targets) from = std::min(from, state.next_height());
        if (from > last) co_return error::success;
        bool moved = false;
        auto const ec = co_await each_block(from, last, false, [&](uint32_t height, block_ptr const& blk) {
            auto const below = height > 0 ? applied.find(height - 1) : applied.end();
            if (below != applied.end() && blk->header().previous_block_hash() != below->second) {
                moved = true;
                return true;
            }
            for (auto& [key, state] : targets) {
                if (state.next_height() <= height) (void)state.apply_block(height, blk->hash(), blk->transactions());
            }
            remember(height, blk->hash());
            return false;
        });
        if (ec) co_return ec;
        if (moved) {
            // Read again, from the fork, on the next round.
            followed = 0;
            (void)wake.try_send(0);
            co_return error::success;
        }
        for (auto& [key, state] : targets) state.set_synced(last);
        co_return error::success;
    }

    std::optional<uint64_t> known_value(point const& outpoint) const {
        for (auto const& [key, state] : targets) {
            if (auto const owned = state.coins().find(outpoint); owned != state.coins().end()) return owned->second.value;
        }
        return std::nullopt;
    }

    // The mempool copy follows the node's own mempool: what left it leaves,
    // what entered it is read. One refresh at a time.
    ::asio::awaitable<void> refresh_pool() {
        if ( ! co_await detail::acquire(pool_lock)) co_return;
        detail::lock_release release{pool_lock};
        auto const txids = chain.get_mempool_txids();
        std::set<hash_digest> now(txids.begin(), txids.end());
        if (now == pooled) co_return;
        // Who follows the targets and transactions these touch looks again;
        // the others need not.
        std::vector<hash_digest> keys;
        auto const touching = [&](hash_digest const& txid) {
            auto const* entry = pool.find(txid);
            if (entry == nullptr) return;
            auto const touched = detail::touched_by(entry->tx, pool, targets);
            keys.insert(keys.end(), touched.begin(), touched.end());
        };
        for (auto const& txid : pooled) {
            if (now.contains(txid)) continue;
            touching(txid);
            (void)pool.erase(txid);
        }
        auto expected = pool.size();
        for (auto const& txid : now) {
            if (pooled.contains(txid)) continue;
            auto const tx = co_await chain.fetch_unconfirmed_transaction(txid);
            if ( ! tx || ! *tx) continue;
            if ( ! pool.insert(domain::chain::transaction(**tx), [this](point const& outpoint) { return known_value(outpoint); })) continue;
            ++expected;
            touching(txid);
        }
        pooled = std::move(now);
        // Room made by dropping older transactions touches what they touched.
        if (pool.size() != expected) {
            changes.bump();
        } else {
            changes.touch(keys);
        }
    }

    target_state& watch_one(target const& watched, uint32_t birthday) {
        auto const key = watched.key();
        auto found = targets.find(key);
        if (found == targets.end()) {
            found = targets.emplace(key, target_state(watched, std::max(birthday, settings.birthday))).first;
        }
        return found->second;
    }

    ::asio::awaitable<std::error_code> watch(std::vector<target> watched, uint32_t birthday) override {
        for (auto const& one : watched) (void)watch_one(one, birthday);
        auto const ec = co_await catch_up();
        update_status();
        co_return ec;
    }

    bool unwatch(target const& watched) override {
        return targets.erase(watched.key()) != 0;
    }

    ::asio::awaitable<std::expected<target_state const*, std::error_code>> ready(target const& watched) {
        (void)watch_one(watched, settings.birthday);
        auto const ec = co_await catch_up();
        update_status();
        if (ec) co_return std::unexpected(ec);
        co_await refresh_pool();
        auto const found = targets.find(watched.key());
        if (found == targets.end()) co_return std::unexpected(error::not_found);
        co_return &found->second;
    }

    ::asio::awaitable<std::expected<target_history, std::error_code>> history(target watched) override {
        auto const state = co_await ready(watched);
        if ( ! state) co_return std::unexpected(state.error());
        co_return target_history{(*state)->synced(), (*state)->history(), detail::unconfirmed_of(**state, pool)};
    }

    ::asio::awaitable<std::expected<balance, std::error_code>> get_balance(target watched, token_filter tokens) override {
        auto const state = co_await ready(watched);
        if ( ! state) co_return std::unexpected(state.error());
        co_return detail::balance_of(**state, pool, tokens);
    }

    ::asio::awaitable<std::expected<std::vector<utxo>, std::error_code>> unspent(target watched, token_filter tokens) override {
        auto const state = co_await ready(watched);
        if ( ! state) co_return std::unexpected(state.error());
        co_return detail::unspent_of(**state, pool, tokens);
    }

    // ------------------------------------------------------------------------
    // Transactions by hash

    void keep_located(transaction_info const& info) {
        auto const txid = info.tx.hash();
        if (located.contains(txid)) return;
        located.emplace(txid, info);
        located_order.push_back(txid);
        while (located_order.size() > located_limit) {
            located.erase(located_order.front());
            located_order.pop_front();
        }
    }

    // The confirmed transaction with this hash: one found before (while its
    // block is still the chain's), the height hint, a watched history, then
    // the stored blocks from the tip down. No index: the node reads its own
    // blocks, as a wallet reads its filters.
    ::asio::awaitable<std::expected<transaction_info, std::error_code>> locate(hash_digest const& txid, std::optional<uint32_t> hint) {
        if (auto const known = located.find(txid); known != located.end()) {
            auto const& at = *known->second.confirmed;
            if (on_chain(at.height, at.block)) co_return known->second;
        }
        auto const find_in = [&](block_ptr const& blk) -> std::optional<uint32_t> {
            auto const& txs = blk->transactions();
            for (size_t at = 0; at < txs.size(); ++at) {
                if (txs[at].hash() == txid) return uint32_t(at);
            }
            return std::nullopt;
        };
        auto const found_at = [&](block_ptr const& blk, uint32_t height, uint32_t position) {
            transaction_info info{blk->transactions()[position], confirmation{height, blk->hash(), position}};
            keep_located(info);
            return info;
        };
        std::vector<uint32_t> candidates;
        if (hint) candidates.push_back(*hint);
        for (auto const& [key, state] : targets) {
            for (auto const& entry : state.history()) {
                if (entry.txid == txid) candidates.push_back(entry.height);
            }
        }
        for (auto const height : candidates) {
            if (height > tip()) continue;
            auto const blk = co_await fetch_block(height);
            if ( ! blk) continue;
            if (auto const at = find_in(*blk)) co_return found_at(*blk, height, *at);
        }
        std::optional<transaction_info> found;
        auto const ec = co_await each_block(0, tip(), true, [&](uint32_t height, block_ptr const& blk) {
            if (auto const at = find_in(blk)) {
                found = found_at(blk, height, *at);
                return true;
            }
            return false;
        });
        if (found) co_return *found;
        if (ec) co_return std::unexpected(ec);
        co_return std::unexpected(error::not_found);
    }

    ::asio::awaitable<std::expected<transaction_info, std::error_code>> transaction(hash_digest txid, std::optional<uint32_t> hint) override {
        co_await refresh_pool();
        if (auto const pending = pool.find(txid)) co_return transaction_info{pending->tx, std::nullopt};
        co_return co_await locate(txid, hint);
    }

    ::asio::awaitable<std::expected<merkle_proof, std::error_code>> merkle(hash_digest txid, std::optional<uint32_t> hint) override {
        auto const found = co_await locate(txid, hint);
        if ( ! found) co_return std::unexpected(found.error());
        auto const at = *found->confirmed;
        auto const blk = co_await fetch_block(at.height);
        if ( ! blk) co_return std::unexpected(blk.error());
        co_return merkle_proof{txid, at, (*blk)->transaction_merkle_branch(at.position)};
    }

    ::asio::awaitable<std::expected<std::pair<domain::chain::transaction, merkle_proof>, std::error_code>>
    transaction_at(uint32_t height, uint32_t position) override {
        if (height > tip()) co_return std::unexpected(error::not_found);
        auto const blk = co_await fetch_block(height);
        if ( ! blk) co_return std::unexpected(blk.error());
        auto const& txs = (*blk)->transactions();
        if (position >= txs.size()) co_return std::unexpected(error::not_found);
        merkle_proof proof{txs[position].hash(), confirmation{height, (*blk)->hash(), position},
            (*blk)->transaction_merkle_branch(position)};
        co_return std::pair{txs[position], std::move(proof)};
    }

    ::asio::awaitable<std::expected<std::vector<rpa_match>, std::error_code>>
    rpa_history(uint16_t prefix, uint8_t bits, uint32_t from, std::optional<uint32_t> to) override {
        if (bits > 16 || (bits < 16 && (uint32_t(prefix) >> bits) != 0)) co_return std::unexpected(error::operation_failed);
        auto const last = std::min(to.value_or(tip()), tip());
        std::vector<rpa_match> out;
        auto const ec = co_await each_block(from, last, false, [&](uint32_t height, block_ptr const& blk) {
            auto const& txs = blk->transactions();
            for (size_t at = 1; at < txs.size(); ++at) {
                auto inputs = prefix_inputs(txs[at], prefix, bits);
                if ( ! inputs.empty()) out.push_back(rpa_match{txs[at].hash(), height, std::move(inputs)});
            }
            return false;
        });
        if (ec) co_return std::unexpected(ec);
        co_return out;
    }

    std::vector<rpa_match> rpa_mempool(uint16_t prefix, uint8_t bits) const override {
        std::vector<rpa_match> out;
        for (auto const& [txid, entry] : pool.transactions()) {
            auto inputs = prefix_inputs(entry.tx, prefix, bits);
            if ( ! inputs.empty()) out.push_back(rpa_match{txid, std::nullopt, std::move(inputs)});
        }
        return out;
    }

    // Into the node's own mempool, which relays it.
    ::asio::awaitable<std::expected<hash_digest, std::error_code>> broadcast(domain::chain::transaction tx) override {
        auto const txid = tx.hash();
        auto const pointer = std::make_shared<domain::message::transaction const>(std::move(tx));
        auto const ec = co_await chain.submit_transaction(pointer);
        if (ec) {
            last_error = "transaction rejected: " + ec.message();
            co_return std::unexpected(ec);
        }
        co_await refresh_pool();
        co_return txid;
    }

    // The node's own relay floor (chain.byte_fee_satoshis per byte), which
    // its estimatefee answers too.
    fee_estimate fees() const override {
        auto const relay = std::max<uint64_t>(1, uint64_t(std::llround(double(chain.chain_settings().byte_fee_satoshis) * 1000.0)));
        return fee_estimate{relay, relay};
    }

    mempool_summary mempool() const override {
        mempool_summary out{pool.size(), pool.bytes(), {}};
        for (auto const& [txid, entry] : pool.transactions()) {
            if (entry.fee) out.fee_rates.emplace_back(double(*entry.fee) / double(std::max<size_t>(1, entry.size)), entry.size);
        }
        return out;
    }

    ::asio::awaitable<std::expected<std::pair<hash_list, hash_digest>, std::error_code>>
    header_branch(uint32_t height, uint32_t cp_height) override {
        if (height > cp_height || cp_height > tip()) co_return std::unexpected(error::operation_failed);
        auto const& index = chain.headers();
        co_return tree.branch(height, cp_height, [&index](uint32_t at) { return index.get_hash(index.active_at(int32_t(at))); });
    }

    // An output a watched target owns is answered from its state; any other
    // is found by reading its transaction's block and the blocks after it.
    // A confirmed output from the node's UTXO set: the output, or none when
    // it is spent (or never was); no answer while the set is not built to
    // the tip.
    std::optional<std::optional<utxo>> utxo_of(point const& outpoint) const {
        auto const built = chain.get_utxo_built_height();
        if ( ! built || *built < tip()) return std::nullopt;
        auto const stored = chain.get_utxo(domain::chain::output_point{outpoint.hash(), outpoint.index()}, tip());
        if ( ! stored) {
            if (stored.error() != database::result_code::key_not_found) return std::nullopt;
            return std::optional<utxo>{};
        }
        auto const& out = stored->output;
        if ( ! out.script().bytes().empty() && out.script().bytes().front() == 0x6a) return std::optional<utxo>{};
        return std::optional<utxo>{utxo{outpoint, out.value(), out.token_data(), out.script().bytes(), uint32_t(stored->height)}};
    }

    // A watched target that owned the output saw its spend; an output the
    // UTXO set holds has none confirmed; otherwise the node's own blocks
    // are read forward from `from` until one spends it. Then the mempool.
    ::asio::awaitable<std::expected<std::optional<transaction_info>, std::error_code>> spender_of(
            point outpoint, data_chunk script, std::optional<uint32_t> from) override {
        (void)script;
        co_await refresh_pool();
        if (from) {
            std::optional<uint32_t> start = *from;
            for (auto const& [key, state] : targets) {
                for (auto const& entry : state.history()) {
                    if (entry.height < *from) continue;
                    if (std::ranges::any_of(entry.inputs, [&](spent const& in) { return in.prevout == outpoint; })) {
                        start = entry.height;
                        break;
                    }
                }
            }
            auto const unspent = utxo_of(outpoint);
            if ( ! unspent || ! *unspent) {
                std::optional<transaction_info> found;
                auto const ec = co_await each_block(*start, tip(), false, [&](uint32_t height, block_ptr const& blk) {
                    auto const& txs = blk->transactions();
                    for (uint32_t at = 0; at < txs.size(); ++at) {
                        auto const spending = std::ranges::any_of(txs[at].inputs(), [&](domain::chain::input const& in) {
                            return in.previous_output().hash() == outpoint.hash() && in.previous_output().index() == outpoint.index();
                        });
                        if ( ! spending) continue;
                        found = transaction_info{txs[at], confirmation{height, blk->hash(), at}};
                        return true;
                    }
                    return false;
                });
                if (found) co_return found;
                if (ec) co_return std::unexpected(ec);
            }
        }
        if (auto const spender = pool.spender(outpoint)) {
            if (auto const* entry = pool.find(*spender)) co_return std::optional<transaction_info>{transaction_info{entry->tx, std::nullopt}};
        }
        co_return std::optional<transaction_info>{};
    }

    detail::identity_memory& identities() override { return known_identities; }

    ::asio::awaitable<std::expected<std::optional<utxo>, std::error_code>> output(point outpoint) override {
        co_await refresh_pool();
        if (pool.spender(outpoint)) co_return std::optional<utxo>{};
        for (auto const& [key, state] : targets) {
            auto const owned = state.coins().find(outpoint);
            if (owned == state.coins().end()) continue;
            if (state.is_spent(outpoint)) co_return std::optional<utxo>{};
            auto const& coin = owned->second;
            co_return std::optional<utxo>{utxo{outpoint, coin.value, coin.token, coin.script, coin.height}};
        }
        // The node's UTXO set, when it is built to the tip, answers for a
        // confirmed output at once: there, or spent.
        auto const unspent = utxo_of(outpoint);
        if (unspent && *unspent) co_return std::optional<utxo>{**unspent};
        auto const found = co_await transaction(outpoint.hash(), std::nullopt);
        if ( ! found) {
            if (found.error() == error::not_found) co_return std::optional<utxo>{};
            co_return std::unexpected(found.error());
        }
        auto const& outputs = found->tx.outputs();
        if (outpoint.index() >= outputs.size()) co_return std::optional<utxo>{};
        auto const& out = outputs[outpoint.index()];
        auto const& script = out.script().bytes();
        if ( ! script.empty() && script.front() == 0x6a) co_return std::optional<utxo>{};
        std::optional<uint32_t> height;
        if (found->confirmed) height = found->confirmed->height;
        utxo result{outpoint, out.value(), out.token_data(), script, height};
        if ( ! height) co_return std::optional<utxo>{result};
        if (unspent) co_return std::optional<utxo>{};
        bool spent = false;
        auto const ec = co_await each_block(*height + 1, tip(), false, [&](uint32_t, block_ptr const& blk) {
            for (auto const& tx : blk->transactions()) {
                for (auto const& in : tx.inputs()) {
                    if (in.previous_output().hash() == outpoint.hash() && in.previous_output().index() == outpoint.index()) {
                        spent = true;
                        return true;
                    }
                }
            }
            return false;
        });
        if (ec) co_return std::unexpected(ec);
        if (spent) co_return std::optional<utxo>{};
        co_return std::optional<utxo>{result};
    }

    // The proofs the node holds that are about a transaction of its mempool.
    ::asio::awaitable<std::vector<dsproof_info>> own_dsproofs() {
        co_await refresh_pool();
        std::vector<dsproof_info> out;
        auto const proofs = co_await chain.fetch_ds_proofs();
        if ( ! proofs) co_return out;
        for (auto const& proof : *proofs) {
            if ( ! proof) continue;
            data_chunk raw(proof->serialized_size(0));
            byte_writer writer(raw);
            if ( ! proof->to_data(writer, 0)) continue;
            dsproof_info info{proof->hash(), null_hash, std::move(raw),
                point(proof->out_point().hash(), proof->out_point().index()), {}};
            detail::describe(info, pool);
            if (info.txid != null_hash) out.push_back(std::move(info));
        }
        co_return out;
    }

    ::asio::awaitable<std::optional<dsproof_info>> dsproof(hash_digest id) override {
        auto proofs = co_await own_dsproofs();
        for (auto& info : proofs) {
            if (info.dspid == id || std::ranges::find(info.descendants, id) != info.descendants.end()) co_return std::move(info);
        }
        co_return std::nullopt;
    }

    ::asio::awaitable<std::vector<hash_digest>> dsproof_transactions() override {
        std::set<hash_digest> all;
        auto const proofs = co_await own_dsproofs();
        for (auto const& info : proofs) all.insert(info.descendants.begin(), info.descendants.end());
        co_return std::vector<hash_digest>(all.begin(), all.end());
    }

    ::asio::awaitable<uint64_t> changed(uint64_t seen, std::chrono::steady_clock::time_point deadline) override {
        co_return co_await changes.wait(seen, deadline);
    }

    ::asio::awaitable<touched_keys> touched(uint64_t seen) override {
        co_return touched_keys{changes.count(), changes.touched_since(seen)};
    }

    ::asio::awaitable<std::optional<domain::chain::header>> header_at(uint32_t height) override {
        if (height > tip()) co_return std::nullopt;
        auto const fetched = co_await chain.fetch_block_header(size_t(height));
        if ( ! fetched || ! fetched->first) co_return std::nullopt;
        co_return domain::chain::header(*fetched->first);
    }

    // ------------------------------------------------------------------------
    // Following the node

    // Rings the service for each event on a node channel, until it closes.
    template <typename Channel>
    ::asio::awaitable<void> relay(Channel channel) {
        for (;;) {
            auto const received = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (std::get<0>(received)) co_return;
            (void)wake.try_send(0);
        }
    }

    // What the node changed since the last round: a new chain view (a block,
    // a reorganization) is read for every target, and the mempool copy
    // follows the node's mempool.
    ::asio::awaitable<void> follow() {
        auto const view = chain.chain_view();
        auto const generation = view ? view->generation : 0;
        if (generation != followed) {
            followed = generation;
            if ( ! targets.empty()) {
                auto const ec = co_await catch_up();
                if ( ! ec) {
                    last_error.clear();
                } else if (ec != error::service_stopped) {
                    last_error = ec.message();
                }
            }
            changes.bump();
        }
        co_await refresh_pool();
        update_status();
    }

    // Woken by the node's own events, never by a timer: each chain view it
    // publishes, each transaction its mempool accepts, each double-spend
    // proof, and stop().
    ::asio::awaitable<std::error_code> run() override {
        auto executor = co_await ::asio::this_coro::executor;
        auto views = chain.subscribe_chain_view();
        auto transactions = chain.subscribe_transaction();
        auto proofs = chain.subscribe_ds_proof();
        {
            kth::task_group relays("wallet chain events", executor);
            if (views) relays.spawn("wallet chain views", relay(views));
            if (transactions) relays.spawn("wallet mempool", relay(transactions));
            if (proofs) relays.spawn("wallet double-spend proofs", relay(proofs));
            while ( ! stopping) {
                co_await follow();
                if ( ! co_await detail::acquire(wake)) break;
            }
            chain.unsubscribe_chain_view(views);
            chain.unsubscribe_transaction(transactions);
            chain.unsubscribe_ds_proof(proofs);
            co_await relays.join();
        }
        update_status();
        co_return error::success;
    }
};

} // namespace

std::unique_ptr<service::impl> detail::make_chain_impl(blockchain::block_chain& chain, domain::config::network network,
        chain_settings settings) {
    return std::make_unique<chain_impl>(chain, network, settings);
}

} // namespace kth::node::wallet
