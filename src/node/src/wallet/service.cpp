// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// One set of calls behind a source of answers: the node's own chain
// (chain_source.cpp). What does not depend on the source, answered from a
// target's confirmed state and the mempool transactions the source knows, is
// here.

#include <kth/node/wallet/service.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <stop_token>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/node/wallet/detail/service_impl.hpp>

namespace kth::node::wallet {

namespace {

using domain::chain::point;

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

} // namespace

namespace detail {

namespace {

unconfirmed_tx new_entry(mempool_tx const& entry, mempool_copy const& pool) {
    return unconfirmed_tx{entry.txid, pool.has_unconfirmed_parent(entry.tx), entry.fee, {}, {}};
}

} // namespace

std::vector<unconfirmed_tx> unconfirmed_of(target_state const& state, mempool_copy const& pool) {
    std::map<point, std::pair<uint64_t, domain::chain::token_data_opt>> owned;
    std::map<hash_digest, unconfirmed_tx> entries;
    for (auto const& [txid, entry] : pool.transactions()) {
        auto const& outputs = entry.tx.outputs();
        for (uint32_t vout = 0; vout < outputs.size(); ++vout) {
            if ( ! state.watched().owns(outputs[vout])) continue;
            owned.insert_or_assign(point(txid, vout), std::pair{outputs[vout].value(), outputs[vout].token_data()});
            auto [found, inserted] = entries.try_emplace(txid, new_entry(entry, pool));
            (void)inserted;
            found->second.outputs.push_back(received{vout, outputs[vout].value(), outputs[vout].token_data(), outputs[vout].script().bytes()});
        }
    }
    for (auto const& [txid, entry] : pool.transactions()) {
        auto const& inputs = entry.tx.inputs();
        for (uint32_t vin = 0; vin < inputs.size(); ++vin) {
            point const outpoint(inputs[vin].previous_output().hash(), inputs[vin].previous_output().index());
            std::optional<std::pair<uint64_t, domain::chain::token_data_opt>> coin_of;
            if (auto const confirmed = state.coins().find(outpoint); confirmed != state.coins().end()) {
                coin_of = std::pair{confirmed->second.value, confirmed->second.token};
            } else if (auto const pending = owned.find(outpoint); pending != owned.end()) {
                coin_of = pending->second;
            }
            if ( ! coin_of) continue;
            auto [found, inserted] = entries.try_emplace(txid, new_entry(entry, pool));
            (void)inserted;
            found->second.inputs.push_back(spent{vin, outpoint, coin_of->first, coin_of->second});
        }
    }
    std::vector<unconfirmed_tx> out;
    for (auto& [txid, entry] : entries) out.push_back(std::move(entry));
    std::ranges::sort(out, [](unconfirmed_tx const& a, unconfirmed_tx const& b) {
        if (a.unconfirmed_parent != b.unconfirmed_parent) return ! a.unconfirmed_parent;
        return encode_hash(a.txid) < encode_hash(b.txid);
    });
    return out;
}

balance balance_of(target_state const& state, mempool_copy const& pool, token_filter tokens) {
    balance result{0, 0};
    for (auto const& owned : state.unspent()) {
        if (keeps(tokens, owned.token)) result.confirmed += owned.value;
    }
    for (auto const& entry : unconfirmed_of(state, pool)) {
        for (auto const& out : entry.outputs) {
            if (keeps(tokens, out.token)) result.unconfirmed += int64_t(out.value);
        }
        for (auto const& in : entry.inputs) {
            if (keeps(tokens, in.token)) result.unconfirmed -= int64_t(in.value);
        }
    }
    return result;
}

std::vector<utxo> unspent_of(target_state const& state, mempool_copy const& pool, token_filter tokens) {
    std::vector<utxo> out;
    for (auto const& owned : state.unspent()) {
        if (keeps(tokens, owned.token) && ! pool.spender(owned.outpoint)) {
            out.push_back(utxo{owned.outpoint, owned.value, owned.token, owned.script, owned.height});
        }
    }
    for (auto const& entry : unconfirmed_of(state, pool)) {
        for (auto const& received_output : entry.outputs) {
            point const outpoint(entry.txid, received_output.vout);
            if (keeps(tokens, received_output.token) && ! pool.spender(outpoint)) {
                out.push_back(utxo{outpoint, received_output.value, received_output.token, received_output.script, std::nullopt});
            }
        }
    }
    return out;
}

domain::chain::block genesis_of(domain::config::network network) {
    return genesis_block(network);
}

std::pair<hash_list, hash_digest> header_tree::branch(uint32_t height, uint32_t cp_height,
        std::function<hash_digest(uint32_t)> const& hash_at) {
    auto const top = hash_at(cp_height);
    if (levels_.empty() || cp_height_ != cp_height || cp_hash_ != top) {
        levels_.clear();
        hash_list leaves;
        leaves.reserve(size_t(cp_height) + 1);
        for (uint32_t at = 0; at <= cp_height; ++at) leaves.push_back(hash_at(at));
        levels_.push_back(std::move(leaves));
        while (levels_.back().size() > 1) {
            auto const& below = levels_.back();
            hash_list next;
            next.reserve((below.size() + 1) / 2);
            for (size_t at = 0; at < below.size(); at += 2) {
                auto const& right = at + 1 < below.size() ? below[at + 1] : below[at];
                data_chunk pair(below[at].begin(), below[at].end());
                pair.insert(pair.end(), right.begin(), right.end());
                next.push_back(bitcoin_hash(pair));
            }
            levels_.push_back(std::move(next));
        }
        cp_height_ = cp_height;
        cp_hash_ = top;
    }
    hash_list out;
    size_t index = height;
    for (size_t level = 0; level + 1 < levels_.size(); ++level) {
        auto const& nodes = levels_[level];
        out.push_back(nodes[(index ^ 1) < nodes.size() ? (index ^ 1) : index]);
        index /= 2;
    }
    return {std::move(out), levels_.back().front()};
}

::asio::awaitable<bool> acquire(kth::select_channel<int>& lock) {
    auto executor = co_await ::asio::this_coro::executor;
    auto completion = co_await ::asio::experimental::make_select(::asio::experimental::receive(lock))
        .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    (void)selected;
    co_return ! ec;
}

std::vector<hash_digest> touched_by(domain::chain::transaction const& tx, mempool_copy const& pool,
        std::map<hash_digest, target_state> const& targets) {
    std::vector<hash_digest> out{tx.hash()};
    auto const paid = [&](domain::chain::output const& output) {
        for (auto const& [key, state] : targets) {
            if (state.watched().owns(output)) out.push_back(key);
        }
    };
    for (auto const& output : tx.outputs()) paid(output);
    for (auto const& in : tx.inputs()) {
        domain::chain::point const prevout(in.previous_output().hash(), in.previous_output().index());
        if (auto const* parent = pool.find(prevout.hash()); parent != nullptr && prevout.index() < parent->tx.outputs().size()) {
            paid(parent->tx.outputs()[prevout.index()]);
        }
        for (auto const& [key, state] : targets) {
            if (state.coins().contains(prevout)) out.push_back(key);
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void describe(dsproof_info& info, mempool_copy const& pool) {
    info.descendants.clear();
    auto const spender = pool.spender(info.outpoint);
    info.txid = spender.value_or(null_hash);
    if ( ! spender) return;
    std::vector<hash_digest> pending{*spender};
    std::set<hash_digest> seen;
    while ( ! pending.empty()) {
        auto const txid = pending.back();
        pending.pop_back();
        if ( ! seen.insert(txid).second) continue;
        info.descendants.push_back(txid);
        auto const* entry = pool.find(txid);
        if (entry == nullptr) continue;
        for (uint32_t vout = 0; vout < entry->tx.outputs().size(); ++vout) {
            if (auto const child = pool.spender(point(txid, vout))) pending.push_back(*child);
        }
    }
}

} // namespace detail

// ============================================================================
// service
// ============================================================================

std::unique_ptr<service> service::from_chain(blockchain::block_chain& chain, domain::config::network network,
        chain_settings settings) {
    return std::unique_ptr<service>(new service(detail::make_chain_impl(chain, network, settings)));
}

service::service(std::unique_ptr<impl> state) : impl_(std::move(state)) {}

service::~service() = default;

::asio::any_io_executor service::executor() const {
    return impl_->executor();
}

::asio::awaitable<std::error_code> service::run() {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->run(), ::asio::use_awaitable);
}

void service::stop() {
    impl_->request_stop();
}

status service::current() const {
    return impl_->current();
}

// Woken by each change, not by asking again on a timer. The count is read
// before the status, so a change between them is not missed.
::asio::awaitable<std::expected<status, std::error_code>> service::wait_synced(std::chrono::seconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    uint64_t seen = co_await ::asio::co_spawn(impl_->executor(), impl_->changed(0, std::chrono::steady_clock::now()), ::asio::use_awaitable);
    for (;;) {
        auto const now = impl_->current();
        if (now.synced) co_return now;
        if (impl_->stopped()) co_return std::unexpected(error::service_stopped);
        if (std::chrono::steady_clock::now() >= deadline) co_return std::unexpected(error::channel_timeout);
        seen = co_await ::asio::co_spawn(impl_->executor(), impl_->changed(seen, deadline), ::asio::use_awaitable);
    }
}

::asio::awaitable<std::error_code> service::watch(std::vector<target> targets, uint32_t birthday) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->watch(std::move(targets), birthday), ::asio::use_awaitable);
}

::asio::awaitable<bool> service::unwatch(target watched) {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state, watched]() -> ::asio::awaitable<bool> {
        co_return state->unwatch(watched);
    }, ::asio::use_awaitable);
}

::asio::awaitable<std::expected<target_history, std::error_code>> service::history(target watched) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->history(std::move(watched)), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<balance, std::error_code>> service::get_balance(target watched, token_filter tokens) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->get_balance(std::move(watched), tokens), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::vector<utxo>, std::error_code>> service::unspent(target watched, token_filter tokens) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->unspent(std::move(watched), tokens), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<transaction_info, std::error_code>> service::transaction(hash_digest txid, std::optional<uint32_t> height_hint) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->transaction(txid, height_hint), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<merkle_proof, std::error_code>> service::merkle(hash_digest txid, std::optional<uint32_t> height_hint) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->merkle(txid, height_hint), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::pair<domain::chain::transaction, merkle_proof>, std::error_code>>
service::transaction_at(uint32_t height, uint32_t position) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->transaction_at(height, position), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::vector<rpa_match>, std::error_code>>
service::rpa_history(uint16_t prefix, uint8_t bits, uint32_t from, std::optional<uint32_t> to) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->rpa_history(prefix, bits, from, to), ::asio::use_awaitable);
}

::asio::awaitable<std::vector<rpa_match>> service::rpa_mempool(uint16_t prefix, uint8_t bits) {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state, prefix, bits]() -> ::asio::awaitable<std::vector<rpa_match>> {
        co_return state->rpa_mempool(prefix, bits);
    }, ::asio::use_awaitable);
}

::asio::awaitable<std::expected<hash_digest, std::error_code>> service::broadcast(domain::chain::transaction tx) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->broadcast(std::move(tx)), ::asio::use_awaitable);
}

::asio::awaitable<fee_estimate> service::fees() {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state]() -> ::asio::awaitable<fee_estimate> {
        co_return state->fees();
    }, ::asio::use_awaitable);
}

::asio::awaitable<mempool_summary> service::mempool() {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state]() -> ::asio::awaitable<mempool_summary> {
        co_return state->mempool();
    }, ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::pair<hash_list, hash_digest>, std::error_code>>
service::header_branch(uint32_t height, uint32_t cp_height) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->header_branch(height, cp_height), ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::optional<utxo>, std::error_code>> service::output(domain::chain::point outpoint) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->output(outpoint), ::asio::use_awaitable);
}

::asio::awaitable<std::optional<dsproof_info>> service::dsproof(hash_digest dspid_or_txid) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->dsproof(dspid_or_txid), ::asio::use_awaitable);
}

::asio::awaitable<std::vector<hash_digest>> service::dsproof_transactions() {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->dsproof_transactions(), ::asio::use_awaitable);
}

::asio::awaitable<uint64_t> service::changed(uint64_t seen, std::chrono::seconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->changed(seen, deadline), ::asio::use_awaitable);
}

// ----------------------------------------------------------------------------
// CashTokens and BCMR, from what either source answers

namespace {

// An authchain longer than this is not followed further.
constexpr size_t identity_steps_limit = 100'000;

identity_step step_of(transaction_info const& found) {
    identity_step out;
    out.txid = found.tx.hash();
    if (found.confirmed) {
        out.from = identity_step::source::chain;
        out.height = found.confirmed->height;
        out.block = found.confirmed->block;
    } else {
        out.from = identity_step::source::mempool;
    }
    return out;
}

bool spends(domain::chain::transaction const& tx, point const& outpoint) {
    return std::ranges::any_of(tx.inputs(), [&](domain::chain::input const& in) {
        return in.previous_output().hash() == outpoint.hash() && in.previous_output().index() == outpoint.index();
    });
}

::asio::awaitable<std::expected<category_state, std::error_code>> category_of(service::impl& state, hash_digest id) {
    auto const watched = target::of_category(id);
    auto const history = co_await state.history(watched);
    if ( ! history) co_return std::unexpected(history.error());
    auto unspent = co_await state.unspent(watched, token_filter::include);
    if ( ! unspent) co_return std::unexpected(unspent.error());
    category_state out;
    out.category = id;
    std::ranges::stable_sort(*unspent, [](utxo const& a, utxo const& b) { return a.height.has_value() && ! b.height.has_value(); });
    out.unspent = std::move(*unspent);
    out.supply = supply_of(id, out.unspent, false);
    out.mempool_supply = supply_of(id, out.unspent, true);
    std::set<data_chunk> scripts;
    for (auto const& coin : out.unspent) {
        if ( ! coin.token || coin.token->id != id) continue;
        scripts.insert(coin.script);
        if (domain::chain::has_nft(*coin.token)) ++out.nfts;
    }
    out.holders = scripts.size();

    // The genesis spends output 0 of the transaction whose hash is the
    // category id; no output of the category comes before it, so it is the
    // first transaction of the category's history when that reaches back.
    point const base(id, 0);
    if ( ! history->confirmed.empty()) {
        auto const& first = history->confirmed.front();
        auto const found = co_await state.transaction(first.txid, first.height);
        if (found && spends(found->tx, base)) {
            out.genesis = first.txid;
            out.genesis_height = first.height;
        }
    } else {
        for (auto const& pending : history->unconfirmed) {
            auto const found = co_await state.transaction(pending.txid, std::nullopt);
            if (found && spends(found->tx, base)) {
                out.genesis = pending.txid;
                break;
            }
        }
    }
    co_return out;
}

::asio::awaitable<std::expected<identity, std::error_code>> identity_of(service::impl& state, hash_digest authbase,
        std::vector<domain::chain::transaction> extension) {
    std::vector<identity_step> chain;
    std::optional<domain::chain::transaction> head;
    if ( ! extension.empty()) {
        // Checked link by link; then the last one is looked up, and the walk
        // goes on from where it is.
        auto const txids = verify_authchain(authbase, extension);
        if ( ! txids) co_return std::unexpected(txids.error());
        auto const found = co_await state.transaction(txids->back(), std::nullopt);
        if ( ! found) co_return std::unexpected(found.error() == error::not_found ? std::error_code(error::bad_stream) : found.error());
        for (size_t at = 0; at + 1 < txids->size(); ++at) {
            chain.push_back(identity_step{(*txids)[at], identity_step::source::registry, 0, null_hash});
        }
        chain.push_back(step_of(*found));
        head = found->tx;
    } else if (auto const known = state.identities().find(authbase)) {
        // What was read before, while each block it was read in is still
        // the chain's.
        for (auto const& kept : *known) {
            if (kept.from == identity_step::source::chain) {
                auto const header = co_await state.header_at(kept.height);
                if ( ! header || domain::chain::hash(*header) != kept.block) break;
            }
            chain.push_back(kept);
        }
        while ( ! chain.empty() && chain.back().from != identity_step::source::chain) chain.pop_back();
    }
    if (chain.empty()) {
        auto const found = co_await state.transaction(authbase, std::nullopt);
        if ( ! found) co_return std::unexpected(found.error());
        chain.push_back(step_of(*found));
        head = found->tx;
    }

    identity out;
    out.authbase = authbase;
    for (size_t steps = 0; steps < identity_steps_limit; ++steps) {
        auto const& last = chain.back();
        if ( ! head) {
            auto const hint = last.from == identity_step::source::chain ? std::optional<uint32_t>(last.height) : std::nullopt;
            auto const found = co_await state.transaction(last.txid, hint);
            if ( ! found) co_return std::unexpected(found.error());
            head = found->tx;
        }
        if (head->outputs().empty()) co_return std::unexpected(error::bad_stream);
        auto const identity_script = head->outputs().front().script().bytes();
        if (is_data_carrier(identity_script)) {
            out.burned = true;
            break;
        }
        auto const from = last.from == identity_step::source::chain ? std::optional<uint32_t>(last.height) : std::nullopt;
        auto const next = co_await state.spender_of(point(last.txid, 0), identity_script, from);
        if ( ! next) co_return std::unexpected(next.error());
        if ( ! *next) break;
        chain.push_back(step_of(**next));
        head = (*next)->tx;
    }
    auto publications = bcmr_publications(*head);
    if ( ! publications.empty()) {
        out.publication = publications.front();
        out.ignored.assign(publications.begin() + 1, publications.end());
    }
    out.chain = chain;
    state.identities().keep(authbase, chain);

    // The identity output is watched from the authhead on: its spend shows
    // up as that script's history moves, and is read from there next time.
    auto const& authhead = chain.back();
    if ( ! out.burned && authhead.from == identity_step::source::chain) {
        std::vector<target> identity_output{target::of_script(head->outputs().front().script().bytes())};
        (void)co_await state.watch(std::move(identity_output), authhead.height);
    }
    co_return out;
}

} // namespace

::asio::awaitable<std::expected<category_state, std::error_code>> service::category(hash_digest id) {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state, id]() -> ::asio::awaitable<std::expected<category_state, std::error_code>> {
        co_return co_await category_of(*state, id);
    }, ::asio::use_awaitable);
}

::asio::awaitable<std::expected<std::vector<token_holding>, std::error_code>> service::holdings(target watched) {
    auto const unspent = co_await this->unspent(std::move(watched), token_filter::include);
    if ( ! unspent) co_return std::unexpected(unspent.error());
    co_return holdings_of(*unspent);
}

::asio::awaitable<std::expected<identity, std::error_code>> service::resolve_identity(hash_digest authbase,
        std::vector<domain::chain::transaction> extension) {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(),
        [state, authbase, extension = std::move(extension)]() mutable -> ::asio::awaitable<std::expected<identity, std::error_code>> {
            co_return co_await identity_of(*state, authbase, std::move(extension));
        }, ::asio::use_awaitable);
}

::asio::awaitable<touched_keys> service::touched(uint64_t seen) {
    co_return co_await ::asio::co_spawn(impl_->executor(), impl_->touched(seen), ::asio::use_awaitable);
}

::asio::awaitable<std::optional<domain::chain::header>> service::header(uint32_t height) {
    auto* state = impl_.get();
    co_return co_await ::asio::co_spawn(impl_->executor(), [state, height]() -> ::asio::awaitable<std::optional<domain::chain::header>> {
        co_return co_await state->header_at(height);
    }, ::asio::use_awaitable);
}

} // namespace kth::node::wallet
